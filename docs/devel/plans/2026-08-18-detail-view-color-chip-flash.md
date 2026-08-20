# Print-Detail Color-Chip Flash Fix

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the open-time "flash" of the print-details filament chips (mapping-card pills and swatch-card chips) — gray/default dots popping to real colors, pill counts changing, warning icons appearing — by making the authoritative state available instantly (cache), computing it faster (single download + early-exit scan), and never showing a guessed intermediate state (skeleton latch).

**Architecture:** Three composing mechanisms. (1) A per-file `ToolsUsedCache` (JSON in the helix cache dir, keyed by path+size+mtime) seeds the used-tool set at `show()` so reprints render the final chip state in one paint. (2) One shared gcode download (`ensure_gcode_downloaded()`) feeds both the headless tools scan and the viewer preview, and the scan early-exits once every palette tool has been seen. (3) A new `detail_mapping_ready` subject — the same readiness concept `is_preflight_ready()` already models for the print-start gate — drives XML skeleton placeholders inside both cards until the authoritative render exists. An idempotent-render fingerprint in `FilamentMappingCard` suppresses any remaining invisible-input rebuilds (AMS resync color arrivals).

**Tech Stack:** LVGL 9.5 subjects + XML bindings, nlohmann JSON via `hv/json.hpp`, Catch2 (`tests/unit/`), spdlog.

## Problem (root cause, verified 2026-08-18)

Opening the print-details overlay triggers **three destroy-and-recreate rebuilds** of the chip row, each rendering different data:

| t | Event | Code path | User sees |
|---|-------|-----------|-----------|
| 0 | `show()` → `FilamentMappingCard::update()` | `ui_print_select_detail_view.cpp:400` | Full slicer-palette pills; slot dots gray (`filament_mapping_pill.xml:35` default, or `0x808080` at `ui_filament_mapping_card.cpp:244`) while mapping is `is_auto`/slot state unsynced |
| +1–3 s | Headless tools scan completes | `:1384` (`kick_off_headless_tools_scan` finish) | Rebuild; preflight runs → mismatch warning icon pops in (`:1390`) |
| +2–8 s | Viewer parse → `set_used_tools()` | `:1066` (`try_extract_gcode_colors`) | Chips for unused tools vanish, card reflows (repro'd: 4 pills → 2 for a T0+T2 file) |
| any | AMS `slots_version` bump | `:1000` (`on_ams_state_changed`), guarded on `gcode_loaded_` | Slot colors arriving during parse are dropped, land late → gray→real flash. `request_resync()` in `on_activate()` (`:506`) guarantees a bump right after every open |

Aggravators:
- The gcode file is downloaded **twice** — headless scan (`tools_scan_<hash>.gcode`, `:1339`) and viewer preview (`detail_preview_<hash>.gcode`, `:1498`) use different cache paths.
- The thumbnail→3D/2D transition already solved this class of problem with the `detail_viewer_first_frame_` latch (`:1462`); the chips have no equivalent.
- Mock never shows it because local-disk download+parse lands all phases in one frame (~20 ms). On hardware the gray window is seconds.

**Verified by log capture** (mock, `HELIX_MOCK_AMS=afc`, file `u1_4color_ring.gcode`): three `[FilamentMapping] Updated` lines + two `Preflight` lines within one open; subset repro file showed 4 pills → 2 pills + late warning icon.

## DRY rule for this plan

The viewer's first-frame latch stays viewer-specific (it keys on a render callback). The **chips reuse the existing preflight-readiness concept** (`is_preflight_ready()` / `run_when_preflight_ready()`, `include/ui_print_select_detail_view.h:235`) — the new subject is a publish of that same state, not a parallel mechanism. Do not invent a second readiness flag.

## Global Constraints

- Data in C++, appearance in XML, subjects connect them (AGENTS.md). New skeleton UI lives in `print_file_detail.xml` bound to subjects; no imperative `lv_obj_add_flag(HIDDEN)` for show/hide.
- `scripts/check_imperative_ui.py --list` count may fall, never rise.
- JSON: `#include "hv/json.hpp"`, never `<nlohmann/json.hpp>`.
- spdlog only; SPDX header `// SPDX-License-Identifier: GPL-3.0-or-later`; class-based code; `lvgl_make_unique` for heap widgets (none needed here — all children go through `lv_xml_create`).
- Never touch LVGL from the HTTP thread — all `download_file_to_path` callbacks marshal through `tok.defer` (existing pattern at `ui_print_select_detail_view.cpp:1602`).
- Background-thread scans hold no `this` — the scan computes a local `std::set<int>` and marshals via `tok.defer` (existing pattern at `:1344`).
- Tests run from repo root: `./build/bin/helix-tests "[tag]"` after `make test`. `make -j` builds only the app binary.

---

### Task 1: Early-exit tools scan

**Files:**
- Modify: `include/gcode_parser.h` (~line 833, `scan_tools_used_from_file` decl; also `scan_tools_used_from_content` decl nearby)
- Modify: `src/rendering/gcode_parser.cpp:2040-2119` (both scan functions)
- Test: `tests/unit/test_gcode_tools_used_scan.cpp`

**Interfaces:**
- Produces (consumed by Task 4):
  ```cpp
  namespace helix::gcode {
  // Returns the distinct tool indices used in the file. When early_exit_full_set
  // is non-empty, scanning stops as soon as `seen ⊇ early_exit_full_set`.
  // Soundness: callers pass the full slicer palette {0..N-1}; tools at indices
  // >= palette size are already dropped by every downstream consumer
  // (get_used_tool_info() filters to palette indices), so a missed
  // beyond-palette tool is invisible. Empty set => scan whole file (back-compat).
  std::set<int> scan_tools_used_from_file(const std::string& filepath,
                                          std::set<int> early_exit_full_set = {});
  std::set<int> scan_tools_used_from_content(const std::string& content,
                                             std::set<int> early_exit_full_set = {});
  }
  ```

- [ ] **Step 1: Write failing tests**

Add to `tests/unit/test_gcode_tools_used_scan.cpp`:

```cpp
TEST_CASE("scan_tools_used - early exit", "[gcode][tools_used]") {
    // Body where all palette tools appear near the head, then a huge tail.
    std::string g = "T0\nG1 X1 E1\nT1\nG1 X2 E1\nT0\n";
    for (int i = 0; i < 50000; ++i) {
        g += "G1 X" + std::to_string(i % 50) + " E0.05\n";
    }
    const std::set<int> full{0, 1};

    SECTION("Early exit returns the same set as a full scan") {
        REQUIRE(scan_tools_used_from_content(g, full) == full);
    }
    SECTION("Subset file: stop set never completed, full scan still correct") {
        // File only ever uses T0; stop set {0,1} is unreachable => whole body
        // is read and the result is still exactly {0}.
        const std::set<int> expect{0};
        REQUIRE(scan_tools_used_from_content(g.substr(0, g.find("T1")), {0, 1}) == expect);
    }
    SECTION("Empty stop set behaves exactly like today") {
        REQUIRE(scan_tools_used_from_content(g, {}) == full);
        REQUIRE(scan_tools_used_from_content(g) == full);
    }
    SECTION("Early exit finds a late tool that IS in the stop set") {
        // T1 appears only deep in the body — must not be missed just because
        // T0 was seen immediately.
        std::string late = "T0\n";
        for (int i = 0; i < 20000; ++i) late += "G1 X1 E0.1\n";
        late += "T1\n";
        const std::set<int> expect{0, 1};
        REQUIRE(scan_tools_used_from_content(late, {0, 1}) == expect);
    }
}
```

- [ ] **Step 2: Run to verify failure**

Run: `./build/bin/helix-tests "[gcode][tools_used]"` (after `make test`)
Expected: compile error — no overload taking two args.

- [ ] **Step 3: Implement**

In `src/rendering/gcode_parser.cpp`, thread the stop set through both functions. Content scanner (the `tool_index_for_line` loop at ~:2085 and the getline loop at :2103):

```cpp
namespace {
// True when `seen` covers every index in `stop_set` (early-exit condition).
bool seen_all(const std::set<int>& seen, const std::set<int>& stop_set) {
    if (stop_set.empty()) return false;
    for (int t : stop_set) {
        if (seen.count(t) == 0) return false;
    }
    return true;
}
} // namespace

std::set<int> scan_tools_used_from_content(const std::string& content,
                                           std::set<int> early_exit_full_set) {
    std::set<int> tools;
    std::string line;
    for (char ch : content) {
        if (ch == '\n') {
            int t = tool_index_for_line(line);
            if (t >= 0) {
                tools.insert(t);
                if (seen_all(tools, early_exit_full_set)) return tools;
            }
            line.clear();
        } else {
            line.push_back(ch);
        }
    }
    if (!line.empty()) {
        int t = tool_index_for_line(line);
        if (t >= 0) tools.insert(t);
    }
    return tools;
}

std::set<int> scan_tools_used_from_file(const std::string& filepath,
                                        std::set<int> early_exit_full_set) {
    std::set<int> tools;
    std::ifstream in(filepath, std::ios::binary);
    if (!in.is_open()) {
        return tools;
    }
    std::string line;
    while (std::getline(in, line)) {
        int t = tool_index_for_line(line);
        if (t >= 0) {
            tools.insert(t);
            if (seen_all(tools, early_exit_full_set)) break;
        }
    }
    return tools;
}
```

Update both declarations in `include/gcode_parser.h` with the doc comment from the Interfaces block. If `scan_tools_used_from_content` is declared in the header without a definition in a `.cpp` (check — it may be header-visible but defined in the .cpp), keep declaration/definition in sync.

- [ ] **Step 4: Run tests**

Run: `./build/bin/helix-tests "[gcode][tools_used]"`
Expected: all PASS (new + pre-existing scanner tests).

- [ ] **Step 5: Commit**

```bash
git add include/gcode_parser.h src/rendering/gcode_parser.cpp tests/unit/test_gcode_tools_used_scan.cpp
git commit -m "feat(gcode): early-exit tools-used scan when the palette is covered"
```

---

### Task 2: ToolsUsedCache

**Files:**
- Create: `include/tools_used_cache.h`
- Create: `src/system/tools_used_cache.cpp`
- Modify: `Makefile` source list ONLY if `src/system/` is not globbed (check `Makefile` `SRC_DIRS` / `SYSTEM_SRCS`; recent files like `src/system/prerendered_images.cpp` compile without Makefile edits — verify with `make -j` after creating)
- Test: `tests/unit/test_tools_used_cache.cpp` (new; add to test build the same way other unit tests are — check `tests/unit/Makefile` or glob in `make test`)

**Interfaces:**
- Consumes: `get_helix_cache_dir()` (`include/app_globals.h:343`), `hv/json.hpp`
- Produces (consumed by Task 4):
  ```cpp
  namespace helix {

  /// Persistent per-file cache of the tools-used set recovered from a gcode
  /// scan/viewer parse. Keyed by (path, size, mtime) so re-sliced files
  /// invalidate naturally. JSON map in <cache>/tools_used/cache.json,
  /// LRU-bounded. Pure logic — no LVGL, safe on any thread (internally
  /// serialized with a mutex if a mutex already exists; single-threaded use
  /// from the detail view today, so keep it simple: document main-thread use).
  class ToolsUsedCache {
    public:
      ToolsUsedCache() = default;

      /// nullopt = miss / stale / malformed. An EMPTY set is a legitimate
      /// cached value (single-extruder file) and is returned as such.
      std::optional<std::set<int>> lookup(const std::string& file_path, uint64_t size_bytes,
                                          time_t modified);

      /// Persist an entry (also compacts when over MAX_ENTRIES — drop
      /// least-recently-LOOKED-UP first). Writes through to disk immediately;
      /// a failed write logs a warning and keeps the in-memory entry.
      void store(const std::string& file_path, uint64_t size_bytes, time_t modified,
                 const std::set<int>& tools);

      static constexpr size_t MAX_ENTRIES = 256;

      /// Test seam + cheap explicit reset. Not needed in production flow.
      void invalidate() { entries_.clear(); dirty_ = false; }

    private:
      struct Entry {
          uint64_t size_bytes = 0;
          time_t modified = 0;
          std::set<int> tools;
          uint64_t last_used_ctr = 0; // higher = more recent
      };
      bool load_from_disk();
      void save_to_disk();

      std::map<std::string, Entry> entries_;
      uint64_t next_ctr_ = 1;
      bool loaded_ = false;
  };

  } // namespace helix
  ```

Storage format (`<cache>/tools_used/cache.json`):

```json
{ "v": 1,
  "entries": {
    "subdir/My File.gcode": { "size": 391225, "mtime": 1764830000, "tools": [0, 2] }
  } }
```

Key = the Moonraker-relative gcode path (already unique per file). Validation on load: every entry must have numeric `size`, numeric `mtime`, array-of-non-negative-ints `tools`; drop malformed entries, keep the rest (never fail the whole cache for one bad entry).

- [ ] **Step 1: Write failing tests**

`tests/unit/test_tools_used_cache.cpp` (model on `tests/unit/test_gcode_tools_used_scan.cpp` — plain Catch2, no LVGL fixture):

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools_used_cache.h"

#include <cstdlib>
#include <filesystem>

#include "../catch_amalgamated.hpp"

namespace {
// Per-TEST_SECTION temp cache dir; HELIX_CACHE_DIR is the documented override
// for get_helix_cache_dir() (src/app_globals.cpp:424). MUST save/restore the
// previous value — leaking it would redirect every later test's cache writes
// in this binary (tests within one Catch2 binary run sequentially, but they
// share the process env).
struct CacheDirGuard {
    std::filesystem::path dir;
    std::string prev_env_;
    bool had_prev_ = false;
    CacheDirGuard() : dir(std::filesystem::temp_directory_path() /
                          ("tools_used_test_" + std::to_string(::getpid()))) {
        std::filesystem::create_directories(dir);
        if (const char* old = ::getenv("HELIX_CACHE_DIR")) {
            prev_env_ = old;
            had_prev_ = true;
        }
        setenv("HELIX_CACHE_DIR", dir.c_str(), 1);
    }
    ~CacheDirGuard() {
        if (had_prev_) {
            setenv("HELIX_CACHE_DIR", prev_env_.c_str(), 1);
        } else {
            unsetenv("HELIX_CACHE_DIR");
        }
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
} // namespace

TEST_CASE("ToolsUsedCache round trip + staleness", "[tools_used_cache]") {
    CacheDirGuard guard;
    helix::ToolsUsedCache a;

    SECTION("miss then hit") {
        REQUIRE(a.lookup("file.gcode", 100, 1000) == std::nullopt);
        const std::set<int> tools{0, 2};
        a.store("file.gcode", 100, 1000, tools);
        REQUIRE(a.lookup("file.gcode", 100, 1000).value_or({}) == tools);
    }
    SECTION("empty set is a hit, not a miss") {
        const std::set<int> empty;
        a.store("single.gcode", 50, 5, empty);
        auto got = a.lookup("single.gcode", 50, 5);
        REQUIRE(got.has_value());
        REQUIRE(got->empty());
    }
    SECTION("size or mtime change invalidates") {
        a.store("file.gcode", 100, 1000, {0});
        REQUIRE(a.lookup("file.gcode", 101, 1000) == std::nullopt);
        REQUIRE(a.lookup("file.gcode", 100, 1001) == std::nullopt);
    }
    SECTION("path difference is a different entry") {
        a.store("dir/a.gcode", 100, 1000, {1});
        REQUIRE(a.lookup("dir/b.gcode", 100, 1000) == std::nullopt);
    }
}

TEST_CASE("ToolsUsedCache persistence across instances", "[tools_used_cache]") {
    CacheDirGuard guard;
    helix::ToolsUsedCache a;
    const std::set<int> tools{0, 1, 3};
    a.store("persist.gcode", 10, 20, tools);
    helix::ToolsUsedCache b; // fresh instance reads the same disk file
    REQUIRE(b.lookup("persist.gcode", 10, 20).value_or({}) == tools);
}

TEST_CASE("ToolsUsedCache LRU bound", "[tools_used_cache]") {
    CacheDirGuard guard;
    helix::ToolsUsedCache a;
    for (size_t i = 0; i < helix::ToolsUsedCache::MAX_ENTRIES + 10; ++i) {
        a.store("f" + std::to_string(i) + ".gcode", i, i, {0});
    }
    // f0 was stored first and never looked up — evicted.
    REQUIRE(a.lookup("f0.gcode", 0, 0) == std::nullopt);
    // The most recent stores survive.
    const size_t last = helix::ToolsUsedCache::MAX_ENTRIES + 9;
    REQUIRE(a.lookup("f" + std::to_string(last) + ".gcode", last, last).has_value());
}
```

- [ ] **Step 2: Run to verify failure**

Run: `make test && ./build/bin/helix-tests "[tools_used_cache]"`
Expected: FAIL — `tools_used_cache.h: No such file`.

- [ ] **Step 3: Implement**

`include/tools_used_cache.h` exactly as the Interfaces block above (plus `#pragma once`, SPDX, includes `<cstdint> <map> <optional> <set> <string> <ctime>`).

`src/system/tools_used_cache.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools_used_cache.h"

#include "app_globals.h"
#include "hv/json.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>

namespace helix {

namespace {
std::string cache_file_path() {
    return get_helix_cache_dir("tools_used") + "/cache.json";
}
} // namespace

bool ToolsUsedCache::load_from_disk() {
    loaded_ = true; // never retry within this instance
    const std::string path = cache_file_path();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return false; // cold cache — normal on first run
    }
    std::string data;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        data.append(buf, n);
    }
    std::fclose(f);

    try {
        const auto j = nlohmann::json::parse(data);
        const auto it = j.find("entries");
        if (it == j.end() || !it->is_object()) {
            return false;
        }
        for (auto entry = it->begin(); entry != it->end(); ++entry) {
            const auto& e = entry.value();
            if (!e.is_object() || !e.contains("size") || !e.contains("mtime") ||
                !e["size"].is_number() || !e["mtime"].is_number() ||
                !e.contains("tools") || !e["tools"].is_array()) {
                continue; // drop malformed entry, keep the rest
            }
            Entry parsed;
            parsed.size_bytes = e["size"].get<uint64_t>();
            parsed.modified = static_cast<time_t>(e["mtime"].get<int64_t>());
            bool ok = true;
            for (const auto& t : e["tools"]) {
                if (!t.is_number() || t.get<int64_t>() < 0) {
                    ok = false;
                    break;
                }
                parsed.tools.insert(static_cast<int>(t.get<int64_t>()));
            }
            if (ok) {
                parsed.last_used_ctr = next_ctr_++;
                entries_[entry.key()] = std::move(parsed);
            }
        }
        return true;
    } catch (const std::exception& ex) {
        spdlog::warn("[ToolsUsedCache] Corrupt cache file, starting cold: {}", ex.what());
        entries_.clear();
        return false;
    }
}

void ToolsUsedCache::save_to_disk() {
    try {
        nlohmann::json j = {{"v", 1}, {"entries", nlohmann::json::object()}};
        for (const auto& [key, e] : entries_) {
            nlohmann::json tools = nlohmann::json::array();
            for (int t : e.tools) tools.push_back(t);
            j["entries"][key] = {{"size", e.size_bytes},
                                 {"mtime", static_cast<int64_t>(e.modified)},
                                 {"tools", std::move(tools)}};
        }
        const std::string path = cache_file_path();
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            spdlog::warn("[ToolsUsedCache] Cannot write {}", path);
            return;
        }
        const std::string out = j.dump();
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
    } catch (const std::exception& ex) {
        spdlog::warn("[ToolsUsedCache] Save failed: {}", ex.what());
    }
}

std::optional<std::set<int>> ToolsUsedCache::lookup(const std::string& file_path, uint64_t size_bytes,
                                                    time_t modified) {
    if (!loaded_ && !load_from_disk()) {
        // cold cache — loaded_ is now true, entries_ empty
    }
    const auto it = entries_.find(file_path);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    const Entry& e = it->second;
    if (e.size_bytes != size_bytes || e.modified != modified) {
        return std::nullopt; // stale — same key, changed file
    }
    it->second.last_used_ctr = next_ctr_++;
    return e.tools;
}

void ToolsUsedCache::store(const std::string& file_path, uint64_t size_bytes, time_t modified,
                           const std::set<int>& tools) {
    if (!loaded_ && !load_from_disk()) {
    }
    Entry& e = entries_[file_path];
    e.size_bytes = size_bytes;
    e.modified = modified;
    e.tools = tools;
    e.last_used_ctr = next_ctr_++;

    if (entries_.size() > MAX_ENTRIES) {
        // Evict lowest last_used_ctr entries down to MAX_ENTRIES.
        std::vector<std::map<std::string, Entry>::iterator> victims;
        // (collect all, partial_sort by ctr ascending, erase the front slice)
        std::vector<std::pair<uint64_t, std::map<std::string, Entry>::iterator>> all;
        all.reserve(entries_.size());
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            all.emplace_back(it->second.last_used_ctr, it);
        }
        const size_t excess = entries_.size() - MAX_ENTRIES;
        std::partial_sort(all.begin(), all.begin() + static_cast<long>(excess), all.end());
        for (size_t i = 0; i < excess; ++i) {
            entries_.erase(all[i].second);
        }
    }
    save_to_disk();
}

} // namespace helix
```

(Add `#include <algorithm> <utility> <vector>` for the eviction block. `get_helix_cache_dir()` creates the subdir — confirmed at `src/app_globals.cpp:426-430`.)

- [ ] **Step 4: Run tests**

Run: `make test && ./build/bin/helix-tests "[tools_used_cache]"`
Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add include/tools_used_cache.h src/system/tools_used_cache.cpp tests/unit/test_tools_used_cache.cpp
git commit -m "feat(cache): persistent tools-used cache keyed by path/size/mtime"
```

---

### Task 3: Shared gcode download in the detail view

**Files:**
- Modify: `include/ui_print_select_detail_view.h` (add members + method)
- Modify: `src/ui/ui_print_select_detail_view.cpp:1318-1424` (`kick_off_headless_tools_scan`), `:1435-1673` (`load_gcode_for_preview`)

**Interfaces:**
- Consumes: Task 1's `scan_tools_used_from_file(path, stop_set)`.
- Produces (consumed by Task 4 for the scan path):
  ```cpp
  // PrintSelectDetailView (private):
  // Canonical downloaded-gcode path for the current file. All consumers
  // (headless scan, viewer preview) share ONE file + ONE download.
  std::string canonical_gcode_path() const;          // <cache>/gcode_temp/detail_<hash(file_path)>.gcode
  // Invokes cb (main thread) once the file exists on disk. Concurrent callers
  // share a single in-flight download. ok=false on download error.
  void ensure_gcode_downloaded(std::function<void(bool ok, const std::string& path)> cb);
  // Members:
  bool gcode_download_in_flight_ = false;
  std::vector<std::function<void(bool, std::string)>> gcode_download_waiters_;
  std::string gcode_download_error_; // "" while in flight / success
  ```

Path note: the hash input changes from `current_filename_` to the full relative path (`current_path_.empty() ? current_filename_ : current_path_ + "/" + current_filename_`) — fixes same-name-different-directory collisions. Stale `detail_preview_*` files under the old hash are orphaned in the cache dir; acceptable (cache dir, not config; the existing `temp_gcode_path_` cleanup removes the current file on view teardown as today).

- [ ] **Step 1: Implement `ensure_gcode_downloaded` + `canonical_gcode_path`**

In `ui_print_select_detail_view.cpp` (new section after `show()`/`hide()`):

```cpp
std::string PrintSelectDetailView::canonical_gcode_path() const {
    const std::string file_path =
        current_path_.empty() ? current_filename_ : current_path_ + "/" + current_filename_;
    return get_helix_cache_dir("gcode_temp") + "/detail_" +
           std::to_string(std::hash<std::string>{}(file_path)) + ".gcode";
}

void PrintSelectDetailView::ensure_gcode_downloaded(
    std::function<void(bool ok, const std::string& path)> cb) {
    const std::string path = canonical_gcode_path();
    // 1. Already on disk (cached from a previous open of this file).
    if (std::ifstream f(path, std::ios::binary | std::ios::ate); f && f.tellg() > 0) {
        cb(true, path);
        return;
    }
    // 2. Join an in-flight download.
    if (gcode_download_in_flight_) {
        gcode_download_waiters_.push_back(std::move(cb));
        return;
    }
    // 3. Start the download.
    gcode_download_in_flight_ = true;
    gcode_download_waiters_.push_back(std::move(cb));
    const std::string file_path =
        current_path_.empty() ? current_filename_ : current_path_ + "/" + current_filename_;
    auto tok = lifetime_.token();
    api_->transfers().download_file_to_path(
        "gcodes", file_path, path,
        [this, tok, path](const std::string& local) {
            tok.defer("DetailView::gcode_shared_download_done", [this, local]() {
                temp_gcode_path_ = local; // keep existing teardown/cleanup semantics
                const bool was_in_flight = gcode_download_in_flight_;
                gcode_download_in_flight_ = false;
                auto waiters = std::move(gcode_download_waiters_);
                gcode_download_waiters_.clear();
                for (auto& w : waiters) w(was_in_flight, local);
            });
        },
        [this, tok, path](const MoonrakerError& err) {
            tok.defer("DetailView::gcode_shared_download_fail", [this, err]() {
                spdlog::warn("[DetailView] Shared gcode download failed: {}", err.message);
                gcode_download_in_flight_ = false;
                auto waiters = std::move(gcode_download_waiters_);
                gcode_download_waiters_.clear();
                for (auto& w : waiters) w(false, {});
            });
        });
}
```

- [ ] **Step 2: Rewire `kick_off_headless_tools_scan`**

Replace the download block (`:1406-1423`) with `ensure_gcode_downloaded`; the scan callback keeps the no-`this` HTTP-thread rule but now runs inside `tok.defer`? NO — keep the scan OFF the main thread: the success waiter runs on the main thread, so re-launch the scan there via a background executor? Simplest correct shape (scan stays off-main, result marshals back):

```cpp
ensure_gcode_downloaded([this, tok, path = canonical_gcode_path()](bool ok, const std::string&) mutable {
    if (!ok) {
        finish_scan(tok, {}); // existing finish lambda, degraded
        return;
    }
    // Scan on a background lane; result marshals via tok.defer (finish() already
    // does this). Early-exit once every palette tool has been seen.
    std::set<int> stop_set;
    for (size_t i = 0; i < current_filament_colors_.size(); ++i) {
        stop_set.insert(static_cast<int>(i));
    }
    helix::HttpExecutor::slow([this, tok, path, stop_set]() mutable {
        std::set<int> tools = helix::gcode::scan_tools_used_from_file(path, stop_set);
        finish_scan(tok, std::move(tools));
    });
});
```

Where `finish_scan` is the existing `finish` lambda refactored into a small private helper (its body is unchanged — `tok.defer` marshals, sets `headless_tools_used_`, renders, `recompute_preflight`, `set_used_tools`, `fire_on_preflight_ready`). Check `include/http_executor.h` for the exact `HttpExecutor::slow` signature (static? takes `std::function<void()>`?) and adapt — the class is the sanctioned way to run one-shot background work (AGENTS.md threading rules). Do NOT `std::thread(...).detach()`.

The scan no longer deletes the file (the viewer shares it). Remove `std::remove(scan_path.c_str())` and the `tools_scan_<hash>` path construction entirely.

- [ ] **Step 3: Rewire `load_gcode_for_preview`'s download path**

Keep: metadata fetch → `is_gcode_2d_streaming_safe(metadata.size)` gate → thumbnail fallback. Replace BOTH the cached-file fast path (`:1501-1558`, keep the `is_gcode_2d_streaming_safe` check on the cached size) and the download block (`:1596-1660`) with `ensure_gcode_downloaded` + the existing load-callback body. The load callback body (`:1513-1551`) is identical in both paths — factor it into a private helper `begin_viewer_load(const std::string& path)` so it exists once:

```cpp
void PrintSelectDetailView::begin_viewer_load(const std::string& path) {
    ui_gcode_viewer_set_load_callback(gcode_viewer_, /* existing callback, unchanged body */, this);
    ui_gcode_viewer_load_file(gcode_viewer_, path.c_str());
}
```

Resulting flow: `on_activate() → load_gcode_for_preview()`:
- Thumbnail-only mode → early-out as today.
- `ensure_gcode_downloaded([this, tok](bool ok, const std::string& p) { ok ? begin_viewer_load(p) : show_gcode_viewer(false); })` — after the metadata size gate. The metadata gate ordering: fetch metadata first (as today), then ensure-download. When the file is already cached on disk AND metadata fetch is slow, today's code loads from cache without waiting for metadata — preserve that by checking disk presence first (`ensure_gcode_downloaded` returns immediately when on disk, so calling it BEFORE the metadata fetch preserves the fast path; the size gate then applies only on cold downloads, matching today).

- [ ] **Step 4: Build + behavior smoke test**

Run: `make -j && SDL_VIDEODRIVER=dummy HELIX_MOCK_AMS=afc HELIX_CONFIG_DIR=/tmp/helix-config-helixscreen ./build/bin/helix-screen --test -vv --remote-socket /tmp/helix-flash.sock &` then open a file and check the log:
Expected: exactly ONE `download_file_to_path`-related line per open; no `tools_scan_` path anywhere; viewer still loads and renders (`First frame rendered`).
Then `pkill -f helix-flash.sock` style targeted kill (by socket name, never broad).

- [ ] **Step 5: Commit**

```bash
git add include/ui_print_select_detail_view.h src/ui/ui_print_select_detail_view.cpp
git commit -m "refactor(detail): one shared gcode download for scan + viewer preview"
```

---

### Task 4: Cache seeding, readiness subject, and the skeleton latch

**Files:**
- Modify: `include/ui_print_select_detail_view.h` (subject member, `show()` signature, `publish_mapping_ready()`, `ToolsUsedCache` member, `begin_viewer_load` decl if not added in Task 3)
- Modify: `src/ui/ui_print_select_detail_view.cpp` (`show()` :357-465, scan finish `:1344-1399`, viewer load callbacks, `on_deactivate()` :534)
- Modify: `src/ui/ui_panel_print_select.cpp` (`apply_file_selection` :2631, `show_detail_view` :2090) + `include/ui_panel_print_select.h` (member `selected_modified_timestamp_`)
- Modify: `ui_xml/print_file_detail.xml` (:177-195 swatches card, :216-254 mapping card)
- Test: `tests/unit/test_print_select_detail_subjects.cpp` (extend; check its fixture shape first)

**Interfaces:**
- Consumes: Task 2 `ToolsUsedCache::lookup/store`, Task 3 `ensure_gcode_downloaded`.
- Produces: LVGL int subject `detail_mapping_ready` (0 = not ready / skeleton, 1 = authoritative state rendered), published via:
  ```cpp
  void PrintSelectDetailView::publish_mapping_ready() {
      lv_subject_set_int(&detail_mapping_ready_, is_preflight_ready() ? 1 : 0);
  }
  ```
  This is a *publish of the existing readiness concept* (`is_preflight_ready()`, `include/ui_print_select_detail_view.h:235`) — the print-start gate's `run_when_preflight_ready()` semantics are untouched.
- `show()` grows one parameter (last):
  ```cpp
  void show(const std::string& filename, const std::string& current_path,
            const std::string& filament_type,
            const std::vector<std::string>& filament_colors,
            const std::vector<std::string>& filament_materials,
            size_t file_size_bytes, time_t modified_timestamp);
  ```

- [ ] **Step 1: Plumb mtime + add subject + cache seed in `show()`**

Panel side: `include/ui_panel_print_select.h` member `time_t selected_modified_timestamp_ = 0;`; set in `apply_file_selection()` (`selected_modified_timestamp_ = file.modified_timestamp;`); pass at the call site (`:2098-2100`).

Detail-view side, in `init_subjects()` next to the existing subjects:

```cpp
// Mapping/swatch readiness (0 = skeleton, 1 = authoritative chips rendered).
// Mirrors is_preflight_ready() — the same readiness the print-start gate
// waits on. Cache hit => 1 immediately at show(); else flips when the tools
// scan or viewer parse completes.
UI_MANAGED_SUBJECT_INT(detail_mapping_ready_, 0, "detail_mapping_ready", subjects_);
```

Member + include: `#include "tools_used_cache.h"`, member `helix::ToolsUsedCache tools_used_cache_;` and `time_t current_file_modified_ = 0;`.

In `show()`, after `current_file_size_bytes_ = file_size_bytes;`:

```cpp
current_file_modified_ = modified_timestamp;

// --- Tools-used cache: instant authoritative chip state on re-prints ---
// Publish "not ready" FIRST so a miss shows the skeleton (subjects settle
// before the first frame renders — LVGL batches within one show() call).
headless_tools_used_.reset();
headless_scan_done_ = false;
lv_subject_set_int(&detail_mapping_ready_, 0);

const std::string file_key =
    current_path_.empty() ? current_filename_ : current_path_ + "/" + current_filename_;
if (auto cached = tools_used_cache_.lookup(file_key, current_file_size_bytes_,
                                           current_file_modified_)) {
    headless_tools_used_ = *cached;
    headless_scan_done_ = true; // readiness now true; the scan below is skipped
    spdlog::debug("[DetailView] Tools-used cache hit ({} tools)", cached->size());
}
```

And at the END of `show()` (after `filament_mapping_card_.update(...)` and visibility publishing): `publish_mapping_ready();`

In `on_activate()` → `kick_off_headless_tools_scan()` — skip when the cache already answered:

```cpp
void PrintSelectDetailView::kick_off_headless_tools_scan() {
    if (headless_scan_done_) {
        spdlog::debug("[DetailView] Tools-used already known (cache/viewer) — skipping scan");
        return;
    }
    // ... existing body minus the initial headless_scan_done_ = false reset
}
```

(Remove `headless_scan_done_ = false; headless_tools_used_.reset();` from its top — `show()` now owns the reset. Keep the no-api/no-cache-dir early-outs setting `headless_scan_done_ = true` + `publish_mapping_ready()` + `fire_on_preflight_ready()`.)

Write-through, in the scan `finish` helper AND in `try_extract_gcode_colors()` (viewer-parse path), right where the set becomes final:

```cpp
tools_used_cache_.store(file_key, current_file_size_bytes_, current_file_modified_,
                        tools_used_effective());
```

Every site that flips readiness also calls `publish_mapping_ready()`: scan finish helper, both viewer load callbacks (`gcode_loaded_ = true;` → next line), `on_deactivate()` (→ 0, next to `lv_subject_set_int(&detail_viewer_first_frame_, 0)`).

- [ ] **Step 2: XML skeleton for the mapping card**

In `ui_xml/print_file_detail.xml`, inside `filament_mapping_card` (`:249-253`): gate the rows on readiness and add a skeleton row beside it (header + sliced-colors toggle stay visible — stable layout):

```xml
<lv_obj name="filament_mapping_rows"
        width="100%" height="content" style_pad_all="0" style_pad_gap="#space_xs" flex_flow="row_wrap"
        scrollable="false" clickable="false" event_bubble="true">
  <bind_flag_if_eq subject="detail_mapping_ready" flag="hidden" ref_value="0"/>
  <!-- Rows added dynamically in C++ -->
</lv_obj>
<!-- Skeleton: two pill-shaped placeholders sized like real pills. Shown only
     until the authoritative tool set exists (cache hit never shows this). -->
<lv_obj name="mapping_skeleton" width="100%" height="content" style_pad_all="0"
        style_pad_gap="#space_xs" flex_flow="row_wrap" scrollable="false" clickable="false"
        event_bubble="true">
  <bind_flag_if_eq subject="detail_mapping_ready" flag="hidden" ref_value="1"/>
  <lv_obj width="48%" height="44" style_radius="#space_lg" style_bg_color="#card_bg"
          style_bg_opa="100" clickable="false" event_bubble="true"/>
  <lv_obj width="48%" height="44" style_radius="#space_lg" style_bg_color="#card_bg"
          style_bg_opa="100" clickable="false" event_bubble="true"/>
</lv_obj>
```

Note `filament_mapping_rows` flex_flow changes column→row_wrap to match what `rebuild_compact_view()` sets anyway (`ui_filament_mapping_card.cpp:199` overrides at runtime) — declaring the real value removes a hidden C++ style write. If the declarative-purity gate flags the C++ `lv_obj_set_flex_flow`, leave the C++ call (it is pre-existing, not new debt; do NOT ratchet the count up or down here beyond the XML addition).

- [ ] **Step 3: XML skeleton for the swatches card**

Same pattern inside `color_requirements_card`: `color_swatches_row` (`:188-194`) gains `<bind_flag_if_eq subject="detail_mapping_ready" flag="hidden" ref_value="0"/>`, and a skeleton row with two 40x32 rounded placeholders (`filament_swatch` chips are 40 wide / 32 tall — `ui_print_select_detail_view.cpp:828`, XML `height="100%"` of the 32px row) follows it, hidden when `ref_value="1"`.

- [ ] **Step 4: Extend unit tests**

In `tests/unit/test_print_select_detail_subjects.cpp` (read its fixture first; it drives the detail view subjects — follow its existing patterns exactly):
- After `show()` with a warmed `ToolsUsedCache` (store an entry for the file key first), `lv_subject_get_int` on `detail_mapping_ready` == 1 and `get_tools_used()` equals the cached set.
- After `show()` with a cold cache: `detail_mapping_ready` == 0.
- After the scan-finish path fires (drive `kick_off_headless_tools_scan` with the mock API as the existing tests do, or call the finish helper directly if the fixture allows): subject == 1.

If the fixture cannot drive the HTTP mock end-to-end, assert the pure publish logic instead: call `publish_mapping_ready()` after manipulating `gcode_loaded_`/`headless_scan_done_` and check the subject tracks `is_preflight_ready()`.

- [ ] **Step 5: Run + verify**

Run: `make -j && make test && ./build/bin/helix-tests "[print_select][detail]" && ./build/bin/helix-tests "[tools_used_cache]" && ./build/bin/helix-tests "[gcode][tools_used]"`

Manual visual verify (skeleton → chips, and cache-hit instant):
```bash
rm -f ~/.cache/helix/tools_used/cache.json   # cold
# launch isolated instance (AGENTS.md pattern), open a multi-color file:
#   first open  → skeleton visible briefly, then chips (ONE visible transition)
#   back, reopen → chips correct IMMEDIATELY, no skeleton, no flash
```
Confirm via log: `Tools-used cache hit` on the second open; `detail_mapping_ready` flips once per open.

- [ ] **Step 6: Commit**

```bash
git add include/ui_print_select_detail_view.h src/ui/ui_print_select_detail_view.cpp \
        include/ui_panel_print_select.h src/ui/ui_panel_print_select.cpp \
        ui_xml/print_file_detail.xml tests/unit/test_print_select_detail_subjects.cpp
git commit -m "feat(detail): instant color chips via tools-used cache + skeleton latch"
```

---

### Task 5: Idempotent mapping-card rebuild

**Files:**
- Modify: `include/ui_filament_mapping_card.h` (private: `std::string last_render_fingerprint_;`)
- Modify: `src/ui/ui_filament_mapping_card.cpp:173-279` (`rebuild_compact_view`), `:163-167` (`on_ui_destroyed`)
- Test: `tests/unit/test_filament_mapping_used_filter.cpp` (extend — LVGL fixture shape already present there)

**Interfaces:**
- Consumes: nothing new.
- Produces: behavior — `rebuild_compact_view()` is a no-op when inputs are unchanged and children exist (suppresses the post-open AMS-resync rebuild flash).

- [ ] **Step 1: Write failing test**

Extend `tests/unit/test_filament_mapping_used_filter.cpp` (it already constructs the card against a real `rows_container` — reuse its fixture):

```cpp
TEST_CASE("FilamentMappingCard rebuild is idempotent on identical input",
          "[filament_mapping][idempotent]") {
    // Build card against fixture container, call update() twice with the same
    // inputs; child count + pointer of first child must be IDENTICAL after the
    // second call (no destroy/recreate). Then change one slot color and verify
    // a rebuild DID happen (pointer differs).
}
```

(Flesh out with the fixture's exact setup calls — mirror the file's existing `SECTION` bodies for `update()` invocation.)

- [ ] **Step 2: Implement fingerprint**

Top of `rebuild_compact_view()`, after the post-drain `rows_container_` null check (`:189-192`):

```cpp
// Idempotent render: identical (tools, mappings, slot state) + existing
// children => nothing visible changed => skip the destroy/recreate. Kills
// the late "gray -> real" rebuild when AMS resync data arrives after open.
std::string fingerprint;
fingerprint.reserve(128);
for (const auto& t : tool_info_) {
    fingerprint += std::to_string(t.tool_index) + ":" + std::to_string(t.color_rgb) + ":" +
                   t.material + "|";
}
for (const auto& m : mappings_) {
    fingerprint += std::to_string(m.tool_index) + ">" + std::to_string(m.mapped_slot) + ":" +
                   std::to_string(m.mapped_backend) + (m.is_auto ? "a" : "m") + "|";
}
for (const auto& s : available_slots_) {
    fingerprint += std::to_string(s.backend_index) + "." + std::to_string(s.slot_index) + "=" +
                   std::to_string(s.color_rgb) + (s.is_empty ? "e" : "f") + "|";
}
if (fingerprint == last_render_fingerprint_ && lv_obj_get_child_count(rows_container_) > 0) {
    return;
}
last_render_fingerprint_ = std::move(fingerprint);
```

Clear it wherever widgets die: `on_ui_destroyed()` (`last_render_fingerprint_.clear();`) and after the mid-drain `rows_container_` null-return is unnecessary (no render happened). Also clear at the top of `update()` BEFORE `apply_used_tools_filter` — no: `update()` recomputes inputs and falls through to `rebuild_compact_view()`, whose fingerprint check handles it. No extra clear needed; only `on_ui_destroyed()`.

- [ ] **Step 3: Run tests + full card suite**

Run: `make test && ./build/bin/helix-tests "[filament_mapping]"`
Expected: PASS including the drain-reentrancy test (`#1221` guard still works — the fingerprint early-return sits AFTER the drain/null re-check, so a container destroyed during drain still returns before touching widgets).

- [ ] **Step 4: Commit**

```bash
git add include/ui_filament_mapping_card.h src/ui/ui_filament_mapping_card.cpp tests/unit/test_filament_mapping_used_filter.cpp
git commit -m "fix(filament): skip mapping-card rebuild when inputs are unchanged"
```

---

### Task 6: Full verification

- [ ] **Step 1: Gates**

```bash
make -j                                       # app builds
make test && ./build/bin/helix-tests          # full suite from repo root
python3 scripts/check_imperative_ui.py --list | wc -l   # count did not rise
python3 scripts/check_l081_anti_pattern.py
python3 scripts/check_timer_destructor_cancel.py
```

- [ ] **Step 2: End-to-end visual proof (both paths)**

Cold cache (`rm -f ~/.cache/helix/tools_used/cache.json`): open multi-color file → exactly ONE visible chip transition (skeleton → final). Reopen → zero transitions, correct pills instantly. Subset file (e.g. a T0+T2 slice of `u1_4color_ring.gcode` — create a temp test file, delete after): 2 pills from the first visible frame on cache hit.

Screenshot pairs via `ctl screenshot` at open+300 ms and open+3 s, both opens — files should be pixel-identical on the second open.

- [ ] **Step 3: Kill every spawned instance**

`pgrep -xl helix-screen` — kill only instances you launched (match by `--remote-socket` in `/proc/<pid>/cmdline`).

---

## Self-review notes

- Early-exit soundness: covered in Task 1 doc comment — beyond-palette tools are dropped by `get_used_tool_info()` (`ui_print_select_detail_view.cpp:1080`) already, so the stop-set omission is invisible downstream.
- Readiness flap risk (cache hit → scan reset → ready=false): handled — `kick_off_headless_tools_scan()` early-outs when `headless_scan_done_` is already true from the cache; `show()` owns the reset.
- Single-download + Thumbnail-Only mode: scan is the only consumer → `ensure_gcode_downloaded` still runs for it (preserves today's behavior where the scan always runs).
- Empty cached set (single-extruder): stored and returned as a hit; `set_used_tools` treats empty as "show all" — correct for a 1-palette-tool card.
- `detail_mapping_ready` reset on viewer clear paths: `on_deactivate()` sets it to 0 (paired with `detail_viewer_first_frame_`), so a reopened panel never leaks ready=1 into a new file's skeleton (also reset first-thing in `show()`).
- The U1/non-editable-backend swatches card gets the same latch — `try_extract_gcode_colors` and the headless finish both already publish `color_swatches_visible_`; the skeleton rides the same `detail_mapping_ready`.
