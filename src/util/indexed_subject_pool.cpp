// SPDX-License-Identifier: GPL-3.0-or-later
#include "helix/xml/indexed_subject_pool.h"

#include "lvgl/lvgl.h"

extern "C" {
#include "helix-xml/src/xml/lv_xml.h"
}

#include <cassert>

namespace helix::xml {

IndexedSubjectPool::IndexedSubjectPool(std::string prefix, Type type, size_t string_cap)
    : prefix_(std::move(prefix)), type_(type), string_cap_(string_cap) {}

IndexedSubjectPool::~IndexedSubjectPool() {
    reclaim();
}

std::string IndexedSubjectPool::slot_name(size_t i) const {
    return prefix_ + "_" + std::to_string(i);
}

void IndexedSubjectPool::ensure_size(size_t n) {
    if (n <= subjects_.size()) {
        return;
    }

    if (type_ == Type::String) {
        string_bufs_.reserve(n);
    }
    subjects_.reserve(n);

    for (size_t i = subjects_.size(); i < n; ++i) {
        auto subject = std::make_unique<lv_subject_t>();

        if (type_ == Type::Int) {
            lv_subject_init_int(subject.get(), 0);
        } else if (type_ == Type::Color) {
            lv_subject_init_color(subject.get(), lv_color_hex(0));
        } else {
            auto buf = std::make_unique<char[]>(string_cap_);
            lv_subject_init_string(subject.get(), buf.get(), nullptr, string_cap_, "");
            string_bufs_.push_back(std::move(buf));
        }

        lv_xml_register_subject(nullptr, slot_name(i).c_str(), subject.get());
        subjects_.push_back(std::move(subject));
    }
}

void IndexedSubjectPool::set_int(size_t i, int v) {
    assert(type_ == Type::Int);
    lv_subject_set_int(subjects_.at(i).get(), v);
}

void IndexedSubjectPool::set_color(size_t i, uint32_t rgb) {
    assert(type_ == Type::Color);
    lv_subject_set_color(subjects_.at(i).get(), lv_color_hex(rgb));
}

void IndexedSubjectPool::set_string(size_t i, const std::string& v) {
    assert(type_ == Type::String);
    lv_subject_copy_string(subjects_.at(i).get(), v.c_str());
}

lv_subject_t* IndexedSubjectPool::at(size_t i) {
    return subjects_.at(i).get();
}

size_t IndexedSubjectPool::size() const {
    return subjects_.size();
}

void IndexedSubjectPool::reclaim() {
    for (size_t i = 0; i < subjects_.size(); ++i) {
        lv_xml_unregister_subject(nullptr, slot_name(i).c_str());
        lv_subject_deinit(subjects_[i].get());
    }
    subjects_.clear();
    string_bufs_.clear();
}

} // namespace helix::xml
