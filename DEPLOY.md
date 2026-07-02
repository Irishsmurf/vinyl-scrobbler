# Vinyl Scrobbler — Deployment Guide

The single source of truth for deploying the whole system: exact configuration,
architecture, and step-by-step instructions.

---

## 1. Architecture

Two independent input paths converge on one Pub/Sub topic, which triggers the
scrobbler:

```
                       ┌──────────────── Hardware path ────────────────┐
  ESP32 + PN532 ──MQTT──▶  VinylScrobbler/rfid/reads
   (or ESPHome)            (broker: coventry.paddez.com:1883)
                                        │
                                        ▼
                            docker/mqtt_to_pubsub.py            (long-running bridge)
                                        │
                                        ▼
  ┌───────────── PWA path ─────────────┤
  Web NFC PWA ──HTTPS POST──▶ web_nfc_gateway ──▶  Pub/Sub topic: vinyl-scrobble
   (GitHub Pages)             (Cloud Function)                 │
                                                               ▼
                                                       ScrobbleAlbum          (Cloud Function)
                                                               │
                                        ┌──────────────────────┼───────────────────────┐
                                        ▼                       ▼                        ▼
                                  Firestore              Last.fm API             scrobble tracks
                              (RFID → album map)      (fetch tracklist)         (to your profile)

  Album Management UI (GitHub Pages) ──▶ Firestore   (create/edit RFID → album mappings)
```

**Components**

| Component            | Location                          | Hosting / Runtime          |
| -------------------- | --------------------------------- | -------------------------- |
| Album Management UI  | `docs/album-manager/index.html`   | GitHub Pages               |
| Web NFC PWA          | `docs/index.html`                 | GitHub Pages               |
| `web_nfc_gateway`    | `gcp_functions/web_nfc_gateway/`  | Cloud Functions Gen 2 (HTTP) |
| `ScrobbleAlbum`      | `gcp_functions/scrobble_album/`   | Cloud Functions Gen 2 (Pub/Sub) |
| MQTT → Pub/Sub bridge| `docker/mqtt_to_pubsub.py`        | Docker container (self-hosted) |
| ESP32 firmware       | `esphome/` or `esp32/*.ino`       | ESP32 device               |

---

## 2. Exact configuration

Keep these values identical everywhere. Changing one means changing every row that
references it.

### Google Cloud

| Setting            | Value                                    |
| ------------------ | ---------------------------------------- |
| Project ID         | `rfid-album-scrobblr`                     |
| Region             | `us-central1` (both functions)           |
| Runtime            | `nodejs20`                               |
| Pub/Sub topic      | `vinyl-scrobble`                         |
| `gcloud` profile   | `vinyl-scrobbler` (activated via `.envrc` + direnv) |

### Cloud Functions

| Function          | Trigger                    | Entry point   | Env vars                                            |
| ----------------- | -------------------------- | ------------- | -------------------------------------------------- |
| `web_nfc_gateway` | HTTP (allow-unauthenticated) | `publishRfid` | — (publishes to `vinyl-scrobble` in code)          |
| `ScrobbleAlbum`   | Pub/Sub topic `vinyl-scrobble` | `helloPubSub` | `LASTFM_API_KEY`, `LASTFM_API_SECRET`, `LASTFM_SESSION_KEY` |

### MQTT bridge (`docker/`)

| Variable           | Value                         |
| ------------------ | ----------------------------- |
| `MQTT_BROKER_HOST` | `coventry.paddez.com`         |
| `MQTT_BROKER_PORT` | `1883`                        |
| `MQTT_TOPIC`       | `VinylScrobbler/rfid/reads`   |
| `GCP_PROJECT_ID`   | `rfid-album-scrobblr`         |
| `PUBSUB_TOPIC`     | `vinyl-scrobble`              |

### Firestore

- Native-mode database in project `rfid-album-scrobblr`.
- Mappings live in an `albums` **collection group**; each document has an `rfid`
  field (the UID string) plus `artist` and `album`.
- The runtime service account needs the **Cloud Datastore User** role.
- The first query needs a collection-group index on `rfid` — if missing, the
  function log prints a `FAILED_PRECONDITION` error with a one-click link to create it.

### Firebase (for the Album Management UI)

- **Anonymous** sign-in enabled (Authentication → Sign-in method).
- Web app config object is injected into `docs/album-manager/index.html` at deploy
  time via the `FIREBASE_CONFIG` GitHub Actions secret (see `.github/workflows/deploy.yml`).

---

## 3. Prerequisites

- `gcloud` CLI, authenticated on `rfid-album-scrobblr` (`gcloud config list`).
- Node.js + npm (for the Cloud Functions).
- Docker (for the MQTT bridge).
- A **Last.fm API account**: API Key, Shared Secret, and a user Session Key.
- A reachable **MQTT broker** (`coventry.paddez.com:1883`) — only for the hardware path.
- Home Assistant + ESPHome Device Builder (recommended) or the Arduino IDE — only
  for the ESP32 path.

---

## 4. Deploy steps

### 4.1 Cloud Functions

```bash
# 1. Provide Last.fm credentials for the scrobbler
cp gcp_functions/scrobble_album/.env.yaml.example gcp_functions/scrobble_album/.env.yaml
# edit .env.yaml -> LASTFM_API_KEY / LASTFM_API_SECRET / LASTFM_SESSION_KEY

# 2. Deploy both functions (creates the vinyl-scrobble topic if needed,
#    prints the gateway's HTTPS URL)
cd gcp_functions
./deploy.sh
cd ..
```

Grant Firestore access to the runtime service account (one-time):

```bash
PROJECT_NUMBER=$(gcloud projects describe rfid-album-scrobblr --format='value(projectNumber)')
gcloud projects add-iam-policy-binding rfid-album-scrobblr \
  --member="serviceAccount:${PROJECT_NUMBER}-compute@developer.gserviceaccount.com" \
  --role="roles/datastore.user"
```

Full details, local testing, and manual `gcloud` commands are in
[`gcp_functions_guide.md`](./gcp_functions_guide.md).

### 4.2 Web UIs (GitHub Pages)

1. In the repo settings → **Pages**, set Source = "Deploy from a branch",
   Branch = `main`, folder = `/docs`.
2. Add the `FIREBASE_CONFIG` repository secret (your Firebase web config JSON) — the
   `Deploy Album Management UI` workflow injects it at build time.
3. After the gateway is deployed, put its HTTPS URL into the `CLOUD_FUNCTION_URL`
   constant in `docs/index.html`, then commit and push.

Sites go live at:
- PWA: `https://<user>.github.io/vinyl-scrobbler/`
- Album manager: `https://<user>.github.io/vinyl-scrobbler/album-manager/`

### 4.3 MQTT → Pub/Sub bridge (hardware path only)

```bash
cd docker

# 1. Service-account key with Pub/Sub Publisher on the topic
gcloud iam service-accounts create vinyl-bridge --display-name="Vinyl MQTT bridge"
SA="vinyl-bridge@rfid-album-scrobblr.iam.gserviceaccount.com"
gcloud pubsub topics add-iam-policy-binding vinyl-scrobble \
  --member="serviceAccount:$SA" --role="roles/pubsub.publisher"
gcloud iam service-accounts keys create credentials.json --iam-account="$SA"

# 2. Build and run (key is mounted, not baked into the image)
docker build -t vinyl-mqtt-bridge .
docker run -d --name vinyl-bridge --restart unless-stopped \
  -v "$PWD/credentials.json:/app/credentials.json:ro" \
  vinyl-mqtt-bridge

docker logs -f vinyl-bridge
```

Details and auth options: [`docker/README.md`](./docker/README.md).

### 4.4 ESP32 scanner

- **Recommended (ESPHome / Home Assistant):** follow [`esphome/README.md`](./esphome/README.md).
- **Arduino:** flash `esp32/esp32_rfid_scrobbler.ino`; broker/topic are already set to
  `coventry.paddez.com:1883` / `VinylScrobbler/rfid/reads`; put Wi-Fi + MQTT
  credentials in `esp32/credentials.h`.

### 4.5 Populate the album collection

Open the Album Management UI, connect it to Firestore, and add one entry per record:
the tag's **UID**, **Artist**, and **Album title**.

---

## 5. Verify end-to-end

```bash
# PWA path: hit the gateway directly
curl -X POST "$GATEWAY_URL" -H "Content-Type: application/json" \
  -d '{"rfid": "12 34 56 78"}'

# Hardware path: publish a UID to MQTT and confirm the bridge forwards it
mosquitto_pub -h coventry.paddez.com -p 1883 \
  -t VinylScrobbler/rfid/reads -m "12 34 56 78"
```

Then check the `ScrobbleAlbum` logs in the Cloud console and your Last.fm profile.
Use a UID that exists in Firestore, or you'll see "No album found".

---

## 6. Housekeeping

Stale/failed deployments cause confusion. List what actually exists and remove
stragglers (note the per-region `--region` flag):

```bash
gcloud functions list --gen2
gcloud functions delete ScrobbleAlbum --gen2 --region=europe-west2   # old failed deploy
```

## 7. Common issues

| Symptom                              | Cause / fix                                                            |
| ------------------------------------ | --------------------------------------------------------------------- |
| Function deploys but never scrobbles | Missing/incorrect Last.fm env vars — re-check `.env.yaml`.            |
| `FAILED_PRECONDITION` in logs        | Missing Firestore collection-group index — click the link in the log. |
| "No album found for RFID"            | UID not in Firestore, or format mismatch vs. what the scanner sends.  |
| Bridge can't publish                 | Service account lacks `roles/pubsub.publisher` on `vinyl-scrobble`.   |
| Web NFC not working                  | Needs Chrome on Android over HTTPS (GitHub Pages provides HTTPS).     |
