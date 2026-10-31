# Settings: Safety & Alerts

**Settings > Safety & Alerts** decides how careful HelixScreen is and how loudly it tells you things. Use it to add a confirmation to the emergency stop or to macros, to set a safety net for cancels that hang, to control camera failure detection, to hold slicer prints that don't fit the printer, and to choose how finished prints and other messages are announced.

![Safety & Alerts settings, top of the page](../../../images/user/settings-safety.png)

---

## E-Stop Confirmation

| Setting | What happens |
|---------|--------------|
| **On** (default) | E-Stop asks you to confirm first |
| **Off** | Tapping E-Stop stops the printer immediately |

The confirmation guards against stopping a print by accident. Turn it off if you want the fastest possible emergency stop.

---

## Cancel Escalation

Some printers take a long time to finish canceling a print: parking tools, cooling down, running a long `CANCEL_PRINT` macro. Cancel Escalation is a safety net. If a cancel hasn't finished after a set time, HelixScreen sends an emergency stop (`M112`).

**Off** by default.

**Leave it off** on tool changers that park tools when canceling, on printers with long `CANCEL_PRINT` macros, and on any printer whose cancel is expected to take more than a few seconds.

**Turn it on** on simple printers where a cancel should be quick, or if you've had cancels that got stuck and never returned to idle.

### Escalation Timeout

> Only shown while Cancel Escalation is on.

How long to wait before the emergency stop: **15**, **30** (default), **60** or **120 seconds**.

---

## Confirm before running macros

| Setting | What happens |
|---------|--------------|
| **On** (default) | HelixScreen asks before running a macro |
| **Off** | Tapping a macro button runs it straight away |

Keep it on if you have macros that move the toolhead, heat the printer or do anything else you wouldn't want set off by a stray tap. Turn it off if you run macros often and trust your fingers. Macros that take parameters always open a form first, whatever this says.

---

## Spaghetti Detection

> Only shown on printers with built-in camera failure detection: the Creality K2 Plus, and the Snapmaker U1 with defect detection.

While a print runs, the printer's camera watches for "spaghetti": a print that has come loose or is piling up as a tangle of plastic.

| Setting | What happens |
|---------|--------------|
| **On** (default) | The camera is checked for failures during every print |
| **Off** | Nothing is watched and no detection alerts appear |

The first time HelixScreen starts on a K2 Plus that had its own detection settings, it copies your on/off and pause choices over once. After that, they're set here.

### Pause on Detection

| Setting | What happens |
|---------|--------------|
| **On** (default) | A detected failure pauses the print and opens a dialog where you can resume, cancel, or turn detection off |
| **Off** | A detected failure only shows a warning. The print keeps going |

Grayed out while Spaghetti Detection is off. Hidden on printers whose firmware pauses the print on its own when it sees a failure (the Snapmaker U1).

See [Print Monitoring](../print-monitoring.md) for what the detection dialog offers.

---

## Hold slicer prints that do not fit

When your slicer uploads a file and asks the printer to print it, the printer starts it without HelixScreen ever seeing it. This switch lets the [HelixPrint plugin](../helixprint-plugin.md) on your printer check the file first. If the file would move past the printer's travel, heat the nozzle or bed past their limits, or use a tool the printer doesn't have, the file is still uploaded but the print isn't started. Start it from the screen once you have re-sliced it, or ignore it.

| Setting | What happens |
|---------|--------------|
| **On** | A slicer's upload-and-print waits for the check, and a file that doesn't fit stays in the file list unstarted |
| **Off** (default) | The slicer's print starts as soon as the upload finishes |

Which slicer starts are held: only an upload that asks for the print in the same request. That is a slicer set up with an **OctoPrint**-style host upload and its **Upload and Print** (or **Print**) button, such as PrusaSlicer, SuperSlicer and OrcaSlicer's host upload. Every file type Moonraker prints is held (`.gcode`, `.g`, `.gco`, `.nc`, Cura's `.ufp`), and so is a `.gcode.3mf` when the plugin is set to start converted 3MF uploads. These are **not** held, because the plugin never sees a request to print them:

- OrcaSlicer's own Moonraker printer connection when it uploads the file first and then sends a separate start command
- a print started from Mainsail, Fluidd or another app after the upload
- the next job in Moonraker's job queue

- A different nozzle size or printer model in the slicer profile never holds a print.
- The slicer waits at most 45 seconds from the moment its upload arrives, including the time a 3MF takes to convert. If the plugin hasn't read the file by then, or can't read the printer's configuration, a G-code upload starts as it would without the switch, and a 3MF upload is converted but not started (the slicer is told so). The printer's Moonraker log says why.
- While the printer is already printing, nothing waits. A file the plugin has already read is checked and, if it doesn't fit, isn't queued. A file it hasn't read yet is queued unchecked, as it would be without the switch, and starts later without a check.
- A file that is printing right now, or already waits in the job queue, is never started or queued a second time, even if the slicer sends it twice. Uploading the same name again after its print has finished starts it as usual.
- The plugin can't see the extra tools a filament system adds, so this switch doesn't hold a print for its tool count. The screen still checks it when you start the file there: a file using a tool past the printer's extruders is blocked, and one past a count only its tool macros give is a warning with **Start Anyway**.
- Without the [HelixPrint plugin](../helixprint-plugin.md) the switch is greyed out with **Requires the HelixPrint plugin** and an **Install** button: only the plugin sees a slicer's print. Prints you start from the screen are checked either way; see [Does the file fit this printer?](../printing.md#does-the-file-fit-this-printer).

---

## Print Completion Alert

How HelixScreen tells you a print has finished, been canceled or failed, when you're not already looking at the print status screen.

| Setting | What you get |
|---------|--------------|
| **Off** | Nothing on screen. The sound still plays if sounds are on |
| **Notification** | A short message at the top of the screen |
| **Alert** (default) | A full-screen summary of the print (time, layers, filament used), with confetti when it succeeded |

A failed print always gets the full alert, whatever you choose, because errors need your attention. If the print status screen is already open when the print ends, there's no extra alert: that screen shows the result.

The end-of-print sound plays for every finished, canceled or failed print as long as the master Sounds switch is on.

---

## On-screen Alerts

Toasts are the short messages that slide in at the top of the screen, like "Filament loaded", "Saved" or "Update available". If the informational ones feel chatty, choose the least important kind that's still allowed to interrupt you:

| Setting | What still shows |
|---------|------------------|
| **All** (default) | Everything: info, success, warnings and errors |
| **Warnings & errors** | Info and success messages are held back |
| **Errors only** | Only errors |

Held-back messages aren't lost. They still go into the notification history, which you open from the Notifications widget on the Home screen. This setting never hides a full-screen error, and never hides the [Print Completion Alert](#print-completion-alert).

---

[Back to Settings](../settings.md) | [Prev: Devices](devices.md) | [Next: Connection](connection.md)
