#include "SerialWake.h"
#include <Arduino.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#if defined(ESP32) && !(defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT)
  #define SERIAL_WAKE_UART 1   // Serial is UART0 (not native USB CDC)
#endif

#ifndef SERIAL_WAKE_HOLD_MS
  #define SERIAL_WAKE_HOLD_MS 15000   // stay awake this long after the last received byte
#endif
#ifndef SERIAL_WAKE_GRACE_MS
  #define SERIAL_WAKE_GRACE_MS 1000   // after a UART wakeup, wait this long for the real data
#endif
#ifndef SERIAL_WAKE_THRESHOLD
  #define SERIAL_WAKE_THRESHOLD 3     // RX edges needed to wake; one CR or LF is enough
#endif

#ifdef SERIAL_WAKE_UART
#include <Preferences.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#if __has_include(<soc/uart_channel.h>)
  #include <soc/uart_channel.h>
#endif
#if __has_include(<soc/uart_pins.h>)
  #include <soc/uart_pins.h>
#endif

static int8_t s_enabled = -1;              // -1 = not loaded from NVS yet
static bool s_inited = false;
static volatile uint32_t s_awake_until = 0;

static bool swEnabled() {
  if (s_enabled < 0) {
    Preferences p;
    s_enabled = 1;  // default on
    if (p.begin("serialwake", true)) { s_enabled = p.getUChar("off", 0) ? 0 : 1; p.end(); }
  }
  return s_enabled == 1;
}

static void swSave(bool on) {
  Preferences p;
  if (p.begin("serialwake", false)) { p.putUChar("off", on ? 0 : 1); p.end(); }
  s_enabled = on ? 1 : 0;
}

// "tsim wake [cmd]": wake a LilyGO T-SIM wired TX43 -> 1N5817 -> GPIO6 (+100 nF to GND, pull-up;
// ext1 any-low wake), then hand it a command over the normal serial link (Heltec TX -> T-SIM RX).
//  1. UART0 TX is inverted in the UART itself (uart_set_line_inverse) TSIM_WAKE_PULSES times for
//     TSIM_WAKE_LOW_MS: the pin never leaves the UART, so the console keeps working.
//  2. After TSIM_WAKE_BOOT_MS (T-SIM boot), a framed line "@TSIM <CMD> <seq>\r\n" is sent, repeated
//     every TSIM_WAKE_RETRY_MS until the T-SIM answers "@TSIM ACK <seq>" on the console (handled
//     below, silently) or TSIM_WAKE_TRIES are used up. The result is printed and kept for "tsim last".
// CMD: WAKE (bare "tsim wake"), STATUS, MODEM_RESET, or another word [A-Z0-9_] (max 15 chars);
// the T-SIM decides what it accepts. seq starts random at boot, so the T-SIM can dedupe across reboots.
#ifndef TSIM_WAKE_PULSES
  #define TSIM_WAKE_PULSES 3
#endif
#ifndef TSIM_WAKE_LOW_MS
  #define TSIM_WAKE_LOW_MS 20      // each low pulse (T-SIM: < 2 s, its "stuck low" limit)
#endif
#ifndef TSIM_WAKE_GAP_MS
  #define TSIM_WAKE_GAP_MS 100     // high between pulses (100 nF recharges through the pull-up in ~15 ms)
#endif
#ifndef TSIM_WAKE_BOOT_MS
  #define TSIM_WAKE_BOOT_MS 1000   // first frame after this
#endif
#ifndef TSIM_WAKE_RETRY_MS
  #define TSIM_WAKE_RETRY_MS 1500
#endif
#ifndef TSIM_WAKE_TRIES
  #define TSIM_WAKE_TRIES 5        // ~7 s total
#endif
static volatile bool s_tw_busy = false;
static volatile uint16_t s_tw_seq = 0, s_tw_acked = 0;
static char s_tw_cmd[16] = "";
static char s_tw_last[80] = "none yet";

static void twTask(void*) {
  uint32_t t0 = millis(); char line[40]; bool ok = false; int tries = 0;
  vTaskDelay(pdMS_TO_TICKS(TSIM_WAKE_BOOT_MS));
  for (tries = 1; tries <= TSIM_WAKE_TRIES && !ok; tries++) {
    int n = snprintf(line, sizeof(line), "@TSIM %s %u\r\n", s_tw_cmd, (unsigned)s_tw_seq);
    Serial.write((const uint8_t*)line, n);   // one write: not split by console echo
    for (uint32_t w = 0; w < TSIM_WAKE_RETRY_MS && !ok; w += 50) { vTaskDelay(pdMS_TO_TICKS(50)); ok = s_tw_acked == s_tw_seq; }
  }
  if (ok) snprintf(s_tw_last, sizeof(s_tw_last), "%s seq %u: ACK after %u ms (%d sent)", s_tw_cmd, (unsigned)s_tw_seq, (unsigned)(millis() - t0), tries - 1);
  else snprintf(s_tw_last, sizeof(s_tw_last), "%s seq %u: no ACK after %d tries (T-SIM asleep, wire, or old firmware)", s_tw_cmd, (unsigned)s_tw_seq, TSIM_WAKE_TRIES);
  Serial.print("TSIM-WAKE: "); Serial.println(s_tw_last);
  s_tw_busy = false;
  vTaskDelete(NULL);
}

static bool twStart(const char* arg, char* reply) {
  char c[16]; int n = 0;
  if (!*arg) strcpy(c, "WAKE");
  else {
    for (; arg[n] && arg[n] != ' '; n++) {
      char ch = arg[n];
      if (n >= 15 || !(isalnum((unsigned char)ch) || ch == '_')) { strcpy(reply, "Error: usage tsim wake [status|modem_reset|<word>]"); return false; }
      c[n] = toupper((unsigned char)ch);
    }
    c[n] = 0;
  }
  if (s_tw_seq == 0) s_tw_seq = (uint16_t)(esp_random() % 60000) + 1;
  else s_tw_seq = s_tw_seq % 65000 + 1;
  strcpy(s_tw_cmd, c);
  Serial.flush();                    // nothing in flight while TX is inverted
  for (int i = 0; i < TSIM_WAKE_PULSES; i++) {
    uart_set_line_inverse(UART_NUM_0, UART_SIGNAL_TXD_INV);    // idle TX = low
    delay(TSIM_WAKE_LOW_MS);
    uart_set_line_inverse(UART_NUM_0, UART_SIGNAL_INV_DISABLE); // back to normal (idle high)
    if (i + 1 < TSIM_WAKE_PULSES) delay(TSIM_WAKE_GAP_MS);
  }
  s_tw_busy = true;
  if (xTaskCreate(twTask, "tsimwake", 3072, NULL, 1, NULL) != pdPASS) { s_tw_busy = false; strcpy(reply, "Error: no memory for tsim wake"); return false; }
  sprintf(reply, "OK - tsim wake %s seq %u: woke, sending; result in ~%u s (tsim last)", c, (unsigned)s_tw_seq,
          (unsigned)((TSIM_WAKE_BOOT_MS + TSIM_WAKE_TRIES * TSIM_WAKE_RETRY_MS) / 1000));
  return true;
}

static void swOnRx() { s_awake_until = millis() + SERIAL_WAKE_HOLD_MS; }

static void swInit() {
  s_inited = true;
  Serial.onReceive(swOnRx);
  uart_set_wakeup_threshold(UART_NUM_0, SERIAL_WAKE_THRESHOLD);
#if defined(UART_NUM_0_RXD_DIRECT_GPIO_NUM)
  gpio_pullup_en((gpio_num_t)UART_NUM_0_RXD_DIRECT_GPIO_NUM);  // no floating RX = no false wakeups
#elif defined(U0RXD_GPIO_NUM)
  gpio_pullup_en((gpio_num_t)U0RXD_GPIO_NUM);
#endif
}
#endif

namespace SerialWake {

bool handleCommand(const char* command, char* reply) {
  if (strncmp(command, "@TSIM ACK ", 10) == 0) {   // the T-SIM's answer on the console: no reply
#ifdef SERIAL_WAKE_UART
    s_tw_acked = (uint16_t)atoi(command + 10);
#endif
    reply[0] = 0; return true;
  }
  if (strcmp(command, "tsim wake") == 0 || strncmp(command, "tsim wake ", 10) == 0) {
#ifdef SERIAL_WAKE_UART
    if (s_tw_busy) { strcpy(reply, "Error: tsim wake already running (tsim last)"); return true; }
    twStart(command[9] ? command + 10 : "", reply);
#else
    strcpy(reply, "Error: tsim wake needs an ESP32 with the serial console on UART0");
#endif
    return true;
  }
  if (strcmp(command, "tsim last") == 0) {
#ifdef SERIAL_WAKE_UART
    snprintf(reply, 100, "> %s%s", s_tw_busy ? "(running) " : "", s_tw_last);
#else
    strcpy(reply, "> n/a");
#endif
    return true;
  }
  if (strncmp(command, "set serial.ondemand ", 20) == 0) {
    const char* v = command + 20;
#ifdef SERIAL_WAKE_UART
    if (strncmp(v, "on", 2) == 0) { swSave(true); strcpy(reply, "OK - serial.ondemand on (UART wakes the board during powersaving)"); }
    else if (strncmp(v, "off", 3) == 0) { swSave(false); strcpy(reply, "OK - serial.ondemand off (stock powersaving)"); }
    else strcpy(reply, "Error: use on or off");
#else
    (void)v;
    strcpy(reply, "Error: not needed on this board (serial is always serviced)");
#endif
    return true;
  }
  if (strcmp(command, "get serial.ondemand") == 0) {
#ifdef SERIAL_WAKE_UART
    strcpy(reply, swEnabled() ? "> on" : "> off");
#else
    strcpy(reply, "> on (always)");
#endif
    return true;
  }
  return false;
}

bool beforeSleep() {
#ifdef SERIAL_WAKE_UART
  if (s_tw_busy) return true;   // tsim wake waiting for the T-SIM's ACK
  if (!swEnabled()) {
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_UART);
    return false;
  }
  if (!s_inited) swInit();
  if (Serial.available() > 0 || (int32_t)(s_awake_until - millis()) > 0) return true;  // CLI in use
  Serial.flush();                    // let the last reply finish sending
  esp_sleep_enable_uart_wakeup(0);   // UART0 RX wakes the MCU
#endif
  return false;
}

void afterSleep() {
#ifdef SERIAL_WAKE_UART
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UART) {
    uint32_t t = millis() + SERIAL_WAKE_GRACE_MS;
    if ((int32_t)(t - s_awake_until) > 0) s_awake_until = t;
  }
#endif
}

}  // namespace SerialWake
