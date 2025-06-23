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
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>

// --- USER CONFIGURATION ---
// -- Wi-Fi Credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// -- MQTT Broker Configuration
const char* mqtt_server = "YOUR_MQTT_BROKER_IP_OR_HOSTNAME";
const int mqtt_port = 1883; // 1883 is standard, 8883 for SSL
const char* mqtt_topic = "vinyl/scrobble"; // The topic to publish RFID tags to
const char* mqtt_client_id = "ESP32VinylScrobbler";

// -- Hardware Pin Configuration
#define SS_PIN    5  // MFRC522 SDA/SS pin
#define RST_PIN   4  // MFRC522 RST pin
#define WAKEUP_PIN GPIO_NUM_33 // Pin to connect the wakeup button to

// --- GLOBAL VARIABLES ---
WiFiClient espClient;
PubSubClient client(espClient);
MFRC522 rfid(SS_PIN, RST_PIN);

// --- FUNCTION PROTOTYPES ---
void setup_wifi();
void reconnect_mqtt();
void scan_and_publish();
String get_rfid_uid(byte *buffer, byte bufferSize);

void setup() {
  // Start serial communication for debugging purposes
  Serial.begin(115200);
  delay(1000); // Wait for serial to initialize

  Serial.println("\nVinyl Scrobbler waking up...");

  // Configure the wakeup pin with an internal pull-up resistor
  // The button should connect this pin to GND
  pinMode(WAKEUP_PIN, INPUT_PULLUP);

  // Connect to Wi-Fi
  setup_wifi();

  // Configure MQTT client
  client.setServer(mqtt_server, mqtt_port);

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
    if (client.connect(mqtt_client_id)) {
      Serial.println("connected");
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
 * @brief Main logic function. Scans for an RFID tag and publishes its UID.
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
      
      String uid = get_rfid_uid(rfid.uid.uidByte, rfid.uid.size);
      Serial.print("Card detected! UID: ");
      Serial.println(uid);

      // Publish the UID to the MQTT topic
      if (client.publish(mqtt_topic, uid.c_str())) {
        Serial.println("UID successfully published to MQTT.");
      } else {
        Serial.println("Failed to publish UID.");
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
