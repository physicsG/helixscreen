// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "helix-xml/src/xml/lv_xml.h"
#include "lvgl/lvgl.h"
#include "platform_capabilities.h"

/// Sets the platform_tier subject for one scope, restoring the previous tier.
class ScopedTier {
  public:
    explicit ScopedTier(helix::PlatformTier tier)
        : subject_(lv_xml_get_subject(nullptr, "platform_tier")),
          saved_(lv_subject_get_int(subject_)) {
        lv_subject_set_int(subject_, static_cast<int>(tier));
    }
    ~ScopedTier() {
        lv_subject_set_int(subject_, saved_);
    }
    ScopedTier(const ScopedTier&) = delete;
    ScopedTier& operator=(const ScopedTier&) = delete;

  private:
    lv_subject_t* subject_;
    int saved_;
};
