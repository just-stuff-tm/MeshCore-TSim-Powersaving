**MeshCore-TSim-Powersaving18.2**: EasySkyMesh PowerSaving18 with a serial console that works during power saving, for a Heltec V3 repeater controlled by a LilyGO T-SIM ([tsim-remote-updater](https://github.com/just-stuff-tm/tsim-remote-updater)).

**Based on**
- EasySkyMesh PowerSaving18, source [IoTThinks/MeshCore `PowerSaving-v18`](https://github.com/IoTThinks/MeshCore/tree/PowerSaving-v18) at `3957b6f4` (Nov 20, 2026). It includes the fixes added since the PowerSaving18 release, such as "Fix SX1262 RXPS post-preamble timer quirk". MeshCore base v1.18.0.
- Power saving is unchanged from EasySkyMesh.

**Added**
- Serial-in-PowerSaving: serial input on UART0 wakes the board from light sleep. It stays awake for 15 s after the last byte, then sleeps again. Send a CR or LF first (the byte that wakes it is lost), wait about 100 ms, then send the command.
- `set serial.ondemand on|off` and `get serial.ondemand`. Default **on**, stored in NVS, kept across reboots and updates.
- The stay-awake timer measures elapsed time, so it keeps working after 24.8 days of uptime.
- `Heltec_v3_repeater_headless`: no OLED (held in reset from boot).

**Removed compared with 18.1**
- The `tsim wake` / `tsim last` T-SIM command and its `@TSIM` serial frames.
- The radio-health watchdog.
- The other boards. This release has the Heltec V3 repeater only; 18.1 is still available for other boards.

**Files**
- `Heltec_v3_repeater-…-upgrade.bin` / `Heltec_v3_repeater_headless-…-upgrade.bin`: app-only update. It keeps the node's identity and settings. Use this file for an OTA from the T-SIM dashboard.
- `…-freshInstall-merged.bin`: a full image for a fresh USB install. It erases identity and settings.
- `SHA256SUMS.txt`: checksums.

**How to flash:** use the T-SIM dashboard (Heltec update), or https://flasher.meshcore.io → Custom Firmware over USB. After flashing, `get serial.ondemand` should answer `> on`.
