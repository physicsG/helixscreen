// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "lvgl/lvgl.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace helix::plugin {

constexpr size_t kMaxTableColumns = 8;
constexpr size_t kMaxTableRows = 500;
constexpr size_t kMaxTableCellBytes = 256;
constexpr size_t kMaxTableKeyBytes = 128;

struct TableColumn {
    std::string title;
    /// Percent of the table's content width; 0 shares what the sized columns leave.
    int width_pct = 0;
};

struct TableRow {
    std::string key;                ///< stable, unique within one set of rows
    std::vector<std::string> cells; ///< one per column; missing cells read empty
};

/// The data one named plugin table shows. Every live `plugin_table` widget with that
/// name renders it; a widget created later renders it on creation.
struct TableModel {
    std::vector<TableColumn> columns;
    std::vector<TableRow> rows;
    std::string selected; ///< the selected row's key; empty when none is
    size_t bytes() const;
};

/// Called with the row key a tap on a data row activates. Main thread.
using TableActivate = std::function<void(const std::string& key)>;

/// Registers the `plugin_table` XML element: an lv_table that shows the model named by
/// its `name=`, a header row of column titles, and theme colors resolved at draw time.
void register_plugin_table_widget();

/// Replaces the columns. The rows and the selection are dropped.
void table_set_columns(const std::string& name, std::vector<TableColumn> columns);
/// Replaces the rows. The selection stays on its key when a row still has it, else it
/// clears.
void table_set_rows(const std::string& name, std::vector<TableRow> rows);
void table_set_on_activate(const std::string& name, TableActivate fn);
/// Selects the row with `key`; an empty key clears the selection. False, changing
/// nothing, when no row has `key`.
bool table_select(const std::string& name, const std::string& key);
/// The model `name` shows, or null when none was set.
const TableModel* table_model(const std::string& name);
/// Forgets the model and handler; live widgets go empty.
void table_drop(const std::string& name);
size_t table_instance_count(const std::string& name);

/// The data row (0-based) a tap on table cell (`row`, `col`) activates, or -1 for the
/// header row and cells outside the data. Pure.
int table_data_row(uint32_t row, uint32_t col, size_t data_rows);

/// Column widths in pixels for a content width of `content_w`. Pure.
std::vector<int32_t> table_column_widths(const std::vector<TableColumn>& columns,
                                         int32_t content_w);

} // namespace helix::plugin
