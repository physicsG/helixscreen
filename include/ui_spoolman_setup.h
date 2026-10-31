// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace helix::ui {

static constexpr const char* kDefaultSpoolmanPort = "7912";

class SpoolmanSetup {
  public:
    static bool validate_port(const std::string& port);
    static bool validate_host(const std::string& host);
    static std::string build_url(const std::string& host, const std::string& port);
    static std::string build_probe_url(const std::string& host, const std::string& port);
    static std::vector<std::pair<std::string, std::string>>
    build_spoolman_config_entries(const std::string& host, const std::string& port);
    static std::pair<std::string, std::string> parse_url_components(const std::string& url);
};

} // namespace helix::ui
