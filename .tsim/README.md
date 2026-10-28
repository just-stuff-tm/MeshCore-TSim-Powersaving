# EasySkyMesh-TSim

EasySkyMesh power-saving MeshCore with a small T-SIM delta on top. Upstream power saving is used as is (not re-ported).

## What this repo adds (branch `tsim`)
- **serial.ondemand** (`src/helpers/SerialWake.*`, hooks in `ESP32Board.h` and `CommonCLI.cpp`): UART0 RX wakes the ESP32 from power-saving light sleep and keeps it awake 15 s after the last byte, so the serial CLI works while power saving is on. `set serial.ondemand on|off` (default on, stored in NVS), `get serial.ondemand`. The byte that wakes the board is lost: send CR/LF, wait ~100 ms, then the command.
- **Screenless Heltec V3 envs** (`variants/heltec_v3_headless`): `Heltec_v3_repeater_headless`, `Heltec_v3_room_server_headless`, `Heltec_v3_companion_radio_usb_headless`, `Heltec_v3_companion_radio_ble_headless`. No display; the OLED is held in reset from boot.
- **isReadBusy()** for the Ethernet (CH390/RAK13800) and RP2040 BLE serial interfaces, and a guard for meshsolar's `NONE` macro.
- **`tsim wake`** (CLI, serial or remote admin; ESP32 with the console on UART0). See below.

## `tsim wake`
1. Flushes the console, then inverts UART0 TX in the UART (`uart_set_line_inverse`) 3 times for 20 ms, 100 ms apart. The TX pin (GPIO43 on the Heltec V3) never leaves the UART, so the console keeps working. Tunable at build time: `TSIM_WAKE_PULSES`, `TSIM_WAKE_LOW_MS`, `TSIM_WAKE_GAP_MS`.
2. Prints `TSIM-WAKE: wake request from the repeater (not a reboot)` 3 times, 1.5 s apart (`TSIM_WAKE_MARKS`, `TSIM_WAKE_MARK_MS`), from a small task, and keeps the board out of light sleep until they're out.
3. Replies `OK - tsim wake: 3 x 20 ms low on UART0 TX, then marker lines`. Works with serial.ondemand on or off.

Wiring it expects on the T-SIM: Heltec TX43 → 1N5817 → T-SIM GPIO6, 100 nF to GND, internal pull-up; T-SIM ext1 any-low wake. **Field note (Oct 2026):** the installed treenode wire shows 0 GPIO6 pulses, so the command can't wake that T-SIM until the wire is fixed on site.

**T-SIM side (not released yet):** T-SIM v1.7.6 treats every GPIO6 wake as a Heltec reboot (modem reset, reboot report, counts toward 6/day). To tell the two apart, after an ext1 wake it should open the Heltec UART for ~5 s and look for `TSIM-WAKE:` (a real reboot prints the ROM banner `rst:0x..` / `Repeater ID:` instead, or nothing it can catch). On the marker: count it as a wake request (own counter), bring the modem up without the reboot recovery reset, check SMS / post a check-in, and don't count it as a reboot or a spurious wake. As a second check, `stats-core` uptime > 60 s means the Heltec did not reboot. While awake, the marker line must not match the boot-line matcher (it doesn't).

## Release automation
`.github/workflows/easyskymesh-sync.yml`, daily at 11:23 UTC and on demand (Actions → Run workflow; inputs: release, force, envs regex).
- Reads the newest IoTThinks/EasySkyMesh release and the `IoTThinks/MeshCore/tree/<branch>` it names. If that branch head differs from `.tsim/upstream.sha`, `.tsim/sync.sh` re-applies our delta (diff from `.tsim/upstream.sha` to `tsim`, outside `.github`/`.tsim`) 3-way onto it and commits it as one commit on `tsim` (upstream workflow files are not imported, so the Actions token can push it), pushed first to `sync/<release>-<sha>`.
- Builds envs matching `repeater|room_server|^heltec_v3` (about 260) in up to 20 parallel jobs.
- Publishes only if `Heltec_v3_repeater` and `Heltec_v3_repeater_headless` built (plain + merged) and at least half the envs built; marks it Latest, then fast-forwards `tsim`. Release notes list not-built envs by family.
- Conflicts or failures: nothing is published, the run fails, and an issue is opened.
- Firmware pushed to `tsim` without an upstream change is released on the next daily run (doc-only changes are skipped).

Tag and version: `<MeshCore base>-ps<NN>-tsim-<commit>`, e.g. `v1.18.0-ps18-tsim-abc1234` (also what `ver` shows). Files: `<env>-<version>.bin` (update, keeps identity), `<env>-<version>-merged.bin` (fresh install), nRF52 `.zip`/`.uf2`, plus `SHA256SUMS.txt`.

Predecessor: just-stuff-tm/MeshCore (stock MeshCore + our power-saving port), archived read-only; its releases stay downloadable.
