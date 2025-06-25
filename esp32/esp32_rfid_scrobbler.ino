/*
 * Vinyl Scrobbler - ESP32 PN532 NFC Reader & MQTT Publisher
 *
 * MODIFIED FOR CONTINUOUS OPERATION (NO DEEP SLEEP)
 *
 * This sketch is for an ESP32 that:
 * 1. Connects to Wi-Fi and an MQTT broker on startup.
 * 2. Continuously scans for a PN532 NFC tag.
 * 3. Publishes the tag's UID to a specific MQTT topic upon successful read.
 * 4. Stays active and repeats the scanning process.
 *
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

  Serial.println("\nVinyl Scrobbler - Continuous Mode");

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
    Serial.println("Didn't find PN532 board. Halting.");
    while(1); // Stop forever
  }

  // Got ok data, print it out!
  Serial.print("Found PN532 with firmware version: ");
  Serial.print((versiondata >> 24) & 0xFF, HEX);
  Serial.print(".");
  Serial.println((versiondata >> 16) & 0xFF, HEX);
  
  // Configure the PN532 to read RFID tags
  nfc.SAMConfig();
  
  Serial.println("NFC Reader Initialized. Ready to scan.");
}


// The loop function now contains the core logic and runs continuously.
void loop() {
  // Ensure we have a connection to the MQTT broker
  if (!client.connected()) {
    reconnect_mqtt();
  }
  // Allow the MQTT client to process messages and maintain its connection
  client.loop();

  // This function will now be called repeatedly
  scan_and_publish();
  
  // A small delay to prevent the loop from running too fast and overwhelming the system.
  delay(250); 
}

/**
 * @brief Connects to the Wi-Fi network.
 */
void setup_wifi() {
  delay(10);
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

/**
 * @brief Connects/reconnects to the MQTT broker.
 */
void reconnect_mqtt() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect with Last Will and Testament
    if (client.connect(mqtt_client_id, mqtt_user, mqtt_password, will_topic, will_qos, will_retain, will_message)) {
      Serial.println("connected");
      // Publish an "online" message to the status topic
      client.publish(will_topic, "online");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/**
 * @brief Scans for an NFC tag and publishes its UID.
 * This is now non-blocking and will simply return if no card is found.
 */
void scan_and_publish() {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 }; // Buffer to store the returned UID
  uint8_t uidLength;                      // Length of the UID (4 or 7 bytes)

  // readPassiveTargetID will try to read a card for a given timeout (default 1s)
  // It is a blocking call for that duration.
  // The '1000' parameter is the timeout in milliseconds. We'll use a shorter one.
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 200);

  if (success) {
    String uid_str = get_rfid_uid(uid, uidLength);
    Serial.print("Card detected! UID: ");
    Serial.println(uid_str);

    // Publish the UID to the MQTT topic
    if (client.publish(mqtt_topic, uid_str.c_str())) {
      Serial.println("UID successfully published to MQTT.");
      // Add a longer delay after a successful scan to prevent immediate re-scans of the same card
      delay(2000); 
    } else {
      Serial.println("Failed to publish UID.");
    }
  }
  // If 'success' is false, the function simply ends, and the loop() will call it again.
}

/**
 * @brief Converts the RFID UID byte array to a readable hex string.
 */
String get_rfid_uid(byte *buffer, byte bufferSize) {
  String uid = "";
  for (byte i = 0; i < bufferSize; i++) {
    uid += (buffer[i] < 0x10 ? "0" : "");
    uid += String(buffer[i], HEX);
    if (i < bufferSize - 1) {
      uid += ":";
    }
  }
  uid.toLowerCase();
  return uid;
}