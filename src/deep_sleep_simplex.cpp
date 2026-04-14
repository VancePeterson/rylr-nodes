// ============================================================================
// deep_sleep_simplex — battery node, deep sleep, report-only
// ============================================================================
//
// Lifecycle:
//   1. ESP32 wakes from deep sleep (first boot = wake too).
//   2. rylr::begin() brings up the RYLR998 and applies AT+ADDRESS/NETWORKID/etc.
//   3. readState() — YOU fill this in — produces one JSON object describing
//      the current reading, e.g. {"tc":21.4,"rh":55.1}.
//   4. We wrap it in an envelope and ship it to the gateway.
//   5. Radio is parked in sleep mode, then the MCU deep-sleeps for
//      WAKE_INTERVAL_S seconds.
//
// Why this shape:
//   A deep-sleep node is off more than it is on, so all the work has to happen
//   in setup(). loop() never runs — esp_deep_sleep_start() resets the chip and
//   setup() starts again on the next wake.
//
//   g_seq lives in RTC RAM so it survives deep sleep. HA sees a monotonic
//   counter across thousands of cycles, which is useful for detecting dropped
//   frames.
//
// What to customize:
//   - readState(): initialize and read your sensor(s), format JSON.
//   - WAKE_INTERVAL_S:  how often to wake (build flag in platformio.ini).
//   - setupPeripherals(): one-time hardware init that has to happen every wake.
//
// Example payloads by sensor type:
//   BME280:       {"tc":21.4,"rh":55.1,"p":1013.2}
//   Soil probe:   {"moist":0.42,"vbat":3.74}
//   Door reed:    {"open":true}
// ============================================================================

#include <Arduino.h>
#include "rylr.h"

#ifndef WAKE_INTERVAL_S
#define WAKE_INTERVAL_S 300
#endif

// Persisted across deep sleep so the sequence number keeps climbing.
RTC_DATA_ATTR uint32_t g_seq = 0;

// One-time-per-wake peripheral init. Called before readState().
// Put Wire.begin(), your sensor's begin(), ADC setup, etc. here.
void setupPeripherals() {
    // TODO: initialize your sensor
    // e.g.  Wire.begin();
    //       bme.begin(0x76);
}

// Produce one JSON object body describing the current reading. The result
// becomes the "d" field of the state envelope. Keep keys short — the whole
// envelope needs to fit in ~240 bytes for AT+SEND.
void readState(char* out, size_t cap) {
    // TODO: read your sensor(s) and write JSON into `out`.
    // Example:
    //   float tc = bme.readTemperature();
    //   float rh = bme.readHumidity();
    //   snprintf(out, cap, "{\"tc\":%.2f,\"rh\":%.1f}", tc, rh);
    snprintf(out, cap, "{}");
}

void setup() {
    Serial.begin(115200);
    delay(50);

    setupPeripherals();
    rylr::begin();

    char data[160];
    readState(data, sizeof(data));
    rylr::send(g_seq++, "state", data);

    // Park the RYLR998 in low-power mode before the MCU sleeps. The module
    // wakes automatically the next time we push a byte on UART.
    rylr::sendAT("AT+MODE=1");

    esp_sleep_enable_timer_wakeup((uint64_t)WAKE_INTERVAL_S * 1000000ULL);
    esp_deep_sleep_start();
}

// Never reached — deep sleep resets the chip, setup() runs again on wake.
void loop() {}
