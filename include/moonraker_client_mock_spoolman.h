// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "moonraker_error.h"
#include "spoolman_types.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "hv/json.hpp"

/**
 * @brief The Spoolman server behind MoonrakerClientMock's server.spoolman.*
 *
 * Holds a spool inventory and answers server.spoolman.proxy requests with the
 * JSON Spoolman itself returns, so the real MoonrakerSpoolmanAPI and its
 * parsers run in --test and in unit tests. The inventory is kept as SpoolInfo
 * records and serialized to Spoolman's schema on every request; vendors and
 * filaments are synthesized from the spools, ahead of which come any seeded
 * with add_vendor()/add_filament() or created through POST.
 */
class MockSpoolmanServer {
  public:
    using json = nlohmann::json;

    MockSpoolmanServer();

    /// Answers one server.spoolman.proxy request ({request_method, path, body}).
    /// Returns false with @p err set when Spoolman would answer an error.
    bool proxy(const json& params, json& result, MoonrakerError& err);

    /// server.spoolman.post_spool_id
    void set_active_spool_id(int spool_id) {
        active_spool_id_ = spool_id;
    }
    /// The spool server.spoolman.status reports as active, or 0 for none.
    [[nodiscard]] int get_mock_active_spool_id() const {
        return active_spool_id_;
    }

    /// Whether /v1/external/filament/search exists, as on Spoolman 0.26.0 and
    /// later. HELIX_MOCK_SPOOLMAN_DB_SEARCH=0 starts it off, as an older server.
    void set_external_search_supported(bool supported) {
        external_search_supported_ = supported;
    }
    /// Delay before a SpoolmanDB search answers, delivered from a worker thread
    /// the way a real response arrives; 0 answers at once on the caller.
    /// HELIX_MOCK_SPOOLMAN_DB_SEARCH_LATENCY_MS sets it at start.
    void set_external_search_latency_ms(int ms) {
        external_search_latency_ms_ = ms;
    }
    [[nodiscard]] int external_search_latency_ms() const {
        return external_search_latency_ms_;
    }
    /// SpoolmanDB search requests answered so far.
    [[nodiscard]] int external_search_count() const {
        return external_search_count_;
    }

    /// The inventory, for tests to inspect or reshape.
    std::vector<SpoolInfo>& get_mock_spools() {
        return spools_;
    }

    /// Seeds a vendor served ahead of the ones synthesized from spools.
    void add_vendor(int id, std::string name);
    /// Seeds a filament served ahead of the ones synthesized from spools; the
    /// vendor-filtered GET returns it only for its own vendor.
    void add_filament(int id, int vendor_id, std::string material, std::string color_hex,
                      std::string name = "");

    // Test inspection: request bodies as they arrived on the wire.
    struct FilamentUpdateRecord {
        int filament_id = 0;
        json data;
    };
    /// Every PATCH /v1/filament/{id}
    std::vector<FilamentUpdateRecord> filament_updates;

    struct SpoolUpdateRecord {
        int spool_id = 0;
        json patch;
    };
    /// PATCH /v1/spool/{id} bodies, except weight-only ones
    std::vector<SpoolUpdateRecord> spool_updates;

    struct WeightUpdateRecord {
        int spool_id = 0;
        double remaining_weight_g = 0.0;
    };
    /// PATCH /v1/spool/{id} bodies carrying nothing but remaining_weight, the
    /// shape update_spoolman_spool_weight() sends
    std::vector<WeightUpdateRecord> weight_updates;

    std::vector<json> created_vendors;   ///< POST /v1/vendor bodies
    std::vector<json> created_filaments; ///< POST /v1/filament bodies
    std::vector<json> created_spools;    ///< POST /v1/spool bodies

    /// IDs the next POST of each kind is given; 0 auto-assigns.
    int next_created_vendor_id = 0;
    int next_created_filament_id = 0;
    int next_created_spool_id = 0;

  private:
    std::vector<SpoolInfo> spools_;
    std::vector<VendorInfo> vendors_;
    std::map<int, std::string> vendor_comments_;
    std::vector<FilamentInfo> filaments_;
    /// Spools PATCHed to archived=true stay in spools_ (the single-spool GET
    /// still serves them) but are filtered from list GETs, as Spoolman does.
    std::set<int> archived_spool_ids_;
    int active_spool_id_ = 1;
    bool external_search_supported_ = true;
    int external_search_latency_ms_ = 0;
    int external_search_count_ = 0;
    int next_filament_id_ = 300;

    void init_mock_spools();
    [[nodiscard]] std::vector<VendorInfo> vendor_list() const;
    [[nodiscard]] std::vector<FilamentInfo> filament_list() const;
    [[nodiscard]] json spool_json(const SpoolInfo& spool) const;
    SpoolInfo* find_spool(int id);
};
