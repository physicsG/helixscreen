// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Forward-declared to keep LVGL includes out of this header; ordinary TUs
// still get the full definition transitively via lvgl_pch.h. Mirrors the
// forward-declare pattern in helix/xml/scoped_subject_registry.h.
typedef struct _lv_subject_t lv_subject_t;

namespace helix::xml {

// A reusable, dynamically-sized pool of name-registered LVGL subjects for
// rendering variable-length lists declaratively (XML binds to
// "<prefix>_<i>" by name; C++ only ever grows/sets by index).
//
// Pointer stability: subjects and string buffers are heap-allocated
// individually (std::unique_ptr per slot) so growing the pool never
// invalidates a pointer already handed to lv_xml_register_subject() or
// returned from at().
//
// Grow-only: ensure_size() never shrinks. The high-water mark of
// registered slots lives until reclaim() (or destruction) tears the whole
// pool down.
//
// Not thread-safe; not copyable. Intended for main-thread XML/UI use only.
class IndexedSubjectPool {
  public:
    enum class Type { Int, String, Color };

    IndexedSubjectPool(std::string prefix, Type type, size_t string_cap = 64);
    ~IndexedSubjectPool();

    IndexedSubjectPool(const IndexedSubjectPool&) = delete;
    IndexedSubjectPool& operator=(const IndexedSubjectPool&) = delete;

    // Grow the pool to at least n slots, initializing and name-registering
    // any newly-created slots. No-op if n <= size().
    void ensure_size(size_t n);

    // Type::Int only.
    void set_int(size_t i, int v);

    // Type::Color only. @p rgb is 0xRRGGBB.
    void set_color(size_t i, uint32_t rgb);

    // Type::String only. Value is truncated to string_cap - 1 chars by LVGL.
    void set_string(size_t i, const std::string& v);

    lv_subject_t* at(size_t i);
    size_t size() const;

    // Unregisters every slot's name, deinits every subject, and frees all
    // backing storage. Idempotent — safe to call more than once, and safe
    // to call on an empty pool. Called automatically by the destructor.
    void reclaim();

  private:
    std::string prefix_;
    Type type_;
    size_t string_cap_;
    std::vector<std::unique_ptr<lv_subject_t>> subjects_;
    std::vector<std::unique_ptr<char[]>> string_bufs_; // empty unless type_ == String

    std::string slot_name(size_t i) const;
};

} // namespace helix::xml
