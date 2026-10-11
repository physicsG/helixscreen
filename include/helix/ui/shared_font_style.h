// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "lvgl/lvgl.h"

namespace helix::ui {

/**
 * @brief One shared ADDED style per font face, carrying only `text_font`
 *
 * A font written as a LOCAL style outranks every added style, and the XML
 * engine applies bind_style / bind_style_if_* through lv_obj_add_style — so a
 * widget whose face is local can never have it overridden from XML. Applying
 * the face as a shared added style at create time puts it below the nested
 * bind elements the parser applies afterwards, because same-precedence styles
 * resolve in addition order. An inline style_text_font attribute stays local
 * and keeps outranking both, which is declarative rule 6.
 *
 * Append-only: one entry per compiled face any caller has ever asked for, at
 * most every face the build links. The returned pointer lives for the life of
 * the process and is shared by every object using that face, so callers must
 * not mutate it.
 */
lv_style_t* shared_font_style(const lv_font_t* font);

/**
 * @brief Apply @p font to @p obj as its face, replacing any face applied before
 *
 * Use this instead of lv_obj_set_style_text_font() wherever a widget resolves
 * its own face at create time. A local style outranks every added style, so a
 * face written locally can never be overridden by a style bound from XML; an
 * added one sits below the bind elements the parser applies afterwards.
 *
 * Idempotent, and safe to call more than once on one object: the previously
 * applied face is removed first, so re-resolving a widget's size replaces its
 * face rather than stacking a second style.
 *
 * An inline style_text_font attribute stays local and keeps outranking this,
 * which is declarative rule 6.
 */
void apply_font_style(lv_obj_t* obj, const lv_font_t* font);

} // namespace helix::ui
