// ============================================================================
// powered_simplex — mains-powered node, always on, report-only
// ============================================================================
//
// Lifecycle:
//   1. setup() runs once: bring up peripherals and the radio.
//   2. loop() runs forever:
//        a. pollEvent()         — YOU fill in. Return true + a JSON body if
//                                 something happened right now (motion, door
//                                 opened, button pressed, leak detected).
//        b. pollPeriodicState() — YOU fill in. Return true + a JSON body on
//                                 a fixed cadence (once per minute temp read,
//                                 power meter sample, etc.).
//
// Why this shape:
//   Mains-powered nodes don't need to hoard power, so they can loop
//   continuously and react instantly. Splitting "events" (edge-triggered,
//   sent the moment they happen) from "state" (periodic) keeps the payloads
//   meaningful and lets HA treat them as different entity kinds — a motion
//   event vs a temperature sensor, for example.
//
// What to customize:
//   - setupPeripherals(), pollEvent(), pollPeriodicState()
//   - Intervals via defines at the top
//
// Example devices:
//   PIR + DS18B20     — motion event, periodic temperature state.
//   Mailbox camera    — door-open event, periodic battery voltage state.
//   Power monitor     — overcurrent event, periodic watt-hours state.
// ============================================================================

#include <Arduino.h>
#include "rylr.h"

#ifndef STATE_INTERVAL_S
#define STATE_INTERVAL_S 60
#endif

static uint32_t      g_seq            = 0;
static unsigned long g_next_state_due = 0;

// One-time hardware init.
void setupPeripherals() {
    // TODO: pinMode(), sensor begin(), etc.
}

// Called every loop iteration. Return true if an event happened *right now*
// and fill `out` with the JSON body. Return false to skip.
//
// Use this for edge-triggered things you want reported immediately:
// button presses, PIR trips, door contacts, leak detectors.
bool pollEvent(char* out, size_t cap) {
    // TODO: read your input, detect a change, write JSON on change.
    // Example:
    //   int pir = digitalRead(PIR_PIN);
    //   if (pir != g_last_pir) {
    //       g_last_pir = pir;
    //       snprintf(out, cap, "{\"motion\":%s}", pir ? "true" : "false");
    //       return true;
    //   }
    (void)out; (void)cap;
    return false;
}

// Called on a fixed cadence (every STATE_INTERVAL_S). Return true and fill
// `out` with the periodic state reading.
//
// Use this for sensors that you want sampled on a schedule: temperature,
// humidity, battery voltage, power consumption.
bool pollPeriodicState(char* out, size_t cap) {
    // TODO: read your periodic sensor and write JSON.
    // Example:
    //   tempSensor.requestTemperatures();
    //   snprintf(out, cap, "{\"tc\":%.2f}", tempSensor.getTempCByIndex(0));
    //   return true;
    (void)out; (void)cap;
    return false;
}

void setup() {
    Serial.begin(115200);
    setupPeripherals();
    rylr::begin();
    g_next_state_due = millis() + (unsigned long)STATE_INTERVAL_S * 1000UL;
}

void loop() {
    char data[160];

    if (pollEvent(data, sizeof(data))) {
        rylr::send(g_seq++, "event", data);
    }

    if ((long)(millis() - g_next_state_due) >= 0) {
        if (pollPeriodicState(data, sizeof(data))) {
            rylr::send(g_seq++, "state", data);
        }
        g_next_state_due = millis() + (unsigned long)STATE_INTERVAL_S * 1000UL;
    }

    delay(50);
}
