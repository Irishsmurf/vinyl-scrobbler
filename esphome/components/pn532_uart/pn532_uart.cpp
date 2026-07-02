#include "pn532_uart.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"

// PN532 over HSU (High Speed UART), modeled on the pn532_i2c bus implementation.
// Frame layout on the wire is identical to I2C minus the leading "ready" status
// byte, so read_data() inserts a dummy byte to keep the base-class offsets valid.
// Based on:
// - https://cdn-shop.adafruit.com/datasheets/PN532C106_Application+Note_v1.2.pdf
// - https://www.nxp.com/docs/en/user-guide/141520.pdf (UM0701-02, §6.2.3 HSU)

namespace esphome::pn532_uart {

static const char *const TAG = "pn532_uart";

// Response frames arrive as a burst at 115200 baud; 100ms matches the
// readiness timeout used by the base class.
static const uint32_t READ_TIMEOUT_MS = 100;

bool PN532Uart::is_read_ready() { return this->available() > 0; }

bool PN532Uart::write_data(const std::vector<uint8_t> &data) {
  // Discard stale bytes so response parsing starts from a clean stream
  uint8_t stale;
  while (this->available()) {
    this->read_byte(&stale);
  }

  // HSU wakeup preamble: harmless when the chip is awake, required to wake it
  // from low-power mode. Extra bytes before the 00 00 FF start code are ignored.
  static const uint8_t WAKEUP[5] = {0x55, 0x55, 0x00, 0x00, 0x00};
  this->write_array(WAKEUP, sizeof(WAKEUP));
  this->write_array(data.data(), data.size());
  this->flush();
  return true;
}

bool PN532Uart::read_data(std::vector<uint8_t> &data, uint8_t len) {
  const uint32_t start = millis();
  while (this->available() < len) {
    if (millis() - start > READ_TIMEOUT_MS) {
      ESP_LOGV(TAG, "Timed out waiting for %d bytes from PN532!", len);
      return false;
    }
    yield();
  }

  data.resize(len + 1);
  data[0] = 0x01;  // dummy status byte, keeps base-class frame offsets identical to I2C
  return this->read_array(data.data() + 1, len);
}

bool PN532Uart::read_response(uint8_t command, std::vector<uint8_t> &data) {
  ESP_LOGV(TAG, "Reading response");

  // Frame header: PREAMBLE(00) STARTCODE(00 FF) LEN LCS TFI
  std::vector<uint8_t> header;
  if (!this->read_data(header, 6)) {
    ESP_LOGD(TAG, "No response data");
    return false;
  }

  if (header[1] != 0x00 || header[2] != 0x00 || header[3] != 0xFF) {
    // invalid packet
    ESP_LOGV(TAG, "read data invalid preamble!");
    return false;
  }

  bool valid_header = (static_cast<uint8_t>(header[4] + header[5]) == 0 &&  // LCS, len + lcs = 0
                       header[6] == 0xD5);  // TFI - frame from PN532 to system controller

  if (!valid_header) {
    ESP_LOGV(TAG, "read data invalid header!");
    return false;
  }

  // full length of message, including TFI
  const uint8_t full_len = header[4];
  // length of data, excluding TFI
  uint8_t len = full_len - 1;
  if (full_len == 0)
    len = 0;

  ESP_LOGV(TAG, "Reading response of length %d", len);

  // Remaining bytes on the wire: command echo + data (len bytes), DCS, POSTAMBLE.
  // Unlike I2C there is no NACK/re-read here: the rest of the frame is already
  // streaming in, so just keep reading.
  std::vector<uint8_t> remainder;
  if (!this->read_data(remainder, len + 2)) {
    ESP_LOGD(TAG, "No response data");
    return false;
  }

  // Reassemble as [TFI, CMD+1, data..., DCS, POSTAMBLE] so the validation below
  // matches pn532_i2c exactly
  data.clear();
  data.push_back(0xD5);
  data.insert(data.end(), remainder.begin() + 1, remainder.end());

  if (data[1] != command + 1) {
    ESP_LOGV(TAG, "read data invalid command response!");
    return false;
  }

  uint8_t checksum = 0;
  for (int i = 0; i < len + 1; i++) {
    uint8_t dat = data[i];
    checksum += dat;
  }
  checksum = ~checksum + 1;

  if (data[len + 1] != checksum) {
    ESP_LOGV(TAG, "read data invalid checksum! %02X != %02X", data[len + 1], checksum);
    return false;
  }

  if (data[len + 2] != 0x00) {
    ESP_LOGV(TAG, "read data invalid postamble!");
    return false;
  }

  data.erase(data.begin(), data.begin() + 2);  // Remove TFI and command code
  data.erase(data.end() - 2, data.end());      // Remove checksum and postamble

  return true;
}

void PN532Uart::dump_config() {
  PN532::dump_config();
  ESP_LOGCONFIG(TAG, "  Interface: UART (HSU)");
}

}  // namespace esphome::pn532_uart
