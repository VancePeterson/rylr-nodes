// ============================================================================
// powered_duplex — mains-powered node, always on, report + command
// ============================================================================
//
// Lifecycle:
//   1. setup() initializes hardware and the radio, then sends one initial
//      "state" envelope so HA has a known starting value.
//   2. loop() runs forever:
//        a. Poll the UART for incoming commands. Anything addressed to us
//           with type "set" is handed to applyCommand(), followed by an
//           "ack" + fresh "state" envelope.
//        b. On HEARTBEAT_INTERVAL_S, send a "state" envelope even if nothing
//           changed. This keeps HA's "available" timeout from firing on
//           quiet devices.
//
// Why this shape:
//   Always-on gives you instant command response — the radio is listening
//   continuously. No wake windows, no polling compromise. The cost is power,
//   so this pattern only makes sense for mains-powered nodes.
//
//   The periodic heartbeat is important even though state rarely changes:
//   HA's expire_after on entities will mark them "unavailable" if no
//   message arrives within the timeout. A cheap once-a-minute republish
//   keeps dashboards green.
//
// What to customize:
//   - setupPeripherals(), readState(), applyCommand()
//   - HEARTBEAT_INTERVAL_S via platformio.ini
//
// Example devices:
//   N-channel relay board  — irrigation zones, outdoor lights.
//   Dimmer                 — PWM level control.
//   Garage door opener     — momentary trigger + reed switch state.
// ============================================================================

#include <Arduino.h>
#include "rylr.h"

#ifndef HEARTBEAT_INTERVAL_S
#define HEARTBEAT_INTERVAL_S 60
#endif

static uint32_t      g_seq     = 0;
static unsigned long g_next_hb = 0;

// One-time hardware init.
void setupPeripherals() {
    // TODO: pinMode(), relay init, etc.
}

// Write the current device state as a JSON object body. Used for both
// "state" envelopes and embedded inside "ack" envelopes.
void readState(char* out, size_t cap) {
    // TODO: describe the current state.
    // Example:
    //   snprintf(out, cap, "{\"ch\":[%s,%s]}",
    //            g_state[0] ? "true" : "false",
    //            g_state[1] ? "true" : "false");
    snprintf(out, cap, "{}");
}

// Act on an incoming command. `payload` is the full envelope JSON, e.g.
//   {"n":6,"s":42,"t":"set","d":{"ch":1,"on":true}}
// Top-level "n"/"t" have already been validated before this is called.
void applyCommand(const char* payload) {
    // TODO: parse command fields and drive the hardware.
    // Example:
    //   long ch = -1;
    //   rylr::json::findInt(payload, "ch", &ch);
    //   bool on = rylr::json::findBool(payload, "on");
    //   digitalWrite(RELAY_PINS[ch], on ? HIGH : LOW);
    //   g_state[ch] = on;
    (void)payload;
}

void reportState(const char* type, long for_seq) {
    char data[160];
    char body[128];
    readState(body, sizeof(body));
    if (for_seq < 0) {
        rylr::send(g_seq++, type, body);
    } else {
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
    reportState("state", -1);
}

void setup() {
    Serial.begin(115200);
    setupPeripherals();
    rylr::begin();
    reportState("state", -1);
    g_next_hb = millis() + (unsigned long)HEARTBEAT_INTERVAL_S * 1000UL;
}

void loop() {
    char payload[256];
    if (rylr::receive(payload, sizeof(payload), 100)) {
        handleIncoming(payload);
    }
    if ((long)(millis() - g_next_hb) >= 0) {
        reportState("state", -1);
        g_next_hb = millis() + (unsigned long)HEARTBEAT_INTERVAL_S * 1000UL;
    }
}
