/*
 * Vinyl Scrobbler - ESP32 RFID Reader & MQTT Publisher
 *
 * This sketch is for an ESP32 that:
 * 1. Wakes up from deep sleep when a button is pressed.
 * 2. Connects to Wi-Fi.
 * 3. Connects to an MQTT broker.
 * 4. Scans for an MFRC522 RFID tag.
 * 5. Publishes the tag's UID to a specific MQTT topic.
 * 6. Goes back into deep sleep to conserve battery.
 *
 * HARDWARE:
 * - ESP32 Development Board
 * - MFRC522 RFID Reader/Writer
 * - A momentary push button
 *
 * WIRING (MFRC522 -> ESP32):
 * - SDA/SS (NSS) -> GPIO 5 (Configurable)
 * - SCK (SCLK)    -> GPIO 18 (VSPI SCK)
 * - MOSI (MISO)   -> GPIO 23 (VSPI MOSI) // Note: MFRC522 MOSI to ESP32 MOSI
 * - MISO (MOSI)   -> GPIO 19 (VSPI MISO) // Note: MFRC522 MISO to ESP32 MISO
 * - RST           -> GPIO 4 (Configurable)
 * - 3.3V          -> 3.3V
 * - GND           -> GND
 *
 * WIRING (Wake-up Button):
 * - One leg of the button -> GPIO 33 (Configurable)
 * - Other leg of the button -> GND
 * (We will use the internal pull-up resistor on the ESP32)
 * * LIBRARIES TO INSTALL via Arduino Library Manager:
 * 1. "MFRC522" by GitHubCommunity
 * 2. "PubSubClient" by Nick O'Leary
 */

// --- LIBRARIES ---
#include <WiFi.h>
#include <WiFiClientSecure.h> // For MQTT over TLS
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h> // For creating JSON payloads

// --- USER CONFIGURATION ---
// IMPORTANT: These values MUST be configured before uploading to the ESP32.
// Consider using a more secure method like a configuration portal (WiFiManager)
// or build flags for production devices.

// -- Wi-Fi Credentials
#define WIFI_SSID "" // Example: "MyWiFiNetwork"
#define WIFI_PASSWORD "" // Example: "MyWiFiPassword"

// -- MQTT Broker Configuration
#define MQTT_SERVER "" // Example: "your_mqtt_broker.com"
#define MQTT_PORT 8883 // Default to 8883 for MQTTS (SSL/TLS)
#define MQTT_USER "" // Example: "mqtt_username", leave empty if no auth
#define MQTT_PASSWORD "" // Example: "mqtt_password", leave empty if no auth
#define MQTT_TOPIC "vinyl/scrobble" // The topic to publish RFID tags to
#define MQTT_CLIENT_ID "ESP32VinylScrobbler"

// -- Device Specific Configuration --
// This User ID should correspond to a Firebase User ID for associating the scrobbles.
// This needs to be provisioned onto the device securely.
#define USER_ID_FOR_ESP32 "" // Example: "firebase_user_id_123"

// -- Hardware Pin Configuration
#define SS_PIN    5  // MFRC522 SDA/SS pin
#define RST_PIN   4  // MFRC522 RST pin
#define WAKEUP_PIN GPIO_NUM_33 // Pin to connect the wakeup button to

// --- MQTT Root CA Certificate (Optional, but recommended for MQTTS) ---
// If your MQTT broker uses a self-signed certificate or one not in ESP32's default store,
// you'll need to provide its Root CA certificate here.
// Example:
// const char* mqtt_root_ca = \
// "-----BEGIN CERTIFICATE-----\n" \
// "MIID... (your CA certificate) ...END CERTIFICATE-----\n";
// If using a well-known CA, you might not need this, or set it to NULL.
const char* mqtt_root_ca = NULL;

// --- GLOBAL VARIABLES ---
WiFiClientSecure espSecureClient; // Use WiFiClientSecure for MQTTS
PubSubClient client(espSecureClient);
MFRC522 rfid(SS_PIN, RST_PIN);

// --- FUNCTION PROTOTYPES ---
void setup_wifi();
void reconnect_mqtt();
void scan_and_publish();
String get_rfid_uid(byte *buffer, byte bufferSize);
bool check_configuration();

void setup() {
  // Start serial communication for debugging purposes
  Serial.begin(115200);
  delay(1000); // Wait for serial to initialize

  Serial.println("\nVinyl Scrobbler waking up...");

  // Check if configuration is set
  if (!check_configuration()) {
    Serial.println("CRITICAL: Configuration is not set. Please define WIFI_SSID, WIFI_PASSWORD, MQTT_SERVER, and USER_ID_FOR_ESP32.");
    Serial.println("Going to sleep to prevent further issues.");
    esp_deep_sleep_start(); // Sleep indefinitely
  }

  // Configure the wakeup pin with an internal pull-up resistor
  pinMode(WAKEUP_PIN, INPUT_PULLUP);

  // Configure WiFiClientSecure (for MQTTS)
  if (mqtt_root_ca != NULL) {
    espSecureClient.setCACert(mqtt_root_ca);
  } else {
    // If no specific CA is provided, you might rely on the system store.
    // For some public brokers, you might need to use setInsecure() if you skip CA validation (NOT RECOMMENDED for production).
    // espSecureClient.setInsecure(); // Use with caution!
    Serial.println("Warning: No MQTT Root CA provided. Connection might be insecure or fail if broker's CA is not recognized.");
  }

  // Connect to Wi-Fi
  setup_wifi();

  // Configure MQTT client
  client.setServer(MQTT_SERVER, MQTT_PORT);

  // Initialize RFID reader
  SPI.begin();       // Init SPI bus
  rfid.PCD_Init();   // Init MFRC522
  Serial.println("RFID Reader Initialized. Hold a card near the reader.");
  
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
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

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
    Serial.print("Attempting MQTT connection to ");
    Serial.print(MQTT_SERVER);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    bool connected = false;
    if (strlen(MQTT_USER) > 0) {
      Serial.print("Authenticating with user: ");
      Serial.println(MQTT_USER);
      connected = client.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD);
    } else {
      Serial.println("Connecting without MQTT username/password.");
      connected = client.connect(MQTT_CLIENT_ID);
    }

    if (connected) {
      Serial.println("MQTT connected!");
    } else {
      Serial.print("MQTT connection failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      // Print more details for SSL/TLS connection errors
      char lastError[100];
      espSecureClient.lastError(lastError, 100);
      Serial.print("WiFiClientSecure last error: ");
      Serial.println(lastError);

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
 * @brief Main logic function. Scans for an RFID tag and publishes its UID and UserID as JSON.
 */
void scan_and_publish() {
  // Ensure we have an MQTT connection
  if (!client.connected()) {
    reconnect_mqtt();
  }
  client.loop(); // Allow the MQTT client to process messages

  Serial.println("Scanning for RFID tag...");

  // Try to find a new card for a few seconds
  unsigned long startTime = millis();
  bool cardFound = false;

  while(millis() - startTime < 5000) { // Scan for 5 seconds
    // Look for new cards
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String rfidUid = get_rfid_uid(rfid.uid.uidByte, rfid.uid.size);
      Serial.print("Card detected! UID: ");
      Serial.println(rfidUid);

      // Create JSON payload
      StaticJsonDocument<128> jsonDoc; // Adjust size as needed
      jsonDoc["rfid"] = rfidUid;
      jsonDoc["userId"] = USER_ID_FOR_ESP32;

      char jsonBuffer[128];
      size_t n = serializeJson(jsonDoc, jsonBuffer);

      // Publish the JSON payload to the MQTT topic
      Serial.print("Publishing to MQTT topic ");
      Serial.print(MQTT_TOPIC);
      Serial.print(": ");
      Serial.println(jsonBuffer);

      if (client.publish(MQTT_TOPIC, jsonBuffer, n)) {
        Serial.println("Message successfully published to MQTT.");
      } else {
        Serial.println("Failed to publish message to MQTT.");
      }
      
      cardFound = true;
      rfid.PICC_HaltA(); // Halt PICC
      rfid.PCD_StopCrypto1(); // Stop encryption on PCD
      break; // Exit the loop once a card is found and processed
    }
    delay(50);
  }

  if (!cardFound) {
    Serial.println("No RFID card found in time.");
  }
}


/**
 * @brief Checks if essential configuration parameters are set.
 * @return True if configuration seems minimally valid, false otherwise.
 */
bool check_configuration() {
  bool configured = true;
  if (strlen(WIFI_SSID) == 0) {
    Serial.println("Error: WIFI_SSID is not defined.");
    configured = false;
  }
  if (strlen(WIFI_PASSWORD) == 0) {
    Serial.println("Error: WIFI_PASSWORD is not defined.");
    configured = false;
  }
  if (strlen(MQTT_SERVER) == 0) {
    Serial.println("Error: MQTT_SERVER is not defined.");
    configured = false;
  }
   if (strlen(USER_ID_FOR_ESP32) == 0) {
    Serial.println("Error: USER_ID_FOR_ESP32 is not defined.");
    configured = false;
  }
  // MQTT_USER and MQTT_PASSWORD can be empty for anonymous connection.
  // MQTT_TOPIC has a default.
  return configured;
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
