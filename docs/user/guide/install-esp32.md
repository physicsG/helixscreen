# Installing HelixScreen on the BTT K-Touch (ESP32) - Alpha

The BigTreeTech K-Touch is a small standalone touchscreen built around an ESP32-S3 chip. HelixScreen runs on it as firmware: the panel joins your WiFi and talks to your printer's Moonraker, just like a [remote screen](../INSTALL.md#remote-screen-setup-run-on-a-separate-device). Nothing is installed on the printer.

> **This is an alpha.** Expect rough edges and missing features (see [what doesn't work yet](#what-doesnt-work-yet-on-the-k-touch)). It replaces the K-Touch's stock BigTreeTech firmware. The 1.0.x releases do not include it: use a 1.1 beta release or newer.

**Easiest: flash from your browser.** Open [helixscreen.org/flash](https://helixscreen.org/flash/) in desktop Chrome or Edge, plug in the K-Touch with a USB-C data cable, and click **Install**. Tick **Erase device** for a first install over BigTreeTech's firmware; leave it unticked when updating to keep your WiFi and settings. Then continue at [First boot](#first-boot-wifi-and-printer-setup). The rest of this page does the same thing from a terminal.

**What you need:**

- A BigTreeTech K-Touch
- A USB-C cable that carries data (some charge-only cables don't)
- A computer with Python 3 and esptool, installed with `pipx install esptool` (or `pip install esptool` inside a virtualenv)
- A driver for the K-Touch's USB chip (a CH340). Linux has it built in and the panel shows up as `/dev/ttyUSB0`. On Windows, if no new COM port appears when you plug the panel in, install the CH340 driver from WCH. Recent macOS versions include one; the panel appears as `/dev/cu.usbserial-*` or `/dev/cu.wchusbserial*`

**Download.** On the [releases page](https://github.com/prestonbrown/helixscreen/releases), pick a 1.1 beta or newer and download `helixscreen-esp32-ktouch-v<VERSION>.zip`. Unzip it and open a terminal inside the folder it creates. The commands below use `PORT` for the panel's port: `/dev/ttyUSB0` on Linux, `/dev/cu.usbserial-...` on macOS, or `COM3` (whatever Device Manager shows under **Ports**) on Windows.

**Optional: back up the stock firmware first.** This saves the whole 16MB flash, so you can put BigTreeTech's firmware back later. It takes a few minutes. Keep reads at 460800 or lower: the CH340 drops bytes at 921600.

```bash
python3 -m esptool --chip esp32s3 -p PORT -b 460800 read_flash 0 0x1000000 ktouch-stock-backup.bin
```

## Installing

Plug the K-Touch into your computer, switch it on, and flash the factory image:

```bash
python3 -m esptool --chip esp32s3 -p PORT -b 460800 write_flash 0x0 helixscreen-esp32-ktouch-factory.bin
```

If the write fails part way, run the same command again with `-b 115200`. Newer esptool versions print "Deprecated" warnings about `write_flash`; those are harmless.

When it finishes, the panel restarts into HelixScreen.

## First boot: WiFi and printer setup

The K-Touch has no setup wizard. On first boot it creates its own WiFi hotspot and shows a **Set Up WiFi** message with the hotspot's name (`HelixScreen-` plus four characters, like `HelixScreen-8CFC`) and a web address.

1. On your phone or computer, join the `HelixScreen-XXXX` network. It has no password.
2. Open the address shown on the panel (normally `http://192.168.4.1`). Many phones open the setup page on their own as soon as they join.
3. Fill in the form:
   - **WiFi network**: pick your network from the list or type its name
   - **WiFi password**
   - **Moonraker host**: your printer's IP address, for example `192.168.1.50`
   - **Moonraker port**: leave at `7125` unless you've changed it
4. Tap **Connect**. The panel joins your network, the hotspot shuts down, and HelixScreen connects to your printer.

If the panel can't join, the page shows why (a wrong password, for example) and you can try again. The hotspot closes on its own after 10 minutes; restart the panel to bring it back.

Prefer to do it on the panel itself? Tap **Use Settings Instead**, then join your network in **Settings > Connection > Network Settings** and enter your printer's address in **Settings > Connection > Host**. That's also where you change either one later.

## Updating

There are no over-the-air updates yet, and the panel won't offer you one. To update, download the new zip, unzip it, and from inside that folder run:

```bash
python3 -m esptool --chip esp32s3 -p PORT -b 460800 write_flash @flash_args
```

This writes the new firmware and keeps your WiFi network and settings. It has to be run from inside the unzipped folder, because `flash_args` lists the other files by name.

The factory-image command from [Installing](#installing) also works for an update, but it forgets your WiFi network, so you'll go through the hotspot setup again. Your settings are kept either way.

## Recovery

If the panel won't start, or keeps restarting, erase it completely and install fresh:

```bash
python3 -m esptool --chip esp32s3 -p PORT erase_flash
python3 -m esptool --chip esp32s3 -p PORT -b 460800 write_flash 0x0 helixscreen-esp32-ktouch-factory.bin
```

Erasing removes everything, including your settings and WiFi, so you'll set the panel up again from the hotspot.

To go back to BigTreeTech's firmware, write the backup you made: `python3 -m esptool --chip esp32s3 -p PORT -b 460800 write_flash 0x0 ktouch-stock-backup.bin`.

## What doesn't work yet on the K-Touch

- The camera feed and QR code features
- The 2D G-code view and the 3D bed mesh view
- Chinese and Japanese (the panel has no room for their font)
- Over-the-air updates

Everything else in this alpha is listed under [ESP32-S3 Firmware](beta-features.md#esp32-s3-firmware-alpha) in the beta features guide.

## Other ESP32-S3 panels

Only the K-Touch is tested and supported. The firmware will not run on a different panel as-is, even one with the same chip, because the screen and touch wiring differ from board to board.

A panel is a candidate for a port if it has:

- An ESP32-S3 with 16MB of flash and 8MB of octal PSRAM
- An 800x480 screen driven over a 16-bit RGB parallel interface (RGB565)
- A GT911 touch controller on I2C

Bringing one up means describing its pins and screen timings in a new board file next to the K-Touch's and building from source. Developers can start in the [firmware directory](https://github.com/prestonbrown/helixscreen/tree/main/firmware/helixscreen-esp32).

## See also

- [Beta Features: ESP32-S3 Firmware](beta-features.md#esp32-s3-firmware-alpha): what the alpha includes
- [Installation overview](../INSTALL.md): all other platforms
