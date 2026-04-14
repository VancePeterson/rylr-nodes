#!/usr/bin/env python3
"""Publish Home Assistant MQTT Discovery configs from esphome/nodes.yaml.

Run this once after editing the node registry. It publishes a retained
discovery message for every entity so HA picks them up on its next
reconnect — no gateway reflash needed.

    pip install paho-mqtt pyyaml
    ./scripts/publish_discovery.py \\
        --broker 192.168.1.10 \\
        --username mqtt-user \\
        --password mqtt-password
"""

import argparse
import json
import pathlib
import sys

import paho.mqtt.client as mqtt
import yaml

DISCOVERY_PREFIX = "homeassistant"
STATE_PREFIX = "lora/node"
GATEWAY_AVAILABILITY = "lora/lora-gateway/status"


def build_discovery(node: dict, entity: dict) -> tuple[str, dict]:
    node_id = node["id"]
    key = entity["key"]
    component = entity.get("component", "sensor")
    state_suffix = entity.get("state_topic_suffix", "state")

    object_id = f"rylr_node{node_id}_{key}"
    topic = f"{DISCOVERY_PREFIX}/{component}/{object_id}/config"

    payload: dict = {
        "name": entity["name"],
        "unique_id": object_id,
        "object_id": object_id,
        "state_topic": f"{STATE_PREFIX}/{node_id}/{state_suffix}",
        "value_template": entity["value_template"],
        "availability_topic": GATEWAY_AVAILABILITY,
        "payload_available": "online",
        "payload_not_available": "offline",
        "device": {
            "identifiers": [f"rylr_node{node_id}"],
            "name": node["name"],
            "model": node.get("model", "RYLR998 LoRa node"),
            "manufacturer": "rylr-nodes",
        },
    }

    for optional in ("device_class", "state_class", "expire_after"):
        if optional in entity:
            payload[optional] = entity[optional]
    if "unit" in entity:
        payload["unit_of_measurement"] = entity["unit"]

    return topic, payload


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--broker", required=True)
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument(
        "--registry",
        type=pathlib.Path,
        default=pathlib.Path(__file__).parent.parent / "esphome" / "nodes.yaml",
    )
    parser.add_argument("--dry-run", action="store_true",
                        help="Print what would be published without connecting.")
    args = parser.parse_args()

    registry = yaml.safe_load(args.registry.read_text())
    nodes = registry.get("nodes", [])
    if not nodes:
        print(f"No nodes found in {args.registry}", file=sys.stderr)
        return 1

    messages: list[tuple[str, str]] = []
    for node in nodes:
        for entity in node.get("entities", []):
            topic, payload = build_discovery(node, entity)
            messages.append((topic, json.dumps(payload, separators=(",", ":"))))

    if args.dry_run:
        for topic, payload in messages:
            print(f"{topic}\n  {payload}\n")
        return 0

    client = mqtt.Client()
    if args.username:
        client.username_pw_set(args.username, args.password)
    client.connect(args.broker, args.port)
    client.loop_start()

    for topic, payload in messages:
        info = client.publish(topic, payload, qos=1, retain=True)
        info.wait_for_publish()
        print(f"published {topic}")

    client.loop_stop()
    client.disconnect()
    print(f"\n{len(messages)} discovery messages published.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
