// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "json_utils.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "hv/json.hpp"

namespace helix::sensors {

/**
 * @brief One kind of discovered sensor: configs in discovery order, plus runtime
 *        state keyed by klipper_name.
 *
 * Holds no lock. Every call happens under the owning manager's mutex, which
 * also guards whatever the manager reads alongside it.
 *
 * Config needs `klipper_name` and `role`; the role/enabled queries also need
 * `enabled`. State needs `available`. Every Role enum has a NONE.
 */
template <typename Config, typename State> class SensorCollection {
  public:
    using iterator = typename std::vector<Config>::iterator;
    using const_iterator = typename std::vector<Config>::const_iterator;

    iterator begin() {
        return configs_.begin();
    }
    iterator end() {
        return configs_.end();
    }
    const_iterator begin() const {
        return configs_.begin();
    }
    const_iterator end() const {
        return configs_.end();
    }
    [[nodiscard]] bool empty() const {
        return configs_.empty();
    }
    [[nodiscard]] size_t size() const {
        return configs_.size();
    }
    Config& front() {
        return configs_.front();
    }
    Config& operator[](size_t i) {
        return configs_[i];
    }
    [[nodiscard]] const std::vector<Config>& configs() const {
        return configs_;
    }

    /**
     * @brief Replace the configs with a fresh discovery.
     *
     * Each discovered sensor ends with an available state, keeping the values
     * of one already known. A state with no discovered sensor is dropped, or,
     * with keep_missing, kept and marked unavailable.
     */
    void reconcile(std::vector<Config> discovered, bool keep_missing = false) {
        configs_ = std::move(discovered);
        for (const auto& config : configs_) {
            states_[config.klipper_name].available = true;
        }
        for (auto it = states_.begin(); it != states_.end();) {
            if (find(it->first)) {
                ++it;
            } else if (keep_missing) {
                it->second.available = false;
                ++it;
            } else {
                it = states_.erase(it);
            }
        }
    }

    void clear() {
        configs_.clear();
        states_.clear();
    }

    Config* find(const std::string& klipper_name) {
        for (auto& config : configs_) {
            if (config.klipper_name == klipper_name) {
                return &config;
            }
        }
        return nullptr;
    }

    const Config* find(const std::string& klipper_name) const {
        return const_cast<SensorCollection*>(this)->find(klipper_name);
    }

    /// First sensor holding `role`.
    template <typename Role> const Config* find_by_role(Role role) const {
        for (const auto& config : configs_) {
            if (config.role == role) {
                return &config;
            }
        }
        return nullptr;
    }

    State* state(const std::string& klipper_name) {
        auto it = states_.find(klipper_name);
        return it == states_.end() ? nullptr : &it->second;
    }

    const State* state(const std::string& klipper_name) const {
        return const_cast<SensorCollection*>(this)->state(klipper_name);
    }

    /// State for `klipper_name`, default-constructed if absent.
    State& state_at(const std::string& klipper_name) {
        return states_[klipper_name];
    }

    /// State of the first `role` holder, whatever its enabled/available flags. NONE has none.
    template <typename Role> const State* role_state(Role role) const {
        if (role == Role::NONE) {
            return nullptr;
        }
        const auto* config = find_by_role(role);
        return config ? state(config->klipper_name) : nullptr;
    }

    /// State of the first `role` holder when it is enabled and available: the
    /// reading a role-driven subject or getter shows.
    template <typename Role> const State* live_state(Role role) const {
        if (role == Role::NONE) {
            return nullptr;
        }
        const auto* config = find_by_role(role);
        if (!config || !config->enabled) {
            return nullptr;
        }
        const auto* s = state(config->klipper_name);
        return s && s->available ? s : nullptr;
    }

    /// Give `role` to `klipper_name`, taking it from every other holder. NONE
    /// is never exclusive. Returns the sensor, or nullptr if it is unknown.
    template <typename Role>
    Config* assign_exclusive_role(const std::string& klipper_name, Role role) {
        if (role != Role::NONE) {
            for (auto& config : configs_) {
                if (config.role == role && config.klipper_name != klipper_name) {
                    config.role = Role::NONE;
                }
            }
        }
        auto* config = find(klipper_name);
        if (config) {
            config->role = role;
        }
        return config;
    }

    /**
     * @brief The persisted form: one {klipper_name, role, enabled, type} entry per sensor.
     */
    template <typename RoleName, typename TypeName>
    [[nodiscard]] nlohmann::json to_json(RoleName role_name, TypeName type_name) const {
        nlohmann::json sensors = nlohmann::json::array();
        for (const auto& config : configs_) {
            nlohmann::json entry;
            entry["klipper_name"] = config.klipper_name;
            entry["role"] = role_name(config.role);
            entry["enabled"] = config.enabled;
            entry["type"] = type_name(config.type);
            sensors.push_back(entry);
        }
        nlohmann::json out;
        out["sensors"] = sensors;
        return out;
    }

    /**
     * @brief Apply saved role/enabled entries to the discovered sensors.
     *
     * Entries for sensors not discovered are skipped. A non-boolean "enabled"
     * leaves that setting as it is; a non-string "role" becomes NONE. Returns
     * whether a "sensors" array was present.
     */
    template <typename RoleFromName>
    bool apply_json(const nlohmann::json& saved, RoleFromName role_from_name) {
        const auto sensors = saved.find("sensors");
        if (sensors == saved.end() || !sensors->is_array()) {
            return false;
        }
        for (const auto& entry : *sensors) {
            const auto name = entry.find("klipper_name");
            if (name == entry.end()) {
                continue;
            }
            auto* config = find(json_util::as_string(*name));
            if (!config) {
                continue;
            }
            if (const auto role = entry.find("role"); role != entry.end()) {
                config->role = role_from_name(json_util::as_string(*role));
            }
            if (const auto enabled = entry.find("enabled"); enabled != entry.end()) {
                config->enabled = json_util::as_bool(*enabled, config->enabled);
            }
        }
        return true;
    }

  private:
    std::vector<Config> configs_;
    std::map<std::string, State> states_;
};

} // namespace helix::sensors
