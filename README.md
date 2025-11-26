# Vinyl Scrobbler

A comprehensive IoT project to scrobble vinyl albums to Last.fm directly from a physical bookshelf using RFID/NFC technology. This repository contains all components: a web-based management UI, an ESP32 firmware for a dedicated hardware scanner, a PWA for scrobbling via a mobile phone's NFC, and the backend Google Cloud Functions.

---

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Features](#features)
- [Components](#components)
  - [Album Management UI](#1-album-management-ui)
  - [Web NFC Scrobbler PWA](#2-web-nfc-scrobbler-pwa)
  - [ESP32 RFID Scrobbler](#3-esp32-rfid-scrobbler)
  - [Google Cloud Functions](#4-google-cloud-functions)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Setup Steps](#setup-steps)
- [Usage](#usage)
- [Troubleshooting](#troubleshooting)
- [Contributing](#contributing)

## Overview

This project bridges the gap between a physical record collection and digital listening history. By placing an RFID/NFC tag on each vinyl album sleeve, you can instantly scrobble the entire album to your Last.fm profile by scanning it with either a custom-built ESP32 device or your Android phone.

## Architecture

The system is composed of several decoupled components that communicate via cloud services:

![Architecture Diagram](httpsa://i.imgur.com/your-diagram-link.png) <!-- It's highly recommended to create and link a diagram -->

1.  **Frontend (Input Methods):**
    * **ESP32 Device:** Wakes on button press, scans an RFID tag, and publishes the tag's UID to an MQTT topic.
    * **Web NFC PWA:** A user on a mobile phone taps a button, scans an album's NFC tag, and sends the tag's UID to an HTTP endpoint.

2.  **Backend (Processing Logic):**
    * **MQTT Broker & Cloud Pub/Sub Bridge:** The MQTT topic used by the ESP32 is bridged to a Google Cloud Pub/Sub topic.
    * **HTTP Gateway Function:** The PWA sends data to this function, which then publishes it to the same Pub/Sub topic.
    * **Scrobbler Function:** This is the core logic. Triggered by new messages on the Pub/Sub topic, it looks up the RFID tag in Firestore, fetches album info from Last.fm, and scrobbles the tracks.

3.  **Data & Management:**
    * **Firebase Firestore:** Stores the mapping between RFID UIDs and album details (artist, title).
    * **Album Management UI:** A web interface to create, edit, and delete the album mappings in Firestore.

## Features

-   **Dual Scanning Methods:** Use a low-power, dedicated ESP32 device or a modern PWA on your phone.
-   **Real-time Database:** Album mappings are managed in real-time via a web UI powered by Firebase.
-   **Decoupled & Scalable:** Components are independent, communicating via a robust message queue (Pub/Sub).
-   **Battery Efficient:** The ESP32 device uses deep sleep to ensure long battery life, only waking to perform a scan.
-   **Free Hosting:** Web frontends are designed to be hosted for free on GitHub Pages.

## Components

### 1. Album Management UI

-   **Location:** [`/docs/album-manager/index.html`](./docs/album-manager/index.html)
-   **Description:** A web application for managing your album collection in Firestore. It allows you to add, edit, and delete the mappings between an RFID tag's UID and the corresponding album's artist/title.

### 2. Web NFC Scrobbler PWA

-   **Location:** [`/docs/index.html`](./docs/index.html)
-   **Description:** A mobile-friendly Progressive Web App that uses the Web NFC API to scan album tags with a phone. It sends the scanned UID to the HTTP Gateway function. **Requires HTTPS**, which is provided by GitHub Pages.

### 3. ESP32 RFID Scrobbler

-   **Location:** [`/esp32/esp32_rfid_scrobbler.ino`](./esp32/esp32_rfid_scrobbler.ino)
-   **Description:** An Arduino sketch for an ESP32 connected to an MFRC522 RFID reader. It is optimized for low power consumption using deep sleep.

### 4. Google Cloud Functions

#### Scrobble Album Function

-   **Location:** [`/gcp_functions/scrobble_album/`](./gcp_functions/scrobble_album/)
-   **Trigger:** Google Cloud Pub/Sub
-   **Description:** The core backend logic. It listens for RFID UIDs, queries Firestore, fetches tracklists from the Last.fm API, and performs the scrobbling.

#### Web NFC Gateway Function

-   **Location:** [`/gcp_functions/web_nfc_gateway/`](./gcp_functions/web_nfc_gateway/)
-   **Trigger:** HTTP Request
-   **Description:** A simple gateway that receives a UID from the Web NFC PWA and publishes it to the Pub/Sub topic for the main scrobbler function to process.

## Getting Started

### Prerequisites

1.  **Node.js & npm:** Required for deploying Cloud Functions.
2.  **Google Cloud Project:** With the Firestore and Pub/Sub APIs enabled.
3.  **Firebase Project:** Linked to your Google Cloud Project.
4.  **Last.fm API Account:** To get an **API Key**, **Shared Secret**, and a user **Session Key**.
    -   Create API Account: [last.fm/api/account/create](https://www.last.fm/api/account/create)
    -   Follow a guide to get a Session Key for your user account.
5.  **Arduino IDE:** With the ESP32 core installed.
6.  **Hardware:** An ESP32, MFRC522 RFID reader, button, and RFID/NFC tags.

### Setup Steps

1.  **Clone the Repository**
    ```bash
    git clone https://github.com/<your-username>/vinyl-scrobbler.git
    cd vinyl-scrobbler
    ```

2.  **Configure Firebase**
    -   In your Firebase project, go to **Authentication > Sign-in method** and enable the **Anonymous** provider. This allows the album management UI to work without requiring user accounts.
    -   Go to **Firestore Database** and create a database in Native mode.
    -   In **Project Settings**, find your Firebase configuration object. You will need this for the Album Management UI.

3.  **Deploy Web UIs to GitHub Pages**
    -   Push the repository to your own GitHub account.
    -   In the repository settings, go to **Pages**.
    -   Set the **Source** to "Deploy from a branch".
    -   Set the **Branch** to `main` and the folder to `/docs`.
    -   Your sites will be live at `https://<your-username>.github.io/vinyl-scrobbler/` and `https://<your-username>.github.io/vinyl-scrobbler/album-manager/`.

4.  **Deploy the Cloud Functions**
    -   For each function in `/gcp_functions/`:
        -   Navigate into the directory (e.g., `cd gcp_functions/scrobble_album`).
        -   Run `npm install` to install dependencies.
        -   Deploy using the `gcloud` CLI.
        -   **For `scrobble_album`:**
            - Set the trigger to the Pub/Sub topic (`vinyl/scrobble`).
            - Set the following as environment variables: `LASTFM_API_KEY`, `LASTFM_API_SECRET`, `LASTFM_SESSION_KEY`.
        -   **For `web_nfc_gateway`:**
            - Set the trigger to HTTP.
            - Note the **Trigger URL** provided after deployment.

5.  **Connect the PWA to the Gateway**
    -   Open `docs/index.html` in a text editor.
    -   Find the `CLOUD_FUNCTION_URL` constant and replace the placeholder URL with the trigger URL of your `web_nfc_gateway` function.
    -   Commit and push this change to your repository.

6.  **Configure and Flash the ESP32**
    -   Open `/esp32/esp32_rfid_scrobbler.ino` in the Arduino IDE.
    -   Install the required libraries from the Arduino Library Manager: `MFRC522` and `PubSubClient`.
    -   Create a `credentials.h` file in the same directory (`/esp32/`) to store your sensitive information. It should look like this:
        ```cpp
        #define WIFI_SSID "your_wifi_ssid"
        #define WIFI_PASSWORD "your_wifi_password"
        #define MQTT_USER "your_mqtt_username"
        #define MQTT_PASSWORD "your_mqtt_password"
        ```
    -   Fill in your Wi-Fi credentials and MQTT broker details in `credentials.h`.
    -   Flash the code to your ESP32.

## Usage

1.  **Populate Your Collection:**
    -   Navigate to your deployed Album Management UI.
    -   You will be prompted to enter your Firebase configuration JSON. Paste it in to connect the UI to your database.
    -   For each record you own, scan one of your RFID/NFC tags to get its UID. You can use the "Scan RFID" button in the management UI if you are using it on a device with NFC capabilities, or you can use the ESP32 connected to the Arduino IDE's serial monitor.
    -   Add a new entry in the UI with the UID, Artist Name, and Album Title for each album.

2.  **Scrobble an Album:**
    -   **With the ESP32:** Press the button on the device, then hold it near the album's RFID tag within 5 seconds. The device will scan the tag, publish the UID to your MQTT broker, and go back to sleep.
    -   **With Your Phone:** Open the Web NFC Scrobbler PWA on an NFC-enabled Android phone. Tap the "Scan Album" button, then hold your phone to the album's NFC tag.

In a few moments, the entire album's tracklist will appear in your Last.fm profile history.

## Troubleshooting

- **Web NFC not working:** Ensure you are using a compatible browser (e.g., Chrome on Android) and that your site is served over HTTPS (which GitHub Pages does automatically).
- **ESP32 connection issues:** Double-check your Wi-Fi and MQTT credentials in `credentials.h`. Use the serial monitor to view debug messages.
- **Cloud Function errors:** Check the logs for your Cloud Functions in the Google Cloud Console. Common issues include missing environment variables or Firestore permission errors.
- **Firestore query fails:** The `scrobble_album` function requires a composite index in Firestore to query the `albums` collection group. If you see a `FAILED_PRECONDITION` error in your function logs, the error message will contain a direct link to create the required index in your Firebase console.

## Contributing

Contributions are welcome! Please feel free to submit a pull request or open an issue for any bugs or feature requests.
