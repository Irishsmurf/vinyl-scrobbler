#!/usr/bin/env python3
"""MQTT -> Google Cloud Pub/Sub bridge for the Vinyl Scrobbler.

The ESP32 / ESPHome scanner publishes scanned RFID UIDs to an MQTT topic. This
long-running service subscribes to that topic and republishes each UID to the
Pub/Sub topic that triggers the `ScrobbleAlbum` Cloud Function.

The UID payload is forwarded verbatim: `ScrobbleAlbum` base64-decodes the Pub/Sub
message data back into the original UID string, so no transformation is needed here.

Configuration (all via environment variables, with sensible defaults):

    GCP_PROJECT_ID     Google Cloud project              (default: rfid-album-scrobblr)
    PUBSUB_TOPIC       Pub/Sub topic ID                  (default: vinyl-scrobble)
    MQTT_BROKER_HOST   MQTT broker hostname              (default: coventry.paddez.com)
    MQTT_BROKER_PORT   MQTT broker port                  (default: 1883)
    MQTT_TOPIC         MQTT topic to subscribe to        (default: VinylScrobbler/rfid/reads)
    MQTT_USERNAME      MQTT username                     (optional)
    MQTT_PASSWORD      MQTT password                     (optional)
    MQTT_CLIENT_ID     MQTT client id                    (default: vinyl-scrobbler-bridge)

Google Cloud authentication uses Application Default Credentials. In the Docker
image a service-account key is copied to /app/credentials.json; point
GOOGLE_APPLICATION_CREDENTIALS at it (the Dockerfile / run command does this).
"""

import logging
import os
import signal
import sys

import paho.mqtt.client as mqtt
from google.cloud import pubsub_v1

logging.basicConfig(
    level=os.environ.get("LOG_LEVEL", "INFO"),
    format="%(asctime)s %(levelname)s %(message)s",
)
log = logging.getLogger("mqtt-pubsub-bridge")


def env(name, default):
    """Read an environment variable, treating empty strings as unset."""
    value = os.environ.get(name)
    return value if value else default


GCP_PROJECT_ID = env("GCP_PROJECT_ID", "rfid-album-scrobblr")
PUBSUB_TOPIC = env("PUBSUB_TOPIC", "vinyl-scrobble")
MQTT_BROKER_HOST = env("MQTT_BROKER_HOST", "coventry.paddez.com")
MQTT_BROKER_PORT = int(env("MQTT_BROKER_PORT", "1883"))
MQTT_TOPIC = env("MQTT_TOPIC", "VinylScrobbler/rfid/reads")
MQTT_USERNAME = env("MQTT_USERNAME", None)
MQTT_PASSWORD = env("MQTT_PASSWORD", None)
MQTT_CLIENT_ID = env("MQTT_CLIENT_ID", "vinyl-scrobbler-bridge")

publisher = pubsub_v1.PublisherClient()
topic_path = publisher.topic_path(GCP_PROJECT_ID, PUBSUB_TOPIC)


def on_connect(client, userdata, flags, reason_code, properties=None):
    if reason_code == 0:
        log.info("Connected to MQTT broker %s:%s", MQTT_BROKER_HOST, MQTT_BROKER_PORT)
        client.subscribe(MQTT_TOPIC)
        log.info("Subscribed to MQTT topic '%s'", MQTT_TOPIC)
    else:
        log.error("MQTT connection failed (reason code %s)", reason_code)


def on_disconnect(client, userdata, flags, reason_code, properties=None):
    # paho's reconnect_delay_set (below) handles automatic reconnection.
    log.warning("Disconnected from MQTT broker (reason code %s); will retry", reason_code)


def on_message(client, userdata, msg):
    uid = msg.payload.decode("utf-8", errors="replace").strip()
    if not uid:
        log.warning("Ignoring empty payload on '%s'", msg.topic)
        return

    log.info("MQTT '%s' -> UID '%s'; publishing to Pub/Sub '%s'", msg.topic, uid, PUBSUB_TOPIC)
    try:
        future = publisher.publish(topic_path, uid.encode("utf-8"))
        message_id = future.result(timeout=30)
        log.info("Published to Pub/Sub (message id %s)", message_id)
    except Exception:  # noqa: BLE001 - keep the bridge alive on transient errors
        log.exception("Failed to publish UID '%s' to Pub/Sub", uid)


def main():
    client = mqtt.Client(
        callback_api_version=mqtt.CallbackAPIVersion.VERSION2,
        client_id=MQTT_CLIENT_ID,
    )
    if MQTT_USERNAME:
        client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message
    client.reconnect_delay_set(min_delay=1, max_delay=60)

    def shutdown(signum, _frame):
        log.info("Received signal %s; shutting down", signum)
        client.disconnect()
        sys.exit(0)

    signal.signal(signal.SIGTERM, shutdown)
    signal.signal(signal.SIGINT, shutdown)

    log.info("Connecting to MQTT broker %s:%s ...", MQTT_BROKER_HOST, MQTT_BROKER_PORT)
    client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, keepalive=60)
    client.loop_forever()


if __name__ == "__main__":
    main()
