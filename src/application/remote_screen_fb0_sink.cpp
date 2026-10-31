// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "remote_screen_fb0_sink.h"

#include "remote_screen_blit.h"

#include <spdlog/spdlog.h>

#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>

// The fbdev ioctl geometry probe is Linux-only. Everything else (mmap blit,
// RGB565->BGRA conversion) is portable POSIX, so non-Linux native/dev builds
// (macOS CI) compile the sink and exercise the conversion via configured
// geometry — only the real-device probe is compiled out.
#ifdef __linux__
#include <linux/fb.h>
#include <sys/ioctl.h>
#endif

namespace helix {

Fb0MailboxSink::Fb0MailboxSink(std::string dev) : dev_(std::move(dev)) {}

Fb0MailboxSink::~Fb0MailboxSink() {
    stop();
}

void Fb0MailboxSink::configure_geometry(int w, int h, uint32_t stride, int bpp) {
    has_cfg_ = true;
    cfg_w_ = w;
    cfg_h_ = h;
    cfg_stride_ = stride;
    cfg_bpp_ = bpp;
}

void Fb0MailboxSink::warn_once(const char* what) {
    if (!warned_) {
        warned_ = true;
        spdlog::warn("[RemoteScreen] fb0 sink disabled ({}): dev={}", what, dev_);
    }
}

bool Fb0MailboxSink::start() {
    if (active_) {
        return true;
    }

    fd_ = ::open(dev_.c_str(), O_RDWR);
    if (fd_ < 0) {
        warn_once("open failed");
        return false;
    }

    bool have_ioctl = false;

#ifdef __linux__
    // A real fbdev answers these ioctls; a regular file (test backing) does not.
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    std::memset(&vinfo, 0, sizeof(vinfo));
    std::memset(&finfo, 0, sizeof(finfo));

    have_ioctl = ::ioctl(fd_, FBIOGET_VSCREENINFO, &vinfo) == 0 &&
                 ::ioctl(fd_, FBIOGET_FSCREENINFO, &finfo) == 0;

    if (have_ioctl) {
        fb_w_ = static_cast<int>(vinfo.xres);
        fb_h_ = static_cast<int>(vinfo.yres);
        fb_bpp_ = static_cast<int>(vinfo.bits_per_pixel);
        fb_stride_ = static_cast<uint32_t>(finfo.line_length);
    }
#endif

    if (!have_ioctl) {
        if (has_cfg_) {
            // Test path (and all non-Linux builds): fall back to configured
            // geometry so the blit math is exercisable without a device.
            fb_w_ = cfg_w_;
            fb_h_ = cfg_h_;
            fb_bpp_ = cfg_bpp_;
            fb_stride_ = cfg_stride_;
        } else {
            warn_once("no fbdev ioctls and no configured geometry");
            ::close(fd_);
            fd_ = -1;
            return false;
        }
    }

    if (fb_bpp_ != 16 && fb_bpp_ != 32) {
        warn_once("unsupported bpp (need 16 or 32)");
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    if (fb_w_ <= 0 || fb_h_ <= 0 || fb_stride_ == 0) {
        warn_once("invalid geometry");
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    map_size_ = static_cast<size_t>(fb_stride_) * static_cast<size_t>(fb_h_);
    void* m = ::mmap(nullptr, map_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (m == MAP_FAILED) {
        warn_once("mmap failed");
        map_size_ = 0;
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    map_ = static_cast<uint8_t*>(m);
    active_ = true;
    spdlog::info("[RemoteScreen] fb0 sink active: dev={} {}x{} stride={} bpp={}", dev_, fb_w_,
                 fb_h_, fb_stride_, fb_bpp_);
    return true;
}

void Fb0MailboxSink::stop() {
    if (map_) {
        ::munmap(map_, map_size_);
        map_ = nullptr;
        map_size_ = 0;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    active_ = false;
}

bool Fb0MailboxSink::wants_frames() const {
    return active_;
}

void Fb0MailboxSink::on_frame(const RemoteScreenFrame& f) {
    if (!active_ || map_ == nullptr || f.px_map == nullptr) {
        return;
    }

    // One-shot diagnostics: log exactly what the flush hands us on the first few
    // frames so the on-device layout (area, real stride, format) is observable
    // without a debugger.
    if (log_count_ < 3) {
        ++log_count_;
        spdlog::info("[RemoteScreen] on_frame ENTER #{}: area=({},{})-({},{}) "
                     "disp={}x{} src_stride={} src_bpp={} cf={} fb={}x{} fb_bpp={} "
                     "fb_stride={} map_size={}",
                     log_count_, f.x1, f.y1, f.x2, f.y2, f.disp_w, f.disp_h, f.src_stride,
                     remote_frame_src_bpp(f.src_format), f.color_format, fb_w_, fb_h_, fb_bpp_,
                     fb_stride_, static_cast<unsigned long>(map_size_));
    }

    const BlitResult r =
        blit_remote_frame(f, BlitTarget{map_, map_size_, fb_stride_, fb_w_, fb_h_, fb_bpp_});
    switch (r.status) {
    case BlitStatus::BeforeOrigin:
    case BlitStatus::SourceOutOfBounds:
    case BlitStatus::TargetOutOfBounds:
        if (!oob_warned_) {
            oob_warned_ = true;
            spdlog::warn("[RemoteScreen] frame out of bounds (status={} rect=({},{})-({},{}) "
                         "px_map_origin=({},{}) src_stride={} px_map_len={}) - skipping mirror",
                         static_cast<int>(r.status), f.x1, f.y1, f.x2, f.y2, f.px_map_x, f.px_map_y,
                         f.src_stride, static_cast<unsigned long>(f.px_map_len));
        }
        return;
    case BlitStatus::Copied:
        break;
    default:
        return;
    }

    if (log_count_ <= 3 && log_done_ < 3) {
        ++log_done_;
        spdlog::info("[RemoteScreen] on_frame DONE #{} (copied {}x{} at ({},{}))", log_done_, r.w,
                     r.h, r.x, r.y);
    }
}

const char* Fb0MailboxSink::name() const {
    return "Fb0MailboxSink"; // i18n: do not translate - internal sink identifier
}

} // namespace helix
