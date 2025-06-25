/*
 * Vinyl Scrobbler - ESP32 PN532 NFC Reader & MQTT Publisher
 *
 * This sketch is for an ESP32 that:
 * 1. Wakes up from deep sleep when a button is pressed.
 * 2. Connects to Wi-Fi.
 * 3. Connects to an MQTT broker.
 * 4. Scans for a PN532 NFC tag using High-Speed UART (HSU).
 * 5. Publishes the tag's UID to a specific MQTT topic.
 * 6. Goes back into deep sleep to conserve battery.
 *
 * HARDWARE:
 * - ESP32 Development Board
 * - PN532 NFC RFID V3 Module
 * - A momentary push button
 *
 * WIRING (PN532 in HSU Mode -> ESP32):
 * - Make sure the PN532 module's dip switches are set for HSU (UART) mode.
 * (Typically: SW1=ON, SW2=OFF)
 * - 5V   -> 5V (The PN532 board has a regulator)
 * - GND  -> GND
 * - RXD  -> GPIO 17 (ESP32's Serial2 TX)
 * - TXD  -> GPIO 16 (ESP32's Serial2 RX)
 *
 * WIRING (Wake-up Button):
 * - One leg of the button -> GPIO 33 (Configurable)
 * - Other leg of the button -> GND
 * (We will use the internal pull-up resistor on the ESP32)
 *
 * LIBRARIES TO INSTALL via Arduino Library Manager:
 * 1. "Adafruit PN532" by Adafruit
 * 2. "PubSubClient" by Nick O'Leary
 */

// --- LIBRARIES ---
#include <WiFi.h>
#include <PubSubClient.h>
#include <PN532_HSU.h>
#include <PN532.h>
#include "credentials.h"


// -- MQTT Broker Configuration
const char* mqtt_server = "coventry.paddez.com";
const int mqtt_port = 1883; // 1883 is standard, 8883 for SSL
const char* mqtt_topic = "VinylScrobbler/rfid/reads"; // The topic to publish RFID tags to
const char* mqtt_client_id = "ESP32VinylScrobbler";

const char* will_topic = "VinylScrobbler/status";
const char* will_message = "offline";
boolean will_retain = false;
byte will_qos = 0;

// -- Hardware Pin Configuration
// For PN532 HSU, we use a Hardware Serial port (Serial2)
// RX: GPIO 16, TX: GPIO 17
#define WAKEUP_PIN GPIO_NUM_33 // Pin to connect the wakeup button to

// --- GLOBAL VARIABLES ---
WiFiClient espClient;
PubSubClient client(espClient);

// Use Serial2 for HSU communication with the PN532
PN532_HSU pn532hsu(Serial2);
PN532 nfc(pn532hsu);

// --- FUNCTION PROTOTYPES ---
void setup_wifi();
void reconnect_mqtt();
void scan_and_publish();
String get_rfid_uid(byte *buffer, byte bufferSize);

void setup() {
  // Start serial communication for debugging purposes
  Serial.begin(115200);
  while (!Serial) { delay(10); }

  Serial.println("\nVinyl Scrobbler waking up...");

  // Configure the wakeup pin with an internal pull-up resistor
  // The button should connect this pin to GND
  pinMode(WAKEUP_PIN, INPUT_PULLUP);

  // Connect to Wi-Fi
  setup_wifi();

  // Configure MQTT client
  client.setServer(mqtt_server, mqtt_port);

  // Initialize NFC reader
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  while (!Serial2) { delay(10); }
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Didn't find PN532 board. Going to sleep.");
    esp_deep_sleep_start();
  }

  // Got ok data, print it out!
  Serial.print("Found PN532 with firmware version: ");
  Serial.print((versiondata >> 24) & 0xFF, HEX);
  Serial.print(".");
  Serial.println((versiondata >> 16) & 0xFF, HEX);
  
  // Configure the PN532 to read RFID tags
  nfc.SAMConfig();
  
  Serial.println("NFC Reader Initialized. Hold a card near the reader.");

  // The main logic: connect, scan, publish.
  scan_and_publish();

  // All tasks are done, time to sleep.
  Serial.println("Task complete. Going to sleep now.");

  // Configure ESP32 to wake up when the WAKEUP_PIN is pulled LOW
  esp_sleep_enable_ext0_wakeup(WAKEUP_PIN, 0); // 0 = LOW level trigger

  // Enter deep sleep
  esp_deep_sleep_start();
}

// The loop function is empty because the ESP32 will go into deep sleep
// and reset upon waking, running setup() again.
void loop() {
  // This part of the code will never be reached.
}

/**
 * @brief Connects to the Wi-Fi network. Halts on failure after 20 attempts.
 */
void setup_wifi() {
  delay(10);
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (++retries > 20) {
      Serial.println("\nFailed to connect to WiFi. Going back to sleep.");
      esp_deep_sleep_start();
    }
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

/**
 * @brief Connects to the MQTT broker. Halts on failure after 5 attempts.
 */
void reconnect_mqtt() {
  int retries = 0;
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(mqtt_client_id, mqtt_user, mqtt_password, will_topic, will_qos, will_retain, will_message)) {
      Serial.println("connected");
      client.publish(will_topic, "online");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      retries++;
      delay(2000);
    }
    if (retries > 5) {
      Serial.println("Failed to connect to MQTT broker. Going to sleep.");
      esp_deep_sleep_start();
    }
  }
}

/**
 * @brief Main logic function. Scans for an NFC tag and publishes its UID.
 */
void scan_and_publish() {
  // Ensure we have an MQTT connection
  if (!client.connected()) {
    reconnect_mqtt();
  }
  client.loop(); // Allow the MQTT client to process messages

  Serial.println("Scanning for NFC tag...");
  
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 }; // Buffer to store the returned UID
  uint8_t uidLength;                      // Length of the UID (4 or 7 bytes)

  // Try to find a new card for a few seconds
  // readPassiveTargetID() has a 1-second timeout by default. We'll try 5 times.
  bool cardFound = false;
  for (int i = 0; i < 5; i++) {
    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

    if (success) {
      String uid_str = get_rfid_uid(uid, uidLength);
      Serial.print("Card detected! UID: ");
      Serial.println(uid_str);

      // Publish the UID to the MQTT topic
      if (client.publish(mqtt_topic, uid_str.c_str())) {
        Serial.println("UID successfully published to MQTT.");
        delay(100);
      } else {
        Serial.println("Failed to publish UID.");
      }

      cardFound = true;
      break; // Exit the loop once a card is found and processed
    }
    delay(50); // Small delay between attempts
  }

  if (!cardFound) {
    Serial.println("No NFC card found in time.");
  }
}

/**
 * @brief Converts the RFID UID byte array to a readable hex string.
 * @param buffer The byte array containing the UID.
 * @param bufferSize The size of the UID byte array.
 * @return A String object representing the UID in hex format (e.g., "AB CD 12 34").
 */
String get_rfid_uid(byte *buffer, byte bufferSize) {
  String uid = "";
  for (byte i = 0; i < bufferSize; i++) {
    uid += (buffer[i] < 0x10 ? "0" : "");
    uid += String(buffer[i], HEX);
    if (i < bufferSize - 1) {
      uid += " ";
    }
  }
  uid.toUpperCase();
  return uid;
}