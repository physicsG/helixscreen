// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#if HELIX_HAS_LABEL_PRINTER

#include "sheet_label_layout.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

namespace helix::label {

// --- Paper dimensions ---

// Letter: 215.9mm x 279.4mm (8.5" x 11")
static constexpr float kLetterWidthMm = 215.9f;
static constexpr float kLetterHeightMm = 279.4f;

// A4: 210mm x 297mm
static constexpr float kA4WidthMm = 210.0f;
static constexpr float kA4HeightMm = 297.0f;

/// Convert millimeters to pixels at a given DPI
static int mm_to_px(float mm, int dpi) {
    return static_cast<int>(mm * dpi / 25.4f);
}

PageDimensions get_page_dimensions(PaperSize paper, int dpi) {
    switch (paper) {
    case PaperSize::LETTER:
        return {mm_to_px(kLetterWidthMm, dpi), mm_to_px(kLetterHeightMm, dpi), kLetterWidthMm,
                kLetterHeightMm};
    case PaperSize::A4:
        return {mm_to_px(kA4WidthMm, dpi), mm_to_px(kA4HeightMm, dpi), kA4WidthMm, kA4HeightMm};
    }
    // Unreachable, but satisfy compiler
    return {mm_to_px(kLetterWidthMm, dpi), mm_to_px(kLetterHeightMm, dpi), kLetterWidthMm,
            kLetterHeightMm};
}

const char* get_pwg_media_keyword(PaperSize paper) {
    switch (paper) {
    case PaperSize::LETTER:
        return "na_letter_8.5x11in";
    case PaperSize::A4:
        return "iso_a4_210x297mm";
    }
    return "na_letter_8.5x11in";
}

// --- Sheet templates ---

// clang-format off
static const std::vector<SheetTemplate> s_templates = {
    // US Letter templates
    {"Avery 5160", "30 labels, 1\" x 2-5/8\"",      PaperSize::LETTER, 66.7f,  25.4f,  3, 10, 4.8f,  12.7f, 3.2f, 0.0f},
    {"Avery 5163", "10 labels, 2\" x 4\"",           PaperSize::LETTER, 101.6f, 50.8f,  2, 5,  4.8f,  12.7f, 6.4f, 0.0f},
    {"Avery 5167", "80 labels, 1/2\" x 1-3/4\"",     PaperSize::LETTER, 44.5f,  12.7f,  4, 20, 7.9f,  12.7f, 3.0f, 0.0f},
    {"Avery 5195", "60 labels, 2/3\" x 1-3/4\"",     PaperSize::LETTER, 44.5f,  16.9f,  3, 20, 14.3f, 12.7f, 7.6f, 0.0f},
    {"Avery 5199", "4 labels, 3-1/2\" x 4\"",        PaperSize::LETTER, 88.9f,  101.6f, 2, 2,  19.1f, 38.1f, 0.0f, 0.0f},
    {"Avery 8126", "2 labels, 8-1/2\" x 5-1/2\"",    PaperSize::LETTER, 215.9f, 139.7f, 1, 2,  0.0f,  0.0f,  0.0f, 0.0f},

    // A4 templates
    {"Avery L7163", "14 labels, 99.1 x 38.1 mm",     PaperSize::A4, 99.1f, 38.1f, 2, 7, 4.7f, 15.1f, 2.5f, 0.0f},
    {"Avery L7160", "21 labels, 63.5 x 38.1 mm",     PaperSize::A4, 63.5f, 38.1f, 3, 7, 7.2f, 15.1f, 2.5f, 0.0f},
    {"Avery L7161", "18 labels, 63.5 x 46.6 mm",     PaperSize::A4, 63.5f, 46.6f, 3, 6, 7.2f, 8.7f,  2.5f, 0.0f},
    {"Avery L7159", "24 labels, 63.5 x 33.9 mm",     PaperSize::A4, 63.5f, 33.9f, 3, 8, 7.2f, 8.5f,  2.5f, 0.0f},
};
// clang-format on

const std::vector<SheetTemplate>& get_sheet_templates() {
    return s_templates;
}

std::vector<const SheetTemplate*> get_templates_for_paper(PaperSize paper) {
    std::vector<const SheetTemplate*> result;
    for (const auto& tmpl : s_templates) {
        if (tmpl.paper == paper) {
            result.push_back(&tmpl);
        }
    }
    return result;
}

LabelSize sheet_template_to_label_size(const SheetTemplate& tmpl, int dpi) {
    int w = mm_to_px(tmpl.label_width_mm, dpi);
    int h = mm_to_px(tmpl.label_height_mm, dpi);
    return LabelSize{
        tmpl.name,
        w,
        h,
        dpi,
        0, // media_type (not used for IPP sheet printing)
        static_cast<uint8_t>(tmpl.label_width_mm),
        static_cast<uint8_t>(tmpl.label_height_mm),
    };
}

LabelBitmap tile_labels_on_page(const LabelBitmap& label_bitmap, const SheetTemplate& tmpl,
                                int count, int start, int dpi) {
    auto page = get_page_dimensions(tmpl.paper, dpi);
    LabelBitmap sheet(page.width_px, page.height_px);
    sheet.fill(false); // white background

    int total = tmpl.labels_per_sheet();
    if (start < 0)
        start = 0;
    if (start >= total) {
        spdlog::warn("[sheet_label_layout] start {} >= total {}, returning blank page", start,
                     total);
        return sheet;
    }

    int available = total - start;
    if (count <= 0 || count > available) {
        count = available;
    }

    if (label_bitmap.empty()) {
        spdlog::warn("[sheet_label_layout] label bitmap is empty, returning blank page");
        return sheet;
    }

    int label_w_px = mm_to_px(tmpl.label_width_mm, dpi);
    int label_h_px = mm_to_px(tmpl.label_height_mm, dpi);
    int margin_left_px = mm_to_px(tmpl.margin_left_mm, dpi);
    int margin_top_px = mm_to_px(tmpl.margin_top_mm, dpi);
    int gap_x_px = mm_to_px(tmpl.gap_x_mm, dpi);
    int gap_y_px = mm_to_px(tmpl.gap_y_mm, dpi);

    // Center the label bitmap within each cell if sizes differ
    int offset_x = 0;
    int offset_y = 0;
    if (label_bitmap.width() < label_w_px) {
        offset_x = (label_w_px - label_bitmap.width()) / 2;
    }
    if (label_bitmap.height() < label_h_px) {
        offset_y = (label_h_px - label_bitmap.height()) / 2;
    }

    spdlog::debug("[sheet_label_layout] tiling {} labels starting at slot {} on {} "
                  "({}x{} px page, {}x{} px label cells)",
                  count, start, tmpl.name, page.width_px, page.height_px, label_w_px, label_h_px);

    int placed = 0;
    for (int slot = start; slot < total && placed < count; ++slot) {
        int row = slot / tmpl.columns;
        int col = slot % tmpl.columns;

        int cell_x = margin_left_px + col * (label_w_px + gap_x_px);
        int cell_y = margin_top_px + row * (label_h_px + gap_y_px);

        sheet.blit(label_bitmap, cell_x + offset_x, cell_y + offset_y);
        ++placed;
    }

    spdlog::debug("[sheet_label_layout] placed {} labels on page", placed);
    return sheet;
}

std::string sheet_template_options() {
    std::string result;
    for (const auto& tmpl : s_templates) {
        if (!result.empty()) {
            result += '\n';
        }
        result += tmpl.name;
    }
    return result;
}

} // namespace helix::label

#endif // HELIX_HAS_LABEL_PRINTER
