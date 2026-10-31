// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui_modal.h"

#include <functional>
#include <string>

namespace helix::ui {

/// One line of text through the host keyboard: a title, a text field and OK/Cancel.
class TextEntryModal : public Modal {
  public:
    struct Config {
        std::string title;
        std::string initial;
        std::string placeholder;
        std::string accept_text; ///< the OK button's label; "OK" when empty
        uint32_t max_length = 0; ///< in characters; 0 is unlimited
        bool password = false;
        /// Called on OK. An empty result accepts the text; anything else is shown as an
        /// error and the dialog stays open.
        std::function<std::string(const std::string&)> validate;
        std::function<void(const std::string&)> on_accept;
        /// Called once for every close that is not an accepted value: Cancel, a backdrop
        /// tap, ESC, a programmatic or system close.
        std::function<void()> on_cancel;
    };

    explicit TextEntryModal(Config config);

    const char* get_name() const override {
        return "Text Entry";
    }
    const char* component_name() const override {
        return "text_entry_modal";
    }

    /// Shows the dialog on the active screen, owned by ModalStack. The dialog, or null
    /// when the XML failed to create (the instance is not leaked).
    static lv_obj_t* show_owned(Config config);

    /// The text field, for tests and remote control.
    lv_obj_t* input() const {
        return input_;
    }

  protected:
    void on_show() override;
    void on_ok() override;
    void on_cancel() override;
    void on_dismissed() override;

  private:
    void cancel_once();

    Config config_;
    lv_obj_t* input_ = nullptr;
    bool answered_ = false;
};

} // namespace helix::ui
