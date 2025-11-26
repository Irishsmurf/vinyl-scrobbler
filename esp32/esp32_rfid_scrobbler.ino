/*
 * Vinyl Scrobbler - ESP32 PN532 NFC Reader & MQTT Publisher
 *
 * This sketch enables an ESP32 with a PN532 NFC reader to continuously scan for NFC tags
 * and publish their UIDs to an MQTT broker. It's designed for constant operation.
 *
 * Core functionalities:
 * 1. Establishes a connection to a Wi-Fi network.
 * 2. Connects to an MQTT broker with a Last Will and Testament.
 * 3. Initializes the PN532 NFC reader.
 * 4. Continuously scans for NFC tags.
 * 5. Publishes the UID of a detected tag to a specified MQTT topic.
 * 6. Maintains connections and handles automatic reconnections.
 */

// --- LIBRARIES ---
#include <WiFi.h>
#include <PubSubClient.h>
#include <PN532_HSU.h>
#include <PN532.h>
#include "credentials.h" // Note: Contains sensitive data (WiFi, MQTT credentials)

// -- MQTT Broker Configuration
const char* mqtt_server = "coventry.paddez.com";
const int mqtt_port = 1883;
const char* mqtt_topic = "VinylScrobbler/rfid/reads";
const char* mqtt_client_id = "ESP32VinylScrobbler";

// -- MQTT Last Will and Testament (LWT) Configuration
const char* will_topic = "VinylScrobbler/status";
const char* will_message = "offline"; // Message to publish if connection is lost
boolean will_retain = false;
byte will_qos = 0;

// --- GLOBAL OBJECTS ---
WiFiClient espClient;
PubSubClient client(espClient);
PN532_HSU pn532hsu(Serial2); // Use Serial2 for HSU (Hardware Serial UART)
PN532 nfc(pn532hsu);

// --- FUNCTION PROTOTYPES ---
void setup_wifi();
void reconnect_mqtt();
void scan_and_publish();
String get_rfid_uid(byte *buffer, byte bufferSize);

/**
 * @brief Initializes serial communication, Wi-Fi, MQTT, and the NFC reader.
 * This function runs once at startup.
 */
void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); } // Wait for serial connection

  Serial.println("\nVinyl Scrobbler - Continuous Mode");

  setup_wifi();

  client.setServer(mqtt_server, mqtt_port);

  // Initialize the PN532 reader
  Serial2.begin(115200, SERIAL_8N1, 16, 17); // RX, TX pins for ESP32
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("Didn't find PN532 board. Halting.");
    while(1); // Stop execution
  }

  Serial.print("Found PN532 with firmware version: ");
  Serial.print((versiondata >> 24) & 0xFF, HEX);
  Serial.print(".");
  Serial.println((versiondata >> 16) & 0xFF, HEX);
  
  nfc.SAMConfig(); // Configure the PN532 to read RFID tags
  
  Serial.println("NFC Reader Initialized. Ready to scan.");
}

/**
 * @brief The main loop of the application.
 * Handles MQTT connection maintenance, message processing, and initiates NFC scans.
 */
void loop() {
  if (!client.connected()) {
    reconnect_mqtt();
  }
  client.loop(); // Allow the MQTT client to process incoming messages

  scan_and_publish(); // Perform a scan attempt
  
  delay(250); // Small delay to prevent busy-waiting
}

/**
 * @brief Establishes a connection to the configured Wi-Fi network.
 * It will block until a connection is successful.
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
 * @brief Connects or reconnects to the MQTT broker.
 * If the connection fails, it will retry every 5 seconds.
 * Upon successful connection, it publishes an "online" message.
 */
void reconnect_mqtt() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(mqtt_client_id, mqtt_user, mqtt_password, will_topic, will_qos, will_retain, will_message)) {
      Serial.println("connected");
      client.publish(will_topic, "online");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000); // Wait before retrying
    }
  }
}

/**
 * @brief Attempts to scan for an NFC tag within a short timeout.
 * If a tag is found, its UID is formatted and published to the MQTT topic.
 */
void scan_and_publish() {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 }; // Buffer for the UID data
  uint8_t uidLength;                      // Variable to store the UID length

  // Attempt to read a card with a 200ms timeout
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 200);

  if (success) {
    String uid_str = get_rfid_uid(uid, uidLength);
    Serial.print("Card detected! UID: ");
    Serial.println(uid_str);

    if (client.publish(mqtt_topic, uid_str.c_str())) {
      Serial.println("UID successfully published to MQTT.");
      delay(2000); // Wait after a successful scan to prevent re-reading
    } else {
      Serial.println("Failed to publish UID.");
    }
  }
}

/**
 * @brief Converts a byte array containing an RFID UID into a lowercase, colon-separated hex string.
 * @param buffer Pointer to the byte array (UID).
 * @param bufferSize The size of the byte array.
 * @return A String object representing the formatted UID.
 */
String get_rfid_uid(byte *buffer, byte bufferSize) {
  String uid = "";
  for (byte i = 0; i < bufferSize; i++) {
    uid += (buffer[i] < 0x10 ? "0" : ""); // Add leading zero if needed
    uid += String(buffer[i], HEX);
    if (i < bufferSize - 1) {
      uid += ":";
    }
  }
  uid.toLowerCase();
  return uid;
}
