#include "bthome_mithermometer.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

#include <vector>
#include "mbedtls/ccm.h"

namespace esphome {
namespace bthome_mithermometer {

static const char *const TAG = "bthome_mithermometer";

void BTHomeMiThermometer::dump_config() {
  ESP_LOGCONFIG(TAG, "BTHome MiThermometer");
  ESP_LOGCONFIG(TAG, "  Bindkey: %s", format_hex_pretty(this->bindkey_, 16).c_str());
  LOG_SENSOR("  ", "Temperature", this->temperature_);
  LOG_SENSOR("  ", "Humidity", this->humidity_);
  LOG_SENSOR("  ", "Battery Level", this->battery_level_);
  LOG_SENSOR("  ", "Battery Voltage", this->battery_voltage_);
}

bool BTHomeMiThermometer::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  if (device.address_uint64() != this->address_) {
    ESP_LOGVV(TAG, "parse_device(): unknown MAC address.");
    return false;
  }
  ESP_LOGVV(TAG, "parse_device(): MAC address %s found.", device.address_str().c_str());

  bool success = false;
  for (auto &service_data : device.get_service_datas()) {
    auto res = parse_header_(service_data);
    if (!res.has_value()) {
      continue;
    }
    if (res->has_encryption &&
        (!(decrypt_bthome_payload(const_cast<std::vector<uint8_t> &>(service_data.data), this->bindkey_,
                                              this->address_)))) {
      continue;
    }
    if (!(parse_message_(service_data.data, *res))) {
      continue;
    }
    if (!(report_results_(res, device.address_str()))) {
      continue;
    }
    if (res->temperature.has_value() && this->temperature_ != nullptr)
      this->temperature_->publish_state(*res->temperature);
    if (res->humidity.has_value() && this->humidity_ != nullptr)
      this->humidity_->publish_state(*res->humidity);
    if (res->battery_level.has_value() && this->battery_level_ != nullptr)
      this->battery_level_->publish_state(*res->battery_level);
    if (res->battery_voltage.has_value() && this->battery_voltage_ != nullptr)
      this->battery_voltage_->publish_state(*res->battery_voltage);
    if (this->signal_strength_ != nullptr)
      this->signal_strength_->publish_state(device.get_rssi());
    success = true;
  }

  return success;
}

optional<ParseResult> BTHomeMiThermometer::parse_header_(const esp32_ble_tracker::ServiceData &service_data) {
  ParseResult result;
  if (!(service_data.uuid.contains(0x1C, 0x18) || service_data.uuid.contains(0x1E, 0x18))) {
    ESP_LOGVV(TAG, "parse_header(): no service data UUID magic bytes.");
    return {};
  }

  (service_data.uuid.contains(0x1E, 0x18)) ? result.has_encryption = true : result.has_encryption = false;

  auto raw = service_data.data;

  static uint8_t packet_id_offset;
  if(result.has_encryption) {
    packet_id_offset = raw.size() - 8;
    result.raw_offset = 0;
  }else{
    if(raw[1] == 0x00) {
      packet_id_offset = 2;
      result.raw_offset = 3;
    }else{
      ESP_LOGVV(TAG, "parse_header(): can't find packet_id");
      return {};
    }
  }

  static uint8_t last_frame_count = 0;
  if (last_frame_count == raw[packet_id_offset]) {
    ESP_LOGVV(TAG, "parse_header(): duplicate data packet received (%hhu).", last_frame_count);
    return {};
  }
  last_frame_count = raw[packet_id_offset];

  return result;
}

bool BTHomeMiThermometer::parse_message_(const std::vector<uint8_t> &message, ParseResult &result) {
  /*
  All data little endian
  uint8_t     size;   // = 11
  uint8_t     uid;    // = 0x16, 16-bit UUID
  uint16_t    UUID;   // = 0x181C or 0x181E
  uint8_t     packet_id; // optional
  int16_t     temperature;    // x 0.01 degree     [2,3]
  uint16_t    humidity;       // x 0.01 %          [6,7]
  uint8_t     battery_level;  // 0..100 %          [10]
  */

  /*
  All data little endian
  uint8_t     size;   // = 7
  uint8_t     uid;    // = 0x16, 16-bit UUID
  uint16_t    UUID;   // = 0x181C or 0x181E
  uint8_t     packet_id; // optional
  uint16_t    battery_mv;     // mV                [5,6]
  */
  const uint8_t *data = message.data();

  int data_length;
  (result.has_encryption) ? data_length = message.size() - 8 : data_length = message.size();

  if(data_length == 11 + result.raw_offset) {
    // int16_t     temperature;    // x 0.01 degree     [2,3]
    const int16_t temperature = int16_t(data[2+result.raw_offset]) | (int16_t(data[3+result.raw_offset]) << 8);
    result.temperature = temperature / 1.0e2f;

    // uint16_t    humidity;       // x 0.01 %          [6,7]
    const int16_t humidity = uint16_t(data[6+result.raw_offset]) | (uint16_t(data[7+result.raw_offset]) << 8);
    result.humidity = humidity / 1.0e2f;

    // uint8_t     battery_level;  // 0..100 %          [10]
    result.battery_level = uint8_t(data[10+result.raw_offset]);

    return true;
  }else if(data_length == 7 + result.raw_offset) {
    // uint16_t    battery_mv;     // mV                [5,6]
    const int16_t battery_voltage = uint16_t(data[5+result.raw_offset]) | (uint16_t(data[6+result.raw_offset]) << 8);
    result.battery_voltage = battery_voltage / 1.0e3f;

    return true;
  }else{
    ESP_LOGVV(TAG, "parse_message(): payload has wrong size (%d)!", data_length);
    return false;
  }
}

bool BTHomeMiThermometer::report_results_(const optional<ParseResult> &result, const std::string &address) {
  if (!result.has_value()) {
    ESP_LOGVV(TAG, "report_results(): no results available.");
    return false;
  }

  ESP_LOGD(TAG, "Got BTHome MiThermometer (%s):", address.c_str());

  if (result->temperature.has_value()) {
    ESP_LOGD(TAG, "  Temperature: %.2f °C", *result->temperature);
  }
  if (result->humidity.has_value()) {
    ESP_LOGD(TAG, "  Humidity: %.2f %%", *result->humidity);
  }
  if (result->battery_level.has_value()) {
    ESP_LOGD(TAG, "  Battery Level: %.0f %%", *result->battery_level);
  }
  if (result->battery_voltage.has_value()) {
    ESP_LOGD(TAG, "  Battery Voltage: %.3f V", *result->battery_voltage);
  }

  return true;
}

void BTHomeMiThermometer::set_bindkey(const std::string &bindkey) {
  memset(bindkey_, 0, 16);
  if (bindkey.size() != 32) {
    return;
  }
  char temp[3] = {0};
  for (int i = 0; i < 16; i++) {
    strncpy(temp, &(bindkey.c_str()[i * 2]), 2);
    bindkey_[i] = std::strtoul(temp, nullptr, 16);
  }
}

bool BTHomeMiThermometer::decrypt_bthome_payload(std::vector<uint8_t> &raw, const uint8_t *bindkey, const uint64_t &address) {
  if (!((raw.size() == 15) || ((raw.size() == 19)))) {
    ESP_LOGVV(TAG, "decrypt_bthome_payload(): data packet has wrong size (%d)!", raw.size());
    ESP_LOGVV(TAG, "  Packet : %s", format_hex_pretty(raw.data(), raw.size()).c_str());
    return false;
  }

  uint8_t mac_address[6] = {0};
  mac_address[0] = (uint8_t) (address >> 40);
  mac_address[1] = (uint8_t) (address >> 32);
  mac_address[2] = (uint8_t) (address >> 24);
  mac_address[3] = (uint8_t) (address >> 16);
  mac_address[4] = (uint8_t) (address >> 8);
  mac_address[5] = (uint8_t) (address >> 0);

  BTHomeAESVector vector{.key = {0},
                         .plaintext = {0},
                         .ciphertext = {0},
                         .authdata = {0x11},
                         .iv = {0},
                         .tag = {0},
                         .keysize = 16,
                         .authsize = 1,
                         .datasize = 0,
                         .tagsize = 4,
                         .ivsize = 12};

  vector.datasize = raw.size() - 8;
  int cipher_pos = 0;

  const uint8_t *v = raw.data();

  memcpy(vector.key, bindkey, vector.keysize);
  memcpy(vector.ciphertext, v + cipher_pos, vector.datasize);
  memcpy(vector.tag, v + raw.size() - vector.tagsize, vector.tagsize);
  memcpy(vector.iv, mac_address, 6);             // MAC address reverse
  vector.iv[6] = 0x1E;
  vector.iv[7] = 0x18;
  memcpy(vector.iv + 8, v + raw.size() - 8, 4);  // count_id

  mbedtls_ccm_context ctx;
  mbedtls_ccm_init(&ctx);

  int ret = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, vector.key, vector.keysize * 8);
  if (ret) {
    ESP_LOGVV(TAG, "decrypt_bthome_payload(): mbedtls_ccm_setkey() failed.");
    mbedtls_ccm_free(&ctx);
    return false;
  }

  ret = mbedtls_ccm_auth_decrypt(&ctx, vector.datasize, vector.iv, vector.ivsize, vector.authdata, vector.authsize,
                                 vector.ciphertext, vector.plaintext, vector.tag, vector.tagsize);
  if (ret) {
    ESP_LOGVV(TAG, "decrypt_bthome_payload(): authenticated decryption failed.");
    ESP_LOGVV(TAG, "  MAC address : %s", format_hex_pretty(mac_address, 6).c_str());
    ESP_LOGVV(TAG, "       Packet : %s", format_hex_pretty(raw.data(), raw.size()).c_str());
    ESP_LOGVV(TAG, "          Key : %s", format_hex_pretty(vector.key, vector.keysize).c_str());
    ESP_LOGVV(TAG, "           Iv : %s", format_hex_pretty(vector.iv, vector.ivsize).c_str());
    ESP_LOGVV(TAG, "       Cipher : %s", format_hex_pretty(vector.ciphertext, vector.datasize).c_str());
    ESP_LOGVV(TAG, "          Tag : %s", format_hex_pretty(vector.tag, vector.tagsize).c_str());
    mbedtls_ccm_free(&ctx);
    return false;
  }

  // replace encrypted payload with plaintext
  uint8_t *p = vector.plaintext;
  for (std::vector<uint8_t>::iterator it = raw.begin() + cipher_pos; it != raw.begin() + cipher_pos + vector.datasize;
       ++it) {
    *it = *(p++);
  }

  ESP_LOGVV(TAG, "decrypt_bthome_payload(): authenticated decryption passed.");
  ESP_LOGVV(TAG, "  Plaintext : %s", format_hex_pretty(raw.data() + cipher_pos, vector.datasize).c_str());

  mbedtls_ccm_free(&ctx);
  return true;
}

}  // namespace bthome_mithermometer
}  // namespace esphome

#endif
