# ESPHome Firmware (Home Assistant ESPHome Device Builder)

This directory contains an [ESPHome](https://esphome.io) port of the Arduino sketch in
[`/esp32`](../esp32/esp32_rfid_scrobbler.ino). It turns the scanner into a device you manage from
Home Assistant's **ESPHome Device Builder** add-on: YAML configuration, over-the-air updates,
live logs, and dashboard entities — no Arduino IDE required.

The device behaves exactly like the sketch from the backend's point of view:

- Scanned tag UIDs are published to `VinylScrobbler/rfid/reads` as lowercase, colon-separated
  hex (e.g. `04:a2:5b:1c`), the format the `scrobble_album` Cloud Function matches against
  Firestore.
- `online` / `offline` is published to `VinylScrobbler/status` (birth message / MQTT last will).

On top of that, the native Home Assistant API exposes a **Tag Present** binary sensor, a
**Last Scanned UID** text sensor, device status, and Wi-Fi signal strength. MQTT publishing is
independent of Home Assistant, so scrobbling keeps working even if HA is down.

## Hardware & Wiring

Identical to the Arduino sketch — no rewiring needed:

| PN532 (HSU mode) | ESP32 |
|---|---|
| TX | GPIO16 (RX2) |
| RX | GPIO17 (TX2) |
| VCC | 5V (or 3.3V, per your board) |
| GND | GND |

Leave the PN532's DIP switches in **HSU (UART) mode** (both switches off on the common
Elechouse-style boards), 115200 baud.

## The `pn532_uart` component

ESPHome's built-in `pn532` component only supports I²C and SPI. To keep the existing UART
wiring, this repo vendors a small external component in
[`components/pn532_uart/`](./components/pn532_uart/) that implements the PN532's HSU protocol
on top of ESPHome's `uart` bus. It subclasses the stock `pn532` hub, so all its features
(`on_tag`, `on_tag_removed`, binary sensors for specific UIDs, tag writing) work as documented
in the [ESPHome PN532 docs](https://esphome.io/components/binary_sensor/pn532/) — just use
`pn532_uart:` instead of `pn532_i2c:`.

It requires ESPHome **2026.6.0 or newer**.

## Setup with the ESPHome Device Builder

1. In Home Assistant, install the **ESPHome Device Builder** add-on (Settings → Add-ons) if
   you haven't already.
2. Copy this directory's contents into the add-on's config folder (`/config/esphome/`):
   - `vinyl-scrobbler.yaml`
   - `components/` (keep it next to the YAML)

   *Alternatively*, copy only `vinyl-scrobbler.yaml` and switch the `external_components:`
   section to the commented-out `github://Irishsmurf/vinyl-scrobbler@main` source so the
   component is fetched from this repository.
3. Open the Device Builder's **Secrets** editor and add the entries from
   [`secrets.yaml.example`](./secrets.yaml.example) with your real values (Wi-Fi, MQTT
   credentials, API encryption key, OTA password).
4. For the **first flash**, connect the ESP32 over USB (to the machine running your browser)
   and use *Install → Plug into this computer*. Every update after that can be installed
   wirelessly (OTA).
5. Home Assistant will discover the device automatically (Settings → Devices & Services →
   ESPHome). Confirm with the encryption key if prompted.

## Verifying

- Open the device logs in the Device Builder; you should see `Found chip PN5xx` and
  `Firmware v1.x` during boot.
- Scan a tagged album: the log shows the tag read, **Last Scanned UID** updates in Home
  Assistant, and the UID is published to `VinylScrobbler/rfid/reads` — from there the existing
  MQTT → Pub/Sub → Cloud Function pipeline scrobbles the album as before.

## Command-line alternative

If you don't run Home Assistant's add-on you can use the ESPHome CLI directly:

```bash
pip install esphome
cd esphome
cp secrets.yaml.example secrets.yaml   # then edit with real values
esphome run vinyl-scrobbler.yaml
```
