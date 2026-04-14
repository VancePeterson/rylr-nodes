#pragma once
#include <Arduino.h>

#ifndef LORA_ADDRESS
#define LORA_ADDRESS 2
#endif
#ifndef LORA_GATEWAY_ADDRESS
#define LORA_GATEWAY_ADDRESS 1
#endif
#ifndef LORA_CHANNEL
#define LORA_CHANNEL 6
#endif
#ifndef LORA_BAND
#define LORA_BAND 915000000
#endif
#ifndef LORA_BAUD
#define LORA_BAUD 115200
#endif

#define LORA_SERIAL Serial2
#ifndef LORA_RX_PIN
#define LORA_RX_PIN 33
#endif
#ifndef LORA_TX_PIN
#define LORA_TX_PIN 32
#endif

namespace rylr {

inline bool waitForOK(unsigned long timeout_ms = 2000) {
    unsigned long start = millis();
    String resp;
    while (millis() - start < timeout_ms) {
        while (LORA_SERIAL.available()) {
            char c = LORA_SERIAL.read();
            resp += c;
            if (resp.endsWith("\r\n")) {
                Serial.print("  <- "); Serial.print(resp);
                if (resp.indexOf("+OK") >= 0) return true;
                if (resp.indexOf("+ERR") >= 0) return false;
                resp = "";
            }
        }
    }
    Serial.println("  <- (timeout)");
    return false;
}

inline bool sendAT(const char* cmd) {
    Serial.print("  -> "); Serial.println(cmd);
    LORA_SERIAL.println(cmd);
    return waitForOK();
}

inline void begin() {
    Serial.print("LoRa node "); Serial.print(LORA_ADDRESS); Serial.println(" starting");
    LORA_SERIAL.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
    delay(100);
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+ADDRESS=%d", LORA_ADDRESS);
    sendAT(cmd);
    snprintf(cmd, sizeof(cmd), "AT+NETWORKID=%d", LORA_CHANNEL);
    sendAT(cmd);
    snprintf(cmd, sizeof(cmd), "AT+BAND=%lu", (unsigned long)LORA_BAND);
    sendAT(cmd);
#ifdef LORA_CPIN
    snprintf(cmd, sizeof(cmd), "AT+CPIN=%s", LORA_CPIN);
    sendAT(cmd);
#endif
}

// Build + send an envelope: {"n":addr,"s":seq,"t":type,"d":<data_json>}
// <data_json> must be a JSON object, e.g. `{"tc":21.4}` or `{}`.
inline void send(uint32_t seq, const char* type, const char* data_json) {
    char payload[220];
    snprintf(payload, sizeof(payload),
             "{\"n\":%d,\"s\":%lu,\"t\":\"%s\",\"d\":%s}",
             LORA_ADDRESS, (unsigned long)seq, type, data_json);
    int len = strlen(payload);
    char cmd[280];
    snprintf(cmd, sizeof(cmd), "AT+SEND=%d,%d,%s",
             LORA_GATEWAY_ADDRESS, len, payload);
    sendAT(cmd);
}

// Poll the UART for an incoming +RCV line. If one arrives within
// `timeout_ms`, copy its JSON payload into out_payload and return true.
// Uses a static line buffer so partial lines carry across calls — safe
// for single-threaded Arduino code, not reentrant.
inline bool receive(char* out_payload, size_t out_cap, unsigned long timeout_ms) {
    static char line[320];
    static size_t idx = 0;
    unsigned long start = millis();
    while (millis() - start < timeout_ms) {
        while (LORA_SERIAL.available()) {
            char c = LORA_SERIAL.read();
            if (idx < sizeof(line) - 1) line[idx++] = c;
            if (idx >= 2 && line[idx - 2] == '\r' && line[idx - 1] == '\n') {
                line[idx] = '\0';
                Serial.print("  <- "); Serial.print(line);
                size_t line_len = idx;
                idx = 0;
                if (strncmp(line, "+RCV=", 5) != 0) continue;
                char* p1 = strchr(line + 5, ',');
                if (!p1) continue;
                char* p2 = strchr(p1 + 1, ',');
                if (!p2) continue;
                int data_len = atoi(p1 + 1);
                size_t data_start = (size_t)(p2 - line) + 1;
                if (data_start + (size_t)data_len > line_len) continue;
                size_t copy = (size_t)data_len < out_cap - 1 ? (size_t)data_len : out_cap - 1;
                memcpy(out_payload, line + data_start, copy);
                out_payload[copy] = '\0';
                return true;
            }
        }
    }
    return false;
}

namespace json {

// Minimal, forgiving helpers for the short-key envelope format. Not a real
// JSON parser — they work because our payloads are flat and controlled.

inline bool findInt(const char* payload, const char* key, long* out) {
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(payload, pat);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    *out = strtol(p + 1, nullptr, 10);
    return true;
}

inline bool findBool(const char* payload, const char* key) {
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* p = strstr(payload, pat);
    if (!p) return false;
    p = strchr(p, ':');
    if (!p) return false;
    while (*++p == ' ') {}
    return *p == 't';
}

inline bool typeEquals(const char* payload, const char* want) {
    char pat[32];
    snprintf(pat, sizeof(pat), "\"t\":\"%s\"", want);
    return strstr(payload, pat) != nullptr;
}

inline bool addressedTo(const char* payload, int addr) {
    long n = -1;
    if (!findInt(payload, "n", &n)) return false;
    return (int)n == addr;
}

} // namespace json
} // namespace rylr
