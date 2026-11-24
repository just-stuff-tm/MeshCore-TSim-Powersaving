#pragma once
// Serial-in-PowerSaving (MeshCore-TSim-Powersaving)
// Keeps the UART serial CLI usable while ESP32 PowerSaving light-sleeps, so a controller on the
// serial port (e.g. a LilyGO T-SIM running tsim-remote-updater) can always reach the CLI:
//  - UART0 RX wakes the MCU (send a bare CR or LF first: the byte that wakes the MCU is lost,
//    then wait ~100 ms and send the command),
//  - the board stays awake SERIAL_WAKE_HOLD_MS after the last received byte, then sleeps again,
//  - CLI: "set serial.ondemand on|off" (default on), "get serial.ondemand".
//    Stored in NVS (namespace "serialwake"): survives reboots and updates, prefs layout unchanged.
// Power saving itself is unchanged: with no serial traffic the board sleeps exactly as EasySkyMesh.

namespace SerialWake {
  bool handleCommand(const char* command, char* reply);  // true = handled
  bool beforeSleep();                                     // true = stay awake (skip this sleep)
  void afterSleep();
}
