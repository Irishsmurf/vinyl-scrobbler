# MQTT → Pub/Sub Bridge

Long-running service that connects the **ESP32 / ESPHome** scanner to the backend.

```
ESP32 ──MQTT──▶ VinylScrobbler/rfid/reads ──▶ [this bridge] ──▶ Pub/Sub: vinyl-scrobble ──▶ ScrobbleAlbum
```

The scanner publishes an RFID UID to MQTT; the bridge republishes it verbatim to the
`vinyl-scrobble` Pub/Sub topic, which triggers the `ScrobbleAlbum` Cloud Function.
(The Web NFC PWA reaches the same Pub/Sub topic via `web_nfc_gateway` instead — the
bridge is only for the hardware scanner path.)

## Configuration

All settings come from environment variables; the defaults match this project:

| Variable           | Default                       | Notes                              |
| ------------------ | ----------------------------- | ---------------------------------- |
| `GCP_PROJECT_ID`   | `rfid-album-scrobblr`         |                                    |
| `PUBSUB_TOPIC`     | `vinyl-scrobble`              | Must match the `ScrobbleAlbum` trigger. |
| `MQTT_BROKER_HOST` | `coventry.paddez.com`         |                                    |
| `MQTT_BROKER_PORT` | `1883`                        |                                    |
| `MQTT_TOPIC`       | `VinylScrobbler/rfid/reads`   | Where the scanner publishes UIDs.  |
| `MQTT_USERNAME`    | *(unset)*                     | Optional.                          |
| `MQTT_PASSWORD`    | *(unset)*                     | Optional.                          |
| `MQTT_CLIENT_ID`   | `vinyl-scrobbler-bridge`      |                                    |

## Credentials

The bridge authenticates to Pub/Sub with a **service-account key** that has the
**Pub/Sub Publisher** role on the topic. Create one and save it as
`docker/credentials.json` (git-ignored — never commit it):

```bash
gcloud iam service-accounts create vinyl-bridge --display-name="Vinyl MQTT bridge"
SA="vinyl-bridge@rfid-album-scrobblr.iam.gserviceaccount.com"
gcloud pubsub topics add-iam-policy-binding vinyl-scrobble \
  --member="serviceAccount:$SA" --role="roles/pubsub.publisher"
gcloud iam service-accounts keys create docker/credentials.json --iam-account="$SA"
```

## Run

Build and run with Docker (the key is mounted, not baked into the image):

```bash
cd docker
docker build -t vinyl-mqtt-bridge .
docker run -d --name vinyl-bridge --restart unless-stopped \
  -v "$PWD/credentials.json:/app/credentials.json:ro" \
  vinyl-mqtt-bridge

docker logs -f vinyl-bridge      # watch it connect + forward UIDs
```

Override any default at runtime with `-e`, e.g. a broker that needs auth:

```bash
docker run -d --name vinyl-bridge --restart unless-stopped \
  -v "$PWD/credentials.json:/app/credentials.json:ro" \
  -e MQTT_USERNAME=myuser -e MQTT_PASSWORD=mypass \
  vinyl-mqtt-bridge
```

## Run locally without Docker

```bash
pip install -r requirements.txt
export GOOGLE_APPLICATION_CREDENTIALS="$PWD/credentials.json"
python mqtt_to_pubsub.py
```

## Verify end-to-end

Publish a test UID to MQTT and confirm the bridge forwards it (watch `docker logs`,
and check the `ScrobbleAlbum` logs in the Cloud console):

```bash
mosquitto_pub -h coventry.paddez.com -p 1883 \
  -t VinylScrobbler/rfid/reads -m "12 34 56 78"
```
