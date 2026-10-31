// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace helix {

/**
 * @brief Reference-counted hold that keeps the display awake
 *
 * While it is held, DisplaySleepController neither dims, starts a screensaver nor
 * sleeps, whatever the sleep-while-printing setting, and a display that is already
 * dimmed wakes on its next tick. For a view that waits on the user while the machine
 * is held mid-operation. release() without a matching acquire() does nothing.
 * Main thread only.
 */
class DisplayAwakeHold {
  public:
    void acquire() {
        ++m_count;
    }
    void release() {
        if (m_count > 0) {
            --m_count;
        }
    }
    bool is_held() const {
        return m_count > 0;
    }

  private:
    int m_count = 0;
};

/// The one hold the idle tick reads.
inline DisplayAwakeHold& display_awake_hold() {
    static DisplayAwakeHold hold;
    return hold;
}

/// At most one acquire of display_awake_hold(), released by reset() or destruction.
class DisplayAwakeToken {
  public:
    DisplayAwakeToken() = default;
    ~DisplayAwakeToken() {
        reset();
    }
    DisplayAwakeToken(const DisplayAwakeToken&) = delete;
    DisplayAwakeToken& operator=(const DisplayAwakeToken&) = delete;

    void acquire() {
        if (!m_held) {
            display_awake_hold().acquire();
            m_held = true;
        }
    }
    void reset() {
        if (m_held) {
            display_awake_hold().release();
            m_held = false;
        }
    }
    bool held() const {
        return m_held;
    }

  private:
    bool m_held = false;
};

} // namespace helix
