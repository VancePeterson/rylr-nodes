# rylr-nodes

A low-power LoRa sensor network for Home Assistant, built around the
[Reyax RYLR998](https://reyax.com/products/RYLR998) 915 MHz module and an
ESP32 gateway that bridges LoRa traffic to MQTT via
[ESPHome](https://esphome.io/).

- **Nodes** run PlatformIO / Arduino firmware and send JSON payloads over LoRa
  using the RYLR998's AT command interface.
- **Gateway** is an ESP32 flashed with ESPHome. It exposes the LoRa module's
  UART over MQTT so Home Assistant can both receive node messages and send AT
  commands back down to the radio.
- **Encryption** uses the RYLR998's built-in AES via `AT+CPIN`, so payloads
  on-air are encrypted end-to-end between nodes and gateway.

## Why this exists

Zigbee and Z-Wave are great inside a house. Once you want a battery-powered
sensor out at the mailbox, in the garden shed, or on a neighbor's fence, range
gets painful. LoRa at 915 MHz reaches hundreds of meters through walls and
trees with a sub-dollar antenna, and the RYLR998 turns that into a
three-wire UART problem instead of an SX1276 driver problem.

This repo gives you working references: a gateway any Home Assistant user can
flash without writing C++, and four node templates covering the four
categories battery / mains × report-only / report + command.

## Repository layout

```
.
├── platformio.ini              # Node firmware build envs (one per node)
├── include/
│   └── rylr.h                  # Shared RYLR998 helpers used by every node
├── src/
│   ├── testnode.cpp            # Bench / range test heartbeat node
│   ├── deep_sleep_simplex.cpp  # Skeleton: battery, report-only
│   ├── deep_sleep_duplex.cpp   # Skeleton: battery, report + command
│   ├── powered_simplex.cpp     # Skeleton: mains powered, report-only
│   └── powered_duplex.cpp      # Skeleton: mains powered, report + command
├── esphome/
│   ├── gateway-wifi.yaml       # ESP32 DevKit + WiFi gateway
│   ├── gateway-eth01.yaml      # WT32-ETH01 (wired Ethernet) gateway
│   └── nodes.yaml              # Node registry — source of truth for HA discovery
├── scripts/
│   └── publish_discovery.py    # Publishes HA MQTT Discovery from nodes.yaml
└── secrets.ini                 # PlatformIO secrets (git-ignored)
```

## How it works

```
  ┌──────────────┐   LoRa 915 MHz    ┌──────────────┐   UART    ┌──────────┐
  │  Node (ESP32)│ ───────────────▶  │   RYLR998    │ ────────▶ │  ESP32   │
  │   + RYLR998  │    AES encrypted  │  (gateway)   │           │ ESPHome  │
  └──────────────┘                   └──────────────┘           └────┬─────┘
                                                                     │ MQTT
                                                                     ▼
                                                              ┌─────────────┐
                                                              │ Home        │
                                                              │ Assistant   │
                                                              └─────────────┘
```

1. Each node configures its RYLR998 on boot (`AT+ADDRESS`, `AT+NETWORKID`,
   `AT+BAND`, `AT+CPIN`) and periodically calls `AT+SEND` with a JSON
   payload addressed to the gateway (address `1`).
2. The gateway's RYLR998 receives the frame, decrypts it with the shared CPIN,
   and emits a `+RCV=<addr>,<len>,<data>,<rssi>,<snr>` line on UART.
3. ESPHome forwards UART traffic to MQTT under the `lora/` topic prefix.
   Home Assistant subscribes and turns payloads into sensors, automations, and
   dashboards.
4. To reconfigure the radio at runtime, publish an AT command to
   `lora/lora-gateway/at/send` and ESPHome writes it straight to the module.

## Hardware

Per node **and** for the gateway:

| Part                  | Notes                                           |
|-----------------------|-------------------------------------------------|
| ESP32 dev board       | Any ESP32 works. Gateway examples target the    |
|                       | ESP32-DevKit-v1 and the WT32-ETH01.             |
| Reyax RYLR998         | 915 MHz (US/AU) module, 3.3 V logic.            |
| Antenna               | Matching 915 MHz whip/SMA antenna.              |
| 3.3 V supply          | RYLR998 can draw ~120 mA in TX bursts.          |

Wiring (both nodes and gateway):

| RYLR998 pin | ESP32 pin          |
|-------------|--------------------|
| VCC         | 3V3                |
| GND         | GND                |
| TXD         | GPIO15 (ESP32 RX)  |
| RXD         | GPIO14 (ESP32 TX)  |
| RST         | *(optional)* 3V3   |

> The node example in `src/testnode.cpp` currently uses GPIO33/32 for UART2 —
> adjust to taste or align with the gateway pinout if you prefer consistency.

## Gateway setup (ESPHome)

The two YAML files in `esphome/` are standalone and ready to copy into your
ESPHome dashboard or a local `esphome/` directory.

- **`gateway-wifi.yaml`** — ESP32 DevKit v1 with WiFi.
- **`gateway-eth01.yaml`** — WT32-ETH01 with wired 100 Mbit Ethernet, for
  installs where WiFi doesn't reach the gateway location (garages, sheds,
  utility closets).

Both configs:

- Use ESP-IDF framework for smaller binaries and better WiFi/Ethernet stability.
- Expose the RYLR998 over UART id `rylr_uart` on GPIO14 (TX) / GPIO15 (RX).
- Log all UART traffic in both directions at `DEBUG` level.
- Connect to your broker and listen on `lora/lora-gateway/at/send` for AT
  commands to forward to the module.

### Required secrets

Create a `secrets.yaml` alongside the gateway config with:

```yaml
wifi_ssid: "your-ssid"
wifi_password: "your-wifi-password"
fallback_ap_password: "fallback-ap-password"

mqtt_broker: "192.168.1.10"
mqtt_username: "mqtt-user"
mqtt_password: "mqtt-password"
```

You should also generate a real API encryption key (`esphome config ...`
or `openssl rand -base64 32`) and OTA password, then fill them into the
`api.encryption.key` and `ota.password` fields. The committed files contain
empty placeholders so ESPHome will refuse to compile until you set them —
that's intentional.

### Flashing

```bash
esphome run esphome/gateway-wifi.yaml
# or
esphome run esphome/gateway-eth01.yaml
```

On first boot the gateway joins your network, connects to MQTT, and starts
logging UART traffic. Open the ESPHome logs to confirm you see `+OK` responses
from the module.

### Configuring the gateway radio

Because the YAML does not auto-configure the RYLR998, you run these once from
an MQTT client (or a Home Assistant `mqtt.publish` service call) after the
gateway first boots:

```
lora/lora-gateway/at/send   AT+ADDRESS=1
lora/lora-gateway/at/send   AT+NETWORKID=6
lora/lora-gateway/at/send   AT+BAND=915000000
lora/lora-gateway/at/send   AT+CPIN=FABC0002EEDCAA90FABC0002EEDCAA90
```

Settings persist in the module's NVS, so you only need to do this once per
gateway (and once per node).

## Node firmware (PlatformIO)

Each node has its own environment in `platformio.ini`. The defaults match the
gateway:

- `LORA_BAND=915000000`
- `LORA_CHANNEL=6` (maps to `AT+NETWORKID`)
- `LORA_GATEWAY_ADDRESS=1`
- `LORA_CPIN` is pulled from `secrets.ini`

Create `secrets.ini` in the repo root (it's git-ignored):

```ini
[secrets]
lora_cpin = FABC0002EEDCAA90FABC0002EEDCAA90
```

Build and flash the bench test node to confirm your gateway is hearing you:

```bash
pio run -e testnode -t upload
pio device monitor -e testnode
```

`src/testnode.cpp` sends a heartbeat envelope every `HEARTBEAT_INTERVAL_S`
seconds. Once you can see its payloads land on `lora/node/2/state` in MQTT,
move on to one of the skeleton templates below.

### Node templates

Four skeleton `.cpp` files in `src/` cover the combinations of power profile
and communication direction. Each has a big header comment explaining the
lifecycle, the rationale, which hook methods to fill in, and example devices
that fit the pattern.

| File                      | Power  | Direction        | Fill in |
|---------------------------|--------|------------------|---------|
| `deep_sleep_simplex.cpp`  | battery| report-only      | `setupPeripherals()`, `readState()` |
| `deep_sleep_duplex.cpp`   | battery| report + command | `setupPeripherals()`, `readState()`, `applyCommand()` |
| `powered_simplex.cpp`     | mains  | report-only      | `setupPeripherals()`, `pollEvent()`, `pollPeriodicState()` |
| `powered_duplex.cpp`      | mains  | report + command | `setupPeripherals()`, `readState()`, `applyCommand()` |

All four use the shared `include/rylr.h` helper — `rylr::begin()`,
`rylr::send(seq, type, data)`, and `rylr::receive(...)` — so your node code
stays focused on the sensor or actuator, not on AT command plumbing.

> **Deep-sleep + command caveat.** A sleeping node's radio is off and cannot
> be reached directly. The duplex battery template handles this by waking on
> schedule, reporting its state, holding the radio on for `LISTEN_WINDOW_MS`
> while HA has a chance to push a command, then sleeping again. Commands
> aren't instant — they're bounded by the wake interval. Use the powered
> duplex template if you need sub-second response.

## Protocol

Everything on the wire — LoRa frames and MQTT payloads alike — uses the same
compact JSON envelope:

```json
{ "n": 2, "s": 147, "t": "hb", "d": {} }
```

| Field | Meaning                                                              |
|-------|----------------------------------------------------------------------|
| `n`   | Node address (1–255). `1` is reserved for the gateway.               |
| `s`   | Monotonic sequence number per node. Used for ack matching and dedup. |
| `t`   | Message type. Short string: `hb`, `state`, `event`, `ack`, `cmd`, …  |
| `d`   | Type-specific data object. Keep keys short to fit in 240 bytes.      |

Short keys matter: the RYLR998's `AT+SEND` tops out around 240 bytes of
payload, and airtime is a real cost for battery nodes. Spending 16 bytes on
an envelope instead of 40+ leaves more room for actual sensor data.

### MQTT topics

| Topic                          | Direction   | Retained | Purpose                                     |
|--------------------------------|-------------|----------|---------------------------------------------|
| `lora/node/<id>/state`         | Gateway → HA| yes      | Latest state envelope from the node         |
| `lora/node/<id>/meta`          | Gateway → HA| yes      | `{rssi, snr, uptime}` for the last frame    |
| `lora/cmd`                     | HA → Gateway| no       | Command envelope — gateway routes by `n`    |
| `lora/node/<id>/ack`           | Gateway → HA| no       | Ack envelopes republished from the node     |
| `lora/lora-gateway/at/send`    | HA → Gateway| no       | Raw `AT+...` passthrough for debugging      |
| `lora/lora-gateway/status`     | Gateway → HA| yes      | ESPHome availability (`online` / `offline`) |

All gateway → HA topics are populated automatically by the ESPHome parser in
`esphome/gateway-*.yaml`. State and meta are retained so HA restarts see the
last known value instantly.

### Sending a command

Publish a full envelope to `lora/cmd` — the gateway parses `n` from the JSON
and wraps the whole thing in `AT+SEND=<n>,<len>,<payload>`:

```bash
mosquitto_pub -h broker -t lora/cmd \
  -m '{"n":2,"s":1,"t":"set_interval","d":{"sec":300}}'
```

The node handles the command, increments its own sequence, and replies with
`{"n":2,"s":<its-seq>,"t":"ack","d":{"for":1,"ok":true}}`, which the gateway
republishes to `lora/node/2/ack`. Match acks to commands by the `for` field
(the original command's `s`), not by any positional ordering.

## Node registry & Home Assistant discovery

`esphome/nodes.yaml` is the single source of truth for every node on the
network. Each entry lists the node's LoRa address, its hardware, and the HA
entities derived from its state payload via Jinja templates:

```yaml
nodes:
  - id: 2
    name: "Heartbeat Node"
    model: "ESP32 + RYLR998"
    entities:
      - component: sensor
        key: rssi
        name: "RSSI"
        value_template: "{{ value_json.rssi }}"
        state_topic_suffix: meta
        device_class: signal_strength
        unit: "dBm"
```

To register the entities with Home Assistant, run the discovery publisher:

```bash
pip install paho-mqtt pyyaml
./scripts/publish_discovery.py \
    --broker 192.168.1.10 \
    --username mqtt-user \
    --password mqtt-password
```

The script reads `esphome/nodes.yaml` and publishes one retained
`homeassistant/<component>/rylr_node<id>_<key>/config` message per entity.
HA's MQTT Discovery picks them up immediately and groups them under one
device per node.

Use `--dry-run` to preview what would be published.

### Adding a new node

1. Copy the skeleton that matches your node's category (see the table above)
   to `src/nodeN.cpp` and fill in the hook methods.
2. Add an `[env:nodeN]` block in `platformio.ini` with a unique `LORA_ADDRESS`
   and `NODE_NAME`, matching `build_src_filter`, and any `lib_deps` your
   sensor needs.
3. Add an entry to `esphome/nodes.yaml` describing its HA entities.
4. `./scripts/publish_discovery.py --broker ...` to register with HA.
5. `pio run -e nodeN -t upload` and power it on.

No gateway reflash. No HA YAML edits. The gateway parses any `+RCV=` frame it
hears and the registry-driven discovery teaches HA what the fields mean.

## Roadmap

- [ ] Command retry / ack timeout handling on the node side.
- [ ] Optional packed-binary envelope for high-frequency nodes.
- [ ] Gateway-side parser that publishes per-node `last_seen` timestamps.

## License

GNU. See `LICENSE`
