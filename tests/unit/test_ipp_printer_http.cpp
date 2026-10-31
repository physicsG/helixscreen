// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_ipp_printer_http.cpp
 * @brief The IPP job POST against a real loopback socket server.
 *
 * Some printers (Canon PIXMA) answer a POST with an unsolicited interim
 * `HTTP/1.1 100 Continue` before the final response, with or without an
 * `Expect:` header. The client has to read past it to the final response
 * (RFC 9110 15.2). The server here is a raw socket so the test controls
 * exactly which bytes arrive in which write (prestonbrown/helixscreen#1822).
 */

#include "../lvgl_test_fixture.h"
#include "../lvgl_ui_test_fixture.h"
#include "config.h"
#include "hv/HttpMessage.h"
#include "ipp_print_modal.h"
#include "ipp_printer.h"
#include "ipp_protocol.h"
#include "label_printer_settings.h"
#include "label_printer_utils.h"
#include "sheet_label_layout.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "../catch_amalgamated.hpp"

using namespace helix;

namespace {

constexpr const char* kContinue = "HTTP/1.1 100 Continue\r\n\r\n";

/// A minimal IPP response body: version 2.0, successful-ok, request-id 1, end tag.
std::string ipp_ok_body() {
    const unsigned char bytes[] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03};
    return std::string(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

std::string final_ok_response() {
    const auto body = ipp_ok_body();
    return "HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + body;
}

/// One-connection HTTP server on 127.0.0.1 that reads the whole request and then
/// writes each scripted chunk, pausing between chunks so each lands in its own recv.
class ScriptedServer {
  public:
    explicit ScriptedServer(std::vector<std::string> chunks, int gap_ms = 150)
        : chunks_(std::move(chunks)), gap_ms_(gap_ms) {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(listen_fd_, 1);
        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { serve(); });
    }

    ~ScriptedServer() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        stop_cv_.notify_all();
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        if (thread_.joinable())
            thread_.join();
    }

    uint16_t port() const {
        return port_;
    }
    size_t body_bytes_received() const {
        return body_bytes_.load();
    }
    /// The request line, e.g. "POST /ipp/print HTTP/1.1"; empty until a request arrived.
    std::string request_line() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return request_line_;
    }
    /// The request body as received; empty until a request arrived.
    std::string body() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return body_;
    }

  private:
    void serve() {
        int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0)
            return;
        timeval tv{5, 0};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        std::string request;
        char buf[4096];
        size_t header_end = std::string::npos;
        size_t content_length = 0;
        while (true) {
            ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            request.append(buf, static_cast<size_t>(n));
            if (header_end == std::string::npos) {
                header_end = request.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    auto pos = request.find("Content-Length: ");
                    if (pos != std::string::npos && pos < header_end)
                        content_length = std::stoul(request.substr(pos + 16));
                }
            }
            if (header_end != std::string::npos &&
                request.size() >= header_end + 4 + content_length)
                break;
        }
        if (header_end != std::string::npos)
            body_bytes_ = request.size() - header_end - 4;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            request_line_ = request.substr(0, request.find("\r\n"));
            if (header_end != std::string::npos)
                body_ = request.substr(header_end + 4);
        }

        // The rest of a script the client gave up on is dropped once the test ends.
        for (size_t i = 0; i < chunks_.size(); ++i) {
            if (i > 0) {
                std::unique_lock<std::mutex> lock(mutex_);
                if (stop_cv_.wait_for(lock, std::chrono::milliseconds(gap_ms_),
                                      [this] { return stopping_; }))
                    break;
            }
            ::send(fd, chunks_[i].data(), chunks_[i].size(), MSG_NOSIGNAL);
        }
        // Keep the connection open until the client hangs up, as a printer would.
        while (::recv(fd, buf, sizeof(buf), 0) > 0) {
        }
        ::close(fd);
    }

    std::vector<std::string> chunks_;
    int gap_ms_;
    mutable std::mutex mutex_;
    std::condition_variable stop_cv_;
    bool stopping_ = false;
    std::string request_line_;
    std::string body_;
    int listen_fd_ = -1;
    uint16_t port_ = 0;
    std::atomic<size_t> body_bytes_{0};
    std::thread thread_;
};

std::vector<uint8_t> job_body() {
    return std::vector<uint8_t>(20000, 0x5A);
}

void require_ipp_ok(const std::shared_ptr<HttpResponse>& resp) {
    REQUIRE(resp != nullptr);
    REQUIRE(static_cast<int>(resp->status_code) == 200);
    auto ipp_resp =
        ipp::parse_response(reinterpret_cast<const uint8_t*>(resp->body.data()), resp->body.size());
    REQUIRE(ipp_resp.is_success());
}

} // namespace

TEST_CASE("IPP POST reads past an interim 100 Continue to the final response",
          "[label][ipp][ipp_http]") {
    ScriptedServer server({kContinue, final_ok_response()});
    auto body = job_body();

    auto resp = ipp::post_request("127.0.0.1", server.port(), "ipp/print", body, 10);

    require_ipp_ok(resp);
    REQUIRE(server.body_bytes_received() == body.size());
}

TEST_CASE("IPP POST handles 100 Continue and the final response in one write",
          "[label][ipp][ipp_http]") {
    ScriptedServer server({std::string(kContinue) + final_ok_response()});

    auto resp = ipp::post_request("127.0.0.1", server.port(), "ipp/print", job_body(), 10);

    require_ipp_ok(resp);
}

TEST_CASE("IPP POST reads past several interim responses", "[label][ipp][ipp_http]") {
    ScriptedServer server({kContinue, "HTTP/1.1 102 Processing\r\n\r\n", final_ok_response()});

    auto resp = ipp::post_request("127.0.0.1", server.port(), "ipp/print", job_body(), 10);

    require_ipp_ok(resp);
}

TEST_CASE("IPP POST returns a final response that has no interim", "[label][ipp][ipp_http]") {
    ScriptedServer server({final_ok_response()});

    auto resp = ipp::post_request("127.0.0.1", server.port(), "ipp/print", job_body(), 10);

    require_ipp_ok(resp);
}

TEST_CASE("IPP POST returns nullptr when only an interim response arrives",
          "[label][ipp][ipp_http]") {
    ScriptedServer server({kContinue});

    auto resp = ipp::post_request("127.0.0.1", server.port(), "ipp/print", job_body(), 1);

    REQUIRE(resp == nullptr);
}

TEST_CASE("IPP POST gives up at its deadline while interim responses keep coming",
          "[label][ipp][ipp_http]") {
    // Each 100 lands well inside the per-read timeout, so only an overall deadline
    // stops the wait.
    ScriptedServer server(std::vector<std::string>(12, kContinue), 400);

    const auto start = std::chrono::steady_clock::now();
    auto resp = ipp::post_request("127.0.0.1", server.port(), "ipp/print", job_body(), 1);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    REQUIRE(resp == nullptr);
    REQUIRE(elapsed < std::chrono::milliseconds(2500));
}

namespace {

/// Points the label-printer settings at a loopback IPP server for one test.
struct IppSettingsScope {
    explicit IppSettingsScope(uint16_t port, const std::string& rp) {
        Config::get_instance();
        auto& s = LabelPrinterSettingsManager::instance();
        s.init_subjects();
        s.set_printer_type("network");
        s.set_printer_protocol("ipp");
        s.set_printer_address("127.0.0.1");
        s.set_printer_port(port);
        s.set_ipp_resource_path(rp);
        s.set_label_size_index(0);
    }
    ~IppSettingsScope() {
        auto& s = LabelPrinterSettingsManager::instance();
        s.set_printer_address("");
        s.set_ipp_resource_path("");
        s.set_printer_port(9100);
        s.set_printer_protocol("raw");
        s.deinit_subjects();
    }
};

SpoolInfo test_spool() {
    SpoolInfo spool;
    spool.id = 42;
    spool.vendor = "Hatchbox";
    spool.material = "PLA";
    spool.color_hex = "#1A1A2E";
    return spool;
}

class OkableIppPrintModal : public IppPrintModal {
  public:
    using IppPrintModal::on_ok;
};

} // namespace

TEST_CASE_METHOD(LVGLTestFixture, "print_spool_label posts to the configured IPP resource path",
                 "[label][ipp_http][ipp_rp]") {
    ScriptedServer server({final_ok_response()});
    IppSettingsScope scope(server.port(), "/printers/Canon_TS8350");

    bool done = false;
    bool ok = false;
    print_spool_label(test_spool(), [&](bool success, const std::string&) {
        ok = success;
        done = true;
    });

    REQUIRE(wait_until([&] { return done; }, 15000));
    REQUIRE(server.request_line() == "POST /printers/Canon_TS8350 HTTP/1.1");
    REQUIRE(ok);
}

TEST_CASE_METHOD(LVGLUITestFixture, "The IPP print modal posts to the configured resource path",
                 "[label][ipp_http][ipp_rp]") {
    ScriptedServer server({final_ok_response()});
    IppSettingsScope scope(server.port(), "printers/Canon_TS8350");

    bool done = false;
    OkableIppPrintModal modal;
    modal.show_for_spool(test_spool(), [&](bool, const std::string&) { done = true; });
    modal.on_ok();

    REQUIRE(wait_until([&] { return done; }, 15000));
    REQUIRE(server.request_line() == "POST /printers/Canon_TS8350 HTTP/1.1");
}

// The PWG page header carries the paper by name at PageSizeName (PWG 5102.4
// offset 1732, counted from the end of the "RaS2" sync word). A printer sizes
// the page from it, so it must match the paper the template is laid out on.
TEST_CASE_METHOD(LVGLTestFixture, "The IPP job's PWG header names the template's paper",
                 "[label][ipp_http][pwg]") {
    const auto paper = GENERATE(label::PaperSize::LETTER, label::PaperSize::A4);
    const std::string keyword = label::get_pwg_media_keyword(paper);
    CAPTURE(keyword);
    const auto& templates = label::get_sheet_templates();
    const auto on_paper = label::get_templates_for_paper(paper);
    REQUIRE_FALSE(on_paper.empty());
    const int index = static_cast<int>(on_paper.front() - templates.data());

    ScriptedServer server({final_ok_response()});
    IppPrinter printer;
    printer.set_target("127.0.0.1", server.port(), "ipp/print");
    printer.set_sheet_template(index);

    bool done = false;
    bool ok = false;
    printer.print(LabelBitmap(64, 32), label::sheet_template_to_label_size(templates[index], 300),
                  [&](bool success, const std::string&) {
                      ok = success;
                      done = true;
                  });
    REQUIRE(wait_until([&] { return done; }, 15000));
    REQUIRE(ok);

    const std::string body = server.body();
    const size_t sync = body.find("RaS2");
    REQUIRE(sync != std::string::npos);
    const size_t field = sync + 4 + 1732;
    REQUIRE(body.size() > field + 64);
    CHECK(std::string(body.c_str() + field) == keyword);
}
