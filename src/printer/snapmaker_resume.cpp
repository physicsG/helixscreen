// SPDX-License-Identifier: GPL-3.0-or-later
#include "snapmaker_resume.h"

#include "detection_source.h"

namespace helix {

std::vector<TerminalMatcher> snapmaker_terminal_matchers() {
    // Exception 532 is the defect_detection family: code 1 dirty bed, 2 noodle,
    // 3 residue, 4 dirty nozzle. The firmware PAUSEs (does not cancel) for all
    // four, so only the hardware-verified dirty-bed case (#991) is Terminal —
    // the rest attempt RESUME. Match by message substring OR by id+code so
    // either signal alone suffices. NOT gated on sdcard-inactive: runout
    // (id 523) also deactivates the SD, so sdcard state cannot discriminate
    // terminal-vs-recoverable here.
    return {
        TerminalMatcher{/*message_substr=*/"dirty bed", /*exception_id=*/-1,
                        /*require_sdcard_inactive=*/false},
        TerminalMatcher{/*message_substr=*/"",
                        /*exception_id=*/detection::kU1DefectExceptionId,
                        /*require_sdcard_inactive=*/false,
                        /*exception_code=*/detection::kU1DefectCodeDirtyBed},
    };
}

} // namespace helix
