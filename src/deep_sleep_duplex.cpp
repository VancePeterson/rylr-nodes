// ============================================================================
// deep_sleep_duplex — battery node, deep sleep, report + command
// ============================================================================
//
// Lifecycle:
//   1. Wake from deep sleep.
//   2. rylr::begin() brings up the radio.
//   3. readState() — YOU fill this in — produces the current state JSON.
//   4. Send it as a "state" envelope.
//   5. Keep the radio on for LISTEN_WINDOW_MS, polling for incoming +RCV
//      lines. Any addressed "set" command is handed to applyCommand().
//   6. After applyCommand() acts, we send an "ack" envelope that echoes the
//      command's sequence number and the new state.
//   7. Park the radio and deep-sleep until the next wake.
//
// Why this shape:
//   A sleeping node has its radio off and CAN'T be reached. The standard
//   trick is to have the node wake on schedule, report, and hold the radio
//   on briefly so HA gets a chance to push a command. Commands aren't
//   instant — they're bounded by WAKE_INTERVAL_S.
//
//   This is the right pattern for anything you don't need to control
//   instantly: latching valves, thermostat setpoints, schedule changes.
//   Not for "turn on the floodlight NOW."
//
//   State that must survive deep sleep (current position, last-seen command
//   seq, etc.) lives in RTC RAM via RTC_DATA_ATTR.
//
// What to customize:
//   - applyHardwareState() / readState() / applyCommand()
//   - WAKE_INTERVAL_S and LISTEN_WINDOW_MS via platformio.ini
//   - Any RTC_DATA_ATTR state you need to persist
//
// Example devices:
//   Latching solenoid valve — two coils, 40 ms pulse per state change.
//   Battery thermostat       — setpoint in RTC RAM, relay driven on wake.
//   Window blinds motor      — position in RTC RAM, stepper on wake.
// ============================================================================

#include <Arduino.h>
#include "rylr.h"

#ifndef WAKE_INTERVAL_S
#define WAKE_INTERVAL_S 600
#endif
#ifndef LISTEN_WINDOW_MS
#define LISTEN_WINDOW_MS 15000
#endif

// Persisted across deep sleep.
RTC_DATA_ATTR uint32_t g_seq = 0;

// TODO: add any state your device needs to remember across sleep. Examples:
//   RTC_DATA_ATTR bool g_valve_open = false;
//   RTC_DATA_ATTR int  g_setpoint   = 20;

// One-time-per-wake peripheral init.
void setupPeripherals() {
    // TODO: pinMode(), Wire.begin(), etc.
}

// Write the current device state as a JSON object body. Goes into "d" of
// both "state" and "ack" envelopes.
void readState(char* out, size_t cap) {
    // TODO: produce JSON describing the current state.
    // Example:  snprintf(out, cap, "{\"open\":%s}", g_valve_open ? "true" : "false");
    snprintf(out, cap, "{}");
}

// Act on an incoming command. `payload` is the full envelope JSON, e.g.
//   {"n":4,"s":42,"t":"set","d":{"open":true}}
// The top-level "n" / "t" have already been validated for you before this
// is called. Use rylr::json::findInt / findBool / etc. to pull fields out
// of the "d" object.
void applyCommand(const char* payload) {
    // TODO: parse command fields and drive the hardware.
    // Example:
    //   bool want = rylr::json::findBool(payload, "open");
    //   if (want != g_valve_open) pulseValve(want);
    (void)payload;
}

void reportState(const char* type, long for_seq) {
    char data[160];
    char body[128];
    readState(body, sizeof(body));
    // Strip the outer braces from readState() so we can inject "for"/"ok"
    // alongside the user fields for ack envelopes.
    if (for_seq < 0) {
        rylr::send(g_seq++, type, body);
    } else {
        // body looks like "{...}" — slice the inner content.
        size_t blen = strlen(body);
        const char* inner = (blen >= 2) ? body + 1 : "";
        size_t inner_len = (blen >= 2) ? blen - 2 : 0;
        snprintf(data, sizeof(data),
                 "{\"for\":%ld,\"ok\":true%s%.*s}",
                 for_seq, inner_len ? "," : "",
                 (int)inner_len, inner);
        rylr::send(g_seq++, type, data);
    }
}

void handleIncoming(const char* payload) {
    if (!rylr::json::addressedTo(payload, LORA_ADDRESS)) return;
    if (!rylr::json::typeEquals(payload, "set")) return;

    long cmd_seq = 0;
    rylr::json::findInt(payload, "s", &cmd_seq);

    applyCommand(payload);
    reportState("ack", cmd_seq);
}

void setup() {
    Serial.begin(115200);
    delay(50);

    setupPeripherals();
    rylr::begin();
    reportState("state", -1);

    // Listen window — stays awake briefly so HA can push a command.
    char payload[256];
    unsigned long deadline = millis() + LISTEN_WINDOW_MS;
    while ((long)(deadline - millis()) > 0) {
        if (rylr::receive(payload, sizeof(payload), deadline - millis())) {
            handleIncoming(payload);
        }
    }

    rylr::sendAT("AT+MODE=1");
    esp_sleep_enable_timer_wakeup((uint64_t)WAKE_INTERVAL_S * 1000000ULL);
    esp_deep_sleep_start();
}

void loop() {}
