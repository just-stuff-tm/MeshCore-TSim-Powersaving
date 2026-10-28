# EasySkyMesh-TSim

EasySkyMesh power-saving MeshCore with a small T-SIM delta on top. Upstream power saving is used as is (not re-ported).

## What this repo adds (branch `tsim`)
- **serial.ondemand** (`src/helpers/SerialWake.*`, hooks in `ESP32Board.h` and `CommonCLI.cpp`): UART0 RX wakes the ESP32 from power-saving light sleep and keeps it awake 15 s after the last byte, so the serial CLI works while power saving is on. `set serial.ondemand on|off` (default on, stored in NVS), `get serial.ondemand`. The byte that wakes the board is lost: send CR/LF, wait ~100 ms, then the command.
- **Screenless Heltec V3 envs** (`variants/heltec_v3_headless`): `Heltec_v3_repeater_headless`, `Heltec_v3_room_server_headless`, `Heltec_v3_companion_radio_usb_headless`, `Heltec_v3_companion_radio_ble_headless`. No display; the OLED is held in reset from boot.
- **isReadBusy()** for the Ethernet (CH390/RAK13800) and RP2040 BLE serial interfaces, and a guard for meshsolar's `NONE` macro.
- **`tsim wake`** (CLI, serial or remote admin; ESP32 with the console on UART0). See below.

## `tsim wake [status|modem_reset|<word>]`
Wake-then-command, for a LilyGO T-SIM wired Heltec TX43 → 1N5817 → T-SIM GPIO6 (100 nF to GND, internal pull-up, ext1 any-low wake). CLI over serial or remote admin; ESP32 with the console on UART0 (Heltec V3). Works with serial.ondemand on or off.
1. Flushes the console and inverts UART0 TX inside the UART (`uart_set_line_inverse`) 3 × 20 ms, 100 ms apart. GPIO43 never leaves the UART, so the console keeps working.
2. Replies at once: `OK - tsim wake STATUS seq 4711: woke, sending; result in ~8 s (tsim last)`.
3. After 1 s (T-SIM boot) a small task sends `@TSIM <CMD> <seq>\r\n` on the normal serial link (Heltec TX → T-SIM GPIO41, 115200), every 1.5 s, up to 5 times, until the T-SIM answers `@TSIM ACK <seq>` on the console (handled silently, no reply line). The board stays out of light sleep meanwhile.
4. Prints `TSIM-WAKE: STATUS seq 4711: ACK after 1320 ms (1 sent)` or `... no ACK after 5 tries ...`; `tsim last` shows the same line (use it over remote admin).
- CMD: bare `tsim wake` sends `WAKE`; `status` → `STATUS`, `modem_reset` → `MODEM_RESET`; any other `[A-Za-z0-9_]` word up to 15 chars is upper-cased and sent (the T-SIM's allowlist decides). seq starts random at boot and counts up.
- Build-time knobs: `TSIM_WAKE_PULSES`, `TSIM_WAKE_LOW_MS`, `TSIM_WAKE_GAP_MS`, `TSIM_WAKE_BOOT_MS`, `TSIM_WAKE_RETRY_MS`, `TSIM_WAKE_TRIES`.
- The repeater echoes serial input, so the T-SIM sees its own `@TSIM ACK` echoed back; it must ignore that.

**Field note (Oct 2026):** the installed treenode wire shows 0 GPIO6 pulses, so this can't wake that T-SIM until the wire is fixed on site. **T-SIM side:** not released; T-SIM v1.7.6 treats any GPIO6 wake as a Heltec reboot. Design: [tsim-remote-updater `design/tsim-wake-frames`](https://github.com/just-stuff-tm/tsim-remote-updater/blob/design/tsim-wake-frames/docs/tsim-wake-frames.md).

## Release automation
`.github/workflows/easyskymesh-sync.yml`, daily at 11:23 UTC and on demand (Actions → Run workflow; inputs: release, force, envs regex).
- Reads the newest IoTThinks/EasySkyMesh release and the `IoTThinks/MeshCore/tree/<branch>` it names. If that branch head differs from `.tsim/upstream.sha`, `.tsim/sync.sh` re-applies our delta (diff from `.tsim/upstream.sha` to `tsim`, outside `.github`/`.tsim`) 3-way onto it and commits it as one commit on `tsim` (upstream workflow files are not imported, so the Actions token can push it), pushed first to `sync/<release>-<sha>`.
- Builds envs matching `repeater|room_server|^heltec_v3` (about 260) in up to 20 parallel jobs.
- Publishes only if `Heltec_v3_repeater` and `Heltec_v3_repeater_headless` built (plain + merged) and at least half the envs built; marks it Latest, then fast-forwards `tsim`. Release notes list not-built envs by family.
- Conflicts or failures: nothing is published, the run fails, and an issue is opened.
- Firmware pushed to `tsim` without an upstream change is released on the next daily run (doc-only changes are skipped).

Tag and version: `<MeshCore base>-ps<NN>-tsim-<commit>`, e.g. `v1.18.0-ps18-tsim-abc1234` (also what `ver` shows). Files: `<env>-<version>.bin` (update, keeps identity), `<env>-<version>-merged.bin` (fresh install), nRF52 `.zip`/`.uf2`, plus `SHA256SUMS.txt`.

Predecessor: just-stuff-tm/MeshCore (stock MeshCore + our power-saving port), archived read-only; its releases stay downloadable.
