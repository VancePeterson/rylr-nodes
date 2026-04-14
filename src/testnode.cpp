// testnode — bench / range test node.
//
// Sends a heartbeat envelope on a fixed interval. No sensors, no commands,
// just proof that the gateway is hearing this node. Use it to verify AT+CPIN
// encryption, check range, and sanity-check the MQTT → HA plumbing before
// you wire up a real sensor.
//
// Envelope: {"n":N,"s":X,"t":"hb","d":{}}

#include <Arduino.h>
#include "rylr.h"

#ifndef HEARTBEAT_INTERVAL_S
#define HEARTBEAT_INTERVAL_S 60
#endif

static uint32_t g_seq = 0;

void setup() {
    Serial.begin(115200);
    rylr::begin();
}

void loop() {
    rylr::send(g_seq++, "hb", "{}");
    delay((unsigned long)HEARTBEAT_INTERVAL_S * 1000UL);
}
