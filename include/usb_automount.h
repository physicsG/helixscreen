// Copyright (C) 2025-2026 356C LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#if defined(__linux__) && !defined(__ANDROID__)

#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace helix::usb {

/**
 * @brief One (filesystem, options) candidate tried when mounting a stick
 */
struct MountAttempt {
    std::string fs_type;
    std::string options;
};

/**
 * @brief Ordered mount candidates for a removable block device
 *
 * Kernel NLS configuration differs per board, so no single option string
 * mounts a FAT stick everywhere: plain kernel defaults work where the
 * default iocharset is built in, an explicit iocharset=utf8 is needed where
 * the FAT default codepage has no NLS module, and forcing utf8 fails on
 * kernels without NLS_UTF8. Candidates are therefore ordered
 * kernel-defaults-first and long-names-first:
 *
 * 1. vfat before msdos - a vfat mount keeps long filenames, msdos reduces
 *    3DBenchy.gcode to 3DBENC~1.GCO
 * 2. plain options before explicit charsets, utf8 before iso8859-1, the
 *    codepage=437 combination last
 * 3. exfat and ntfs3 for larger sticks, before the msdos last resort
 */
std::vector<MountAttempt> automount_ladder();

/**
 * @brief Mount point this component uses for a device node
 *
 * Must stay inside the prefixes UsbBackendLinux::is_usb_mount accepts so the
 * regular mount-table detection surfaces the drive with no special casing.
 */
std::string automount_mount_point(const std::string& device_node);

/**
 * @brief What the system probe observed about primary USB mounters
 *
 * The mount grace period exists only to lose the race against a primary
 * mounter (udisks2, udev, an mdev hotplug helper, a vendor app). On a system
 * where the probe positively observes that none is registered, waiting is
 * dead time before the first mount; any less-certain answer keeps the grace.
 */
enum class MounterPresence {
    PRESENT, ///< udev daemon or a kernel hotplug helper is registered
    ABSENT,  ///< both signals positively observed absent
    UNKNOWN, ///< probe could not observe the system; behave as PRESENT
};

/**
 * @brief Syscall surface of UsbAutomount, injectable for tests
 */
class MountOps {
  public:
    virtual ~MountOps() = default;

    /// Process runs as root (mounting a device requires it)
    virtual bool is_root() = 0;

    /// Device nodes listed anywhere in the mount table (/proc/mounts col 1)
    virtual std::vector<std::string> mounted_devices() = 0;

    /// Unmounted candidate device nodes (removable/USB sd* disks + partitions)
    virtual std::vector<std::string> removable_block_devices() = 0;

    /// The device's sysfs node still exists (false once unplugged)
    virtual bool device_present(const std::string& device_node) = 0;

    /// mount(2) a device read-only; false on any errno
    virtual bool mount(const std::string& device_node, const std::string& mount_point,
                       const std::string& fs_type, const std::string& options) = 0;

    /// umount2 a mount point; lazy = MNT_DETACH
    virtual bool unmount(const std::string& mount_point, bool lazy) = 0;

    /// Create the mount point directory (mkdir -p semantics)
    virtual bool ensure_mount_point_dir(const std::string& mount_point) = 0;
};

/**
 * @brief Fallback mounter for boards where nothing else mounts USB sticks
 *
 * Runs on the USB backend's monitor thread. After a grace period (so a
 * primary mounter - udisks2, a vendor app - wins the race; skipped only when
 * the boot-time probe says none can exist) it mounts
 * otherwise-unmounted removable devices read-only, letting the backend's
 * existing mount-table detection pick the drive up. It never mounts a device
 * the mount table already lists, and never unmounts a mount it did not
 * create - including the stale entry a yanked stick leaves behind, which it
 * does clean up (lazily, when busy).
 */
class UsbAutomount {
  public:
    static constexpr std::chrono::milliseconds kDefaultGrace{3000};
    static constexpr std::chrono::milliseconds kRetryCooldown{30000};

    /**
     * @brief Production instance, or nullptr when fallback mounting is off
     *
     * Requires root; a developer's desktop build must never mount the
     * machine's own USB drives. HELIX_USB_AUTOMOUNT=0 forces the component
     * off regardless of user.
     */
    static std::unique_ptr<UsbAutomount> create();

    /// Injection form for tests: decisions against a fake syscall surface.
    /// primary_mounter is the (injected or probed) capability answer that
    /// decides whether the grace period applies at all.
    explicit UsbAutomount(std::unique_ptr<MountOps> ops,
                          std::chrono::milliseconds grace_period = kDefaultGrace,
                          MounterPresence primary_mounter = MounterPresence::UNKNOWN);

    bool armed() const {
        return armed_;
    }

    /**
     * @brief One pass; call periodically from a monitor loop
     *
     * now is injected so grace and cooldown decisions are testable.
     */
    void poll(std::chrono::steady_clock::time_point now);

    /// Unmount every mount this component created (clean shutdown)
    void unmount_all();

    /// Mounts currently tracked as created by this component
    std::size_t our_mount_count() const {
        return ours_.size();
    }

  private:
    struct Candidate {
        std::chrono::steady_clock::time_point first_seen;
        std::chrono::steady_clock::time_point retry_after{};
    };

    void attempt_mount(const std::string& device_node, std::chrono::steady_clock::time_point now);
    void unmount_one(const std::string& device_node, const std::string& mount_point);

    std::unique_ptr<MountOps> ops_;
    std::chrono::milliseconds grace_;
    bool armed_{false};

    std::map<std::string, Candidate> pending_; // device -> grace/cooldown state
    std::map<std::string, std::string> ours_;  // device -> mount point WE created
    std::optional<MountAttempt> ladder_cache_; // combination that worked on this kernel
};

} // namespace helix::usb

#endif // __linux__ && !__ANDROID__
