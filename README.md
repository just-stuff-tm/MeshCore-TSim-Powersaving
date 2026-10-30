# MeshCore-TSim-Powersaving

Power-saving MeshCore repeater firmware for the boards EasySkyMesh and MeshCore support. T-SIM/LTE management (a LilyGO T-SIM7672G with [tsim-remote-updater](https://github.com/just-stuff-tm/tsim-remote-updater)) and the headless builds are Heltec V3 extras.

The tree is [EasySkyMesh](https://github.com/IoTThinks/EasySkyMesh) power-saving MeshCore, from [IoTThinks/MeshCore](https://github.com/IoTThinks/MeshCore) branch `PowerSaving-vNN`, plus the changes below. Power saving is EasySkyMesh's and is used as shipped. MeshCore's own docs are at [docs.meshcore.io](https://docs.meshcore.io) and [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore).

## Boards

[`.github/workflows/easyskymesh-sync.yml`](.github/workflows/easyskymesh-sync.yml) builds environments whose names match `repeater`, `room_server`, or `heltec_v3`:

- **ESP32** (ESP32, ESP32-C3, ESP32-S3)
- **ESP32-C6**
- **nRF52**
- **RP2040**
- **STM32**

Tested: Heltec V3 (with a LilyGO T-SIM7672G). Other boards build from the same source but are untested; reports welcome.

## Features

- **EasySkyMesh power saving.** `powersaving on` turns it on. ESP32 and ESP32-C6 repeaters light-sleep between radio work. nRF52 repeaters sleep whenever they can. On RP2040 and STM32 the flag is stored and `sleep()` does nothing.
- **serial.ondemand.** On an ESP32 whose console is UART0 (not native USB CDC), UART0 RX wakes the chip from light sleep and the board stays awake for 15 seconds after the last byte, so the serial CLI works while power saving is on. `set serial.ondemand on|off` (default on, stored in NVS) and `get serial.ondemand`. The byte that wakes the board is lost: send CR or LF, wait about 100 ms, then the command. On any other board, `set` returns `Error: not needed on this board (serial is always serviced)` and `get` prints `> on (always)`.
- **Headless Heltec V3 builds.** `Heltec_v3_repeater` keeps the OLED. `Heltec_v3_repeater_headless` is the screenless repeater: the OLED is held in reset from boot. The same headless builds exist for the room server and the USB and BLE companion radios (`Heltec_v3_room_server_headless`, `Heltec_v3_companion_radio_usb_headless`, `Heltec_v3_companion_radio_ble_headless`).
- **isReadBusy() serial fixes.** The Ethernet serial path (CH390 and RAK13800) stays busy while a frame is only partly received. RP2040 BLE implements `isReadBusy()`. A `NONE` macro from meshsolar's headers is undefined around `InterfaceType::NONE` so that enum still compiles.
- **`tsim wake` and `tsim last`.** Heltec V3 extra: wake a LilyGO T-SIM on the serial port and send it a command. The commands run on any ESP32 whose console is UART0. See below.

## `tsim wake` and `tsim last`

Serial CLI or remote admin, on an ESP32 whose console is UART0 (not native USB CDC). Heltec V3 is that case, and the wiring below is for it. Works with serial.ondemand on or off.

Wiring for the wake pulse: Heltec V3 TX (GPIO43) through a 1N5817 to T-SIM GPIO6, 100 nF from GPIO6 to ground, GPIO6 internal pull-up. The T-SIM wakes on GPIO6 going low. Commands after that use the normal serial link, Heltec TX to T-SIM RX, 115200 baud.

`tsim wake [status|modem_reset|<word>]`

1. Flushes the console, then inverts UART0 TX inside the UART three times for 20 ms, with 100 ms between pulses. The pin stays on the UART (GPIO43 on Heltec V3), so the console keeps working.
2. Replies at once, for example `OK - tsim wake STATUS seq 4711: woke, sending; result in ~8 s (tsim last)`. The `~8 s` is the 1 s boot wait plus five tries at 1.5 s. The sequence number is the one for this attempt.
3. After 1 second a task sends `@TSIM <CMD> <seq>` every 1.5 s, up to 5 times, until the T-SIM answers `@TSIM ACK <seq>`. That answer is taken silently (no reply line). The board stays out of light sleep until the attempt finishes.
4. Prints `TSIM-WAKE: <CMD> seq <n>: ACK after <ms> ms (<sent> sent)`, or `TSIM-WAKE: <CMD> seq <n>: no ACK after 5 tries (T-SIM asleep, wire, or old firmware)`. The time and the send count are measured.

`tsim last` prints that same line, prefixed with `> `. While a wake is still running it prints `> (running) ` and the last result (or `none yet` if nothing has finished). On a board whose console is not UART0, `tsim wake` returns `Error: tsim wake needs an ESP32 with the serial console on UART0` and `tsim last` prints `> n/a`.

The command word: a bare `tsim wake` sends `WAKE`; `status` sends `STATUS`; `modem_reset` sends `MODEM_RESET`. Any other word of letters, digits, and `_`, up to 15 characters, is sent in upper case. The T-SIM allowlist decides which words it accepts. The sequence number starts at a random value at boot and then counts up. A second `tsim wake` while one is running returns `Error: tsim wake already running (tsim last)`.

The repeater echoes serial input, so the T-SIM sees its own `@TSIM ACK` come back and must ignore that echo.

On the T-SIM, [tsim-remote-updater](https://github.com/just-stuff-tm/tsim-remote-updater) v1.7.7 or newer answers these frames: `WAKE` leaves the modem asleep, `STATUS` sends a status report, `MODEM_RESET` runs modem recovery. Older T-SIM firmware treats the GPIO6 pulse as a Heltec reboot and ignores `@TSIM` frames.

Build-time knobs, with those defaults: `TSIM_WAKE_PULSES` 3, `TSIM_WAKE_LOW_MS` 20, `TSIM_WAKE_GAP_MS` 100, `TSIM_WAKE_BOOT_MS` 1000, `TSIM_WAKE_RETRY_MS` 1500, `TSIM_WAKE_TRIES` 5.

## Downloads

Builds are on [Releases](https://github.com/just-stuff-tm/MeshCore-TSim-Powersaving/releases).

The version, the git tag, the release title, and the firmware `ver` string are `MeshCore-TSim-Powersaving<NN>.<n>`. Example: `MeshCore-TSim-Powersaving18.1`. `NN` is the EasySkyMesh release number (`PowerSaving18` becomes 18). `n` is this repo's build on that upstream (18.1, 18.2, and so on) and starts again at `.1` for the next PowerSaving release.

File names, EasySkyMesh style:

- `<env>-MeshCore-TSim-PowersavingNN.n-upgrade.bin` updates an ESP32 or ESP32-C6 board and keeps identity and settings.
- `<env>-MeshCore-TSim-PowersavingNN.n-freshInstall-merged.bin` is a full flash and wipes identity and settings.
- Screenless Heltec V3 repeater: env `Heltec_v3_repeater_headless`, same two suffixes.
- nRF52 builds are `<env>-MeshCore-TSim-PowersavingNN.n.uf2` and `.zip`.
- RP2040 builds are `<env>-MeshCore-TSim-PowersavingNN.n.uf2` and `.bin`.
- STM32 builds are `<env>-MeshCore-TSim-PowersavingNN.n.hex` and `.bin`.
- Each release includes `SHA256SUMS.txt`.

## How releases are built

[`.github/workflows/easyskymesh-sync.yml`](.github/workflows/easyskymesh-sync.yml) runs from Actions → Run workflow. It reads the newest EasySkyMesh release and the `IoTThinks/MeshCore` `PowerSaving-vNN` branch named in that release. When that branch has moved, [`.tsim/sync.sh`](.tsim/sync.sh) reapplies this repo's changes on it, then the workflow builds the repeater, room server, and Heltec V3 environments (the families in [Boards](#boards)). A manual run leaves a draft unless publish is selected. The daily schedule in the workflow file is commented out. Inputs, the publish checks, and the rest of the flow are in [`.tsim/README.md`](.tsim/README.md).

## Flashing

Use the MeshCore web flasher, <https://flasher.meshcore.io> (Chrome or Edge): Custom Firmware, choose the file, Flash.

- ESP32 and ESP32-C6: `-upgrade.bin` keeps identity and settings. `-freshInstall-merged.bin` erases them.
- Screenless Heltec V3: flash `Heltec_v3_repeater_headless` the same way. The OLED stays held in reset.
- nRF52 boards (RAK4631, Heltec T114, and others): the `.zip` is a DFU package (enter DFU mode first if the flasher asks). Or put the board in its bootloader and copy the `.uf2` onto the USB drive.
- RP2040: copy the `.uf2` onto the board in bootloader mode (a `.bin` is also attached).
- STM32: flash the `.hex` (a `.bin` is also attached).

After flashing, on the serial console at 115200 baud or by remote admin: `powersaving on`. On an ESP32 whose console is UART0, `get serial.ondemand` answers `on` unless it was turned off. On other boards it answers `on (always)`.

tsim-remote-updater installs `Heltec_v3_repeater_headless-…-upgrade.bin` over LTE by default (stock MeshCore is the other choice), which keeps identity and settings.

## License

MIT. The upstream copyright stays in [license.txt](license.txt): Copyright (c) 2025 Scott Powell / rippleradios.com, with the MIT permission text.

## Credits

- **EasySkyMesh** (IoTThinks), <https://github.com/IoTThinks/EasySkyMesh>. Power saving is from that project. Source: [IoTThinks/MeshCore](https://github.com/IoTThinks/MeshCore) `PowerSaving-vNN`.
- **MeshCore** (meshcore-dev/MeshCore), <https://github.com/meshcore-dev/MeshCore>. MIT, [license.txt](license.txt). Docs: <https://docs.meshcore.io>.
