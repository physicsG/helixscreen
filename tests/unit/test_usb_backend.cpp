// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "usb_backend.h"
#include "usb_backend_mock.h"
#include "usb_manager.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>

#include "../catch_amalgamated.hpp"

// Detection reports a drive's identity - mount point, device, label - and
// nothing else. Free space on a removable FAT volume costs a full FAT scan on
// the first query after mount, so UsbDrive carries no capacity members and
// detection cannot grow a dependency on them; this fails the build if one is
// reintroduced.
template <typename T, typename = void> struct HasCapacityMembers : std::false_type {};
template <typename T>
struct HasCapacityMembers<
    T, std::void_t<decltype(std::declval<T&>().total_bytes, std::declval<T&>().available_bytes)>>
    : std::true_type {};
static_assert(!HasCapacityMembers<UsbDrive>::value,
              "UsbDrive must not carry capacity fields; compute free space on demand");

TEST_CASE("UsbBackendMock lifecycle", "[usb_backend][mock]") {
    UsbBackendMock backend;

    SECTION("starts and stops correctly") {
        REQUIRE_FALSE(backend.is_running());

        UsbError result = backend.start();
        REQUIRE(result.success());
        REQUIRE(backend.is_running());

        backend.stop();
        REQUIRE_FALSE(backend.is_running());
    }

    SECTION("start is idempotent") {
        REQUIRE(backend.start().success());
        REQUIRE(backend.start().success());
        REQUIRE(backend.is_running());
    }

    SECTION("stop is idempotent") {
        backend.start();
        backend.stop();
        backend.stop(); // Should not crash
        REQUIRE_FALSE(backend.is_running());
    }
}

TEST_CASE("UsbBackendMock drive simulation", "[usb_backend][mock]") {
    UsbBackendMock backend;
    backend.start();

    SECTION("no drives initially") {
        std::vector<UsbDrive> drives;
        UsbError result = backend.get_connected_drives(drives);
        REQUIRE(result.success());
        REQUIRE(drives.empty());
    }

    SECTION("simulate drive insert") {
        UsbDrive drive("/media/usb0", "/dev/sda1", "TEST_USB");
        backend.simulate_drive_insert(drive);

        std::vector<UsbDrive> drives;
        REQUIRE(backend.get_connected_drives(drives).success());
        REQUIRE(drives.size() == 1);
        REQUIRE(drives[0].mount_path == "/media/usb0");
        REQUIRE(drives[0].label == "TEST_USB");
    }

    SECTION("simulate drive remove") {
        UsbDrive drive("/media/usb0", "/dev/sda1", "TEST_USB");
        backend.simulate_drive_insert(drive);

        backend.simulate_drive_remove("/media/usb0");

        std::vector<UsbDrive> drives;
        REQUIRE(backend.get_connected_drives(drives).success());
        REQUIRE(drives.empty());
    }

    SECTION("multiple drives") {
        backend.simulate_drive_insert(UsbDrive("/media/usb0", "/dev/sda1", "USB1"));
        backend.simulate_drive_insert(UsbDrive("/media/usb1", "/dev/sdb1", "USB2"));

        std::vector<UsbDrive> drives;
        REQUIRE(backend.get_connected_drives(drives).success());
        REQUIRE(drives.size() == 2);
    }

    SECTION("duplicate insert ignored") {
        UsbDrive drive("/media/usb0", "/dev/sda1", "TEST_USB");
        backend.simulate_drive_insert(drive);
        backend.simulate_drive_insert(drive); // Should be ignored

        std::vector<UsbDrive> drives;
        REQUIRE(backend.get_connected_drives(drives).success());
        REQUIRE(drives.size() == 1);
    }

    SECTION("remove nonexistent drive ignored") {
        backend.simulate_drive_remove("/media/nonexistent"); // Should not crash

        std::vector<UsbDrive> drives;
        REQUIRE(backend.get_connected_drives(drives).success());
        REQUIRE(drives.empty());
    }
}

TEST_CASE("UsbBackendMock event callbacks", "[usb_backend][mock]") {
    UsbBackendMock backend;
    backend.start();

    std::atomic<int> insert_count{0};
    std::atomic<int> remove_count{0};
    std::string last_mount_path;

    backend.set_event_callback([&](UsbEvent event, const UsbDrive& drive) {
        if (event == UsbEvent::DRIVE_INSERTED) {
            insert_count++;
            last_mount_path = drive.mount_path;
        } else if (event == UsbEvent::DRIVE_REMOVED) {
            remove_count++;
        }
    });

    SECTION("insert fires callback") {
        UsbDrive drive("/media/usb0", "/dev/sda1", "TEST");
        backend.simulate_drive_insert(drive);

        REQUIRE(insert_count == 1);
        REQUIRE(remove_count == 0);
        REQUIRE(last_mount_path == "/media/usb0");
    }

    SECTION("remove fires callback") {
        UsbDrive drive("/media/usb0", "/dev/sda1", "TEST");
        backend.simulate_drive_insert(drive);
        backend.simulate_drive_remove("/media/usb0");

        REQUIRE(insert_count == 1);
        REQUIRE(remove_count == 1);
    }
}

TEST_CASE("UsbBackendMock G-code file scanning", "[usb_backend][mock]") {
    UsbBackendMock backend;
    backend.start();

    UsbDrive drive("/media/usb0", "/dev/sda1", "GCODE_USB");
    backend.simulate_drive_insert(drive);

    SECTION("no files initially") {
        std::vector<UsbGcodeFile> files;
        UsbError result = backend.scan_for_gcode("/media/usb0", files);
        REQUIRE(result.success());
        REQUIRE(files.empty());
    }

    SECTION("mock files returned") {
        std::vector<UsbGcodeFile> mock_files = {
            {"/media/usb0/benchy.gcode", "benchy.gcode", 1024, 1000000},
            {"/media/usb0/cube.gcode", "cube.gcode", 512, 2000000},
        };
        backend.set_mock_files("/media/usb0", mock_files);

        std::vector<UsbGcodeFile> files;
        REQUIRE(backend.scan_for_gcode("/media/usb0", files).success());
        REQUIRE(files.size() == 2);
        REQUIRE(files[0].filename == "benchy.gcode");
        REQUIRE(files[1].filename == "cube.gcode");
    }

    SECTION("scan nonexistent drive fails") {
        std::vector<UsbGcodeFile> files;
        UsbError result = backend.scan_for_gcode("/media/nonexistent", files);
        REQUIRE_FALSE(result.success());
        REQUIRE(result.result == UsbResult::DRIVE_NOT_FOUND);
    }

    SECTION("files cleared on drive remove") {
        std::vector<UsbGcodeFile> mock_files = {
            {"/media/usb0/test.gcode", "test.gcode", 100, 1000},
        };
        backend.set_mock_files("/media/usb0", mock_files);
        backend.simulate_drive_remove("/media/usb0");

        // Re-insert drive - files should be gone
        backend.simulate_drive_insert(drive);
        std::vector<UsbGcodeFile> files;
        REQUIRE(backend.scan_for_gcode("/media/usb0", files).success());
        REQUIRE(files.empty());
    }
}

TEST_CASE("UsbBackendMock demo drives", "[usb_backend][mock]") {
    UsbBackendMock backend;
    backend.start();

    backend.add_demo_drives();

    std::vector<UsbDrive> drives;
    REQUIRE(backend.get_connected_drives(drives).success());
    REQUIRE(drives.size() >= 1);
    REQUIRE(drives[0].label == "PRINT_FILES");

    std::vector<UsbGcodeFile> files;
    REQUIRE(backend.scan_for_gcode(drives[0].mount_path, files).success());
    REQUIRE(files.size() >= 1);
}

TEST_CASE("UsbBackendMock clear all", "[usb_backend][mock]") {
    UsbBackendMock backend;
    backend.start();

    backend.add_demo_drives();
    backend.clear_all();

    std::vector<UsbDrive> drives;
    REQUIRE(backend.get_connected_drives(drives).success());
    REQUIRE(drives.empty());
}

TEST_CASE("UsbBackendMock operations when not running", "[usb_backend][mock]") {
    UsbBackendMock backend;
    // Don't start

    SECTION("get_connected_drives fails") {
        std::vector<UsbDrive> drives;
        UsbError result = backend.get_connected_drives(drives);
        REQUIRE_FALSE(result.success());
        REQUIRE(result.result == UsbResult::NOT_INITIALIZED);
    }

    SECTION("scan_for_gcode fails") {
        std::vector<UsbGcodeFile> files;
        UsbError result = backend.scan_for_gcode("/media/usb0", files);
        REQUIRE_FALSE(result.success());
        REQUIRE(result.result == UsbResult::NOT_INITIALIZED);
    }
}

// =============================================================================
// UsbManager Tests
// =============================================================================

TEST_CASE("UsbManager lifecycle", "[usb_manager]") {
    UsbManager manager(true); // force mock

    SECTION("starts and stops correctly") {
        REQUIRE_FALSE(manager.is_running());

        REQUIRE(manager.start());
        REQUIRE(manager.is_running());

        manager.stop();
        REQUIRE_FALSE(manager.is_running());
    }

    SECTION("start is idempotent") {
        REQUIRE(manager.start());
        REQUIRE(manager.start());
        REQUIRE(manager.is_running());
    }
}

TEST_CASE("UsbManager drive queries", "[usb_manager]") {
    UsbManager manager(true); // force mock
    manager.start();

    // Get mock backend for test setup
    auto* backend = dynamic_cast<UsbBackendMock*>(manager.get_backend());
    REQUIRE(backend != nullptr);

    SECTION("get_drives returns empty initially") {
        auto drives = manager.get_drives();
        REQUIRE(drives.empty());
    }

    SECTION("get_drives returns inserted drives") {
        backend->simulate_drive_insert(UsbDrive("/media/usb0", "/dev/sda1", "TEST"));

        auto drives = manager.get_drives();
        REQUIRE(drives.size() == 1);
        REQUIRE(drives[0].label == "TEST");
    }

    SECTION("scan_for_gcode works through manager") {
        backend->simulate_drive_insert(UsbDrive("/media/usb0", "/dev/sda1", "TEST"));
        backend->set_mock_files("/media/usb0",
                                {
                                    {"/media/usb0/test.gcode", "test.gcode", 100, 1000},
                                });

        auto files = manager.scan_for_gcode("/media/usb0");
        REQUIRE(files.size() == 1);
        REQUIRE(files[0].filename == "test.gcode");
    }
}

TEST_CASE("UsbManager event callbacks", "[usb_manager]") {
    UsbManager manager(true); // force mock

    std::atomic<int> event_count{0};
    UsbEvent last_event = UsbEvent::DRIVE_REMOVED;
    std::string last_label;

    manager.set_drive_callback([&](UsbEvent event, const UsbDrive& drive) {
        event_count++;
        last_event = event;
        last_label = drive.label;
    });

    manager.start();

    auto* backend = dynamic_cast<UsbBackendMock*>(manager.get_backend());
    REQUIRE(backend != nullptr);

    SECTION("callback fires on drive insert") {
        backend->simulate_drive_insert(UsbDrive("/media/usb0", "/dev/sda1", "CALLBACK_TEST"));

        REQUIRE(event_count == 1);
        REQUIRE(last_event == UsbEvent::DRIVE_INSERTED);
        REQUIRE(last_label == "CALLBACK_TEST");
    }

    SECTION("callback fires on drive remove") {
        backend->simulate_drive_insert(UsbDrive("/media/usb0", "/dev/sda1", "TEST"));
        backend->simulate_drive_remove("/media/usb0");

        REQUIRE(event_count == 2);
        REQUIRE(last_event == UsbEvent::DRIVE_REMOVED);
    }
}

TEST_CASE("UsbManager queries when not running", "[usb_manager]") {
    UsbManager manager(true);
    // Don't start

    SECTION("get_drives returns empty") {
        auto drives = manager.get_drives();
        REQUIRE(drives.empty());
    }

    SECTION("scan_for_gcode returns empty") {
        auto files = manager.scan_for_gcode("/media/usb0");
        REQUIRE(files.empty());
    }
}

// =============================================================================
// UsbBackend Factory Tests
// =============================================================================

TEST_CASE("UsbBackend factory", "[usb_backend][factory]") {
    SECTION("force_mock creates mock backend") {
        auto backend = UsbBackend::create(true);
        REQUIRE(backend != nullptr);

        // Should be a mock backend
        auto* mock = dynamic_cast<UsbBackendMock*>(backend.get());
        REQUIRE(mock != nullptr);
    }

    SECTION("default create returns nullptr on unsupported platforms") {
        auto backend = UsbBackend::create(false);
        // On macOS and unsupported platforms, returns nullptr (no USB support)
        // On Linux, returns native backend (may succeed or fail depending on system)
#if defined(__linux__)
        // Linux may or may not have USB support - just check it doesn't crash
        (void)backend;
#else
        REQUIRE(backend == nullptr);
#endif
    }
}

#if defined(__linux__) && !defined(__ANDROID__)
TEST_CASE("UsbBackend factory returns the Linux backend unstarted", "[usb_backend][factory]") {
    // UsbManager attaches its event callback and then starts the backend; a
    // backend already running when it arrives reports drives to no one.
    auto backend = UsbBackend::create(false);
    REQUIRE(backend != nullptr);
    CHECK_FALSE(backend->is_running());
}
#endif

TEST_CASE("UsbManager stops its backend even while a scan still holds it", "[usb_manager]") {
    // A USB walk on a worker thread keeps a reference to the backend; a
    // backend still running after its manager dies reports events into
    // freed memory.
    auto manager = std::make_unique<UsbManager>(true);
    REQUIRE(manager->start());
    std::shared_ptr<UsbBackend> held = manager->backend_snapshot();
    REQUIRE(held != nullptr);
    REQUIRE(held->is_running());

    manager.reset();
    CHECK_FALSE(held->is_running());
}
