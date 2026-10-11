// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "moonraker_api_mock.h"
#include "moonraker_client_mock.h"
#include "printer_state.h"

// A mock client, the PrinterState it feeds and a mock API bound to both, built the same
// way everywhere. Members are declared in dependency order so teardown runs api, state, client.
struct MockPrinter {
    explicit MockPrinter(
        MoonrakerClientMock::PrinterType type = MoonrakerClientMock::PrinterType::VORON_24)
        : client(type), api((state.init_subjects(false), client), state) {}

    MoonrakerClientMock client;
    helix::PrinterState state;
    MoonrakerAPIMock api;
};
