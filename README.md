# Vinyl Scrobbler

Scrobble vinyl albums to Last.fm straight from your shelf by tapping an RFID/NFC tag
on the sleeve — either with a dedicated ESP32 scanner or an Android phone. This repo
holds every component: the album-management UI, the ESP32/ESPHome firmware, the Web
NFC PWA, and the backend Google Cloud Functions.

> **Deploying or configuring?** Everything — architecture, exact config values, and
> step-by-step instructions — is in **[DEPLOY.md](./DEPLOY.md)**.

## How it works

Two input methods feed one Pub/Sub topic, which triggers the scrobbler:

- **ESP32 / ESPHome scanner** → publishes the tag UID over **MQTT** → the
  [`docker/`](./docker/) bridge republishes it to Pub/Sub.
- **Web NFC PWA** → HTTP POST → the `web_nfc_gateway` function publishes it to Pub/Sub.
- **`ScrobbleAlbum`** consumes the topic, looks the UID up in Firestore, fetches the
  tracklist from Last.fm, and scrobbles it.
- The **Album Management UI** maintains the RFID → album mappings in Firestore.

See [DEPLOY.md](./DEPLOY.md#1-architecture) for the full diagram.

## Repository map

| Component            | Location                                    | Notes                                   |
| -------------------- | ------------------------------------------- | --------------------------------------- |
| Album Management UI  | [`docs/album-manager/`](./docs/album-manager/) | Web UI for RFID → album mappings (Firestore). |
| Web NFC PWA          | [`docs/index.html`](./docs/index.html)      | Scan tags with an Android phone (needs HTTPS). |
| ESP32 scanner        | [`esphome/`](./esphome/README.md) · [`esp32/`](./esp32/) | ESPHome (recommended) or Arduino sketch. |
| `ScrobbleAlbum`      | [`gcp_functions/scrobble_album/`](./gcp_functions/scrobble_album/) | Core logic (Pub/Sub-triggered).         |
| `web_nfc_gateway`    | [`gcp_functions/web_nfc_gateway/`](./gcp_functions/web_nfc_gateway/) | HTTP entry point for the PWA.           |
| MQTT → Pub/Sub bridge| [`docker/`](./docker/README.md)             | Connects the hardware scanner path.     |

## Features

- **Two scanning methods** — a low-power ESP32 device or a phone PWA.
- **Decoupled** — components communicate via Pub/Sub, so each can change independently.
- **Battery efficient** — the ESP32 deep-sleeps between scans.
- **Free frontend hosting** — the web UIs run on GitHub Pages.

## Deploying

The whole system deploys from [DEPLOY.md](./DEPLOY.md). The short version:

```bash
# Cloud Functions (after adding Last.fm creds to scrobble_album/.env.yaml)
cd gcp_functions && ./deploy.sh
```

- Cloud Functions: [`gcp_functions_guide.md`](./gcp_functions_guide.md) (local testing + manual commands).
- Hardware bridge: [`docker/README.md`](./docker/README.md).
- ESP32 firmware: [`esphome/README.md`](./esphome/README.md).

## Usage

1. **Populate your collection.** Open the Album Management UI, connect it to Firestore,
   and add an entry per record: the tag **UID**, **Artist**, and **Album title**. Scan a
   tag to read its UID (via the UI's "Scan RFID" button on an NFC phone, or the ESP32).

2. **Scrobble an album.**
   - **ESP32:** hold the device near the album's tag.
   - **Phone:** open the PWA, tap "Scan Album", and hold the phone to the tag.

   The album's tracklist appears in your Last.fm history moments later.

## Troubleshooting

Backend/deploy issues are covered in [DEPLOY.md](./DEPLOY.md#7-common-issues).
Frontend/device quick checks:

- **Web NFC not working:** use Chrome on Android over HTTPS (GitHub Pages provides HTTPS).
- **ESP32 not connecting:** verify Wi-Fi/MQTT credentials and watch the serial monitor.

## Contributing

Contributions welcome — open an issue or a pull request.
