#include "changhong_ac.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include <cmath>

namespace esphome {
namespace changhong_ac {

static const char *const TAG = "changhong_ac";

void ChanghongDisplaySwitch::write_state(bool state) {
  this->publish_state(state);
  if (this->parent_ != nullptr) {
    this->parent_->set_display_state(state);
  }
}

void ChanghongAC::set_display_state(bool state) {
  if (this->display_on_ != state) {
    this->display_on_ = state;
    this->send_command_packet_(true);
  }
}

void ChanghongAC::setup() {
  this->session_hi_ = (uint8_t) (random_uint32() & 0xFF);
  this->session_lo_ = (uint8_t) (random_uint32() & 0xFF);
  if (this->session_hi_ == 0x00 && this->session_lo_ == 0x00) {
    this->session_hi_ = 0x17;
    this->session_lo_ = 0x2A;
  }

  this->mode = climate::CLIMATE_MODE_OFF;
  this->target_temperature = 24.0f;
  this->fan_mode = climate::CLIMATE_FAN_AUTO;
  this->swing_mode = climate::CLIMATE_SWING_OFF;
  this->preset = climate::CLIMATE_PRESET_NONE;

  if (this->display_switch_ != nullptr) {
    this->display_switch_->publish_state(this->display_on_);
  }

  ESP_LOGI(TAG, "Changhong AC initialized. Session ID: 0x%02X 0x%02X", this->session_hi_, this->session_lo_);
}

void ChanghongAC::dump_config() {
  LOG_CLIMATE("", "Changhong / DEXP AC", this);
  this->check_uart_settings(9600);
}

climate::ClimateTraits ChanghongAC::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(16.0f);
  traits.set_visual_max_temperature(32.0f);
  traits.set_visual_temperature_step(1.0f);

  traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_HEAT_COOL,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_DRY,
      climate::CLIMATE_MODE_FAN_ONLY,
  });

  traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO,
      climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MEDIUM,
      climate::CLIMATE_FAN_HIGH,
  });

  traits.set_supported_swing_modes({
      climate::CLIMATE_SWING_OFF,
      climate::CLIMATE_SWING_VERTICAL,
      climate::CLIMATE_SWING_HORIZONTAL,
      climate::CLIMATE_SWING_BOTH,
  });

  traits.set_supported_presets({
      climate::CLIMATE_PRESET_NONE,
      climate::CLIMATE_PRESET_BOOST,
      climate::CLIMATE_PRESET_SLEEP,
      climate::CLIMATE_PRESET_COMFORT,
  });

  return traits;
}

void ChanghongAC::update() {
  this->send_ping_packet_();
}

void ChanghongAC::loop() {
  while (this->available()) {
    uint8_t byte;
    this->read_byte(&byte);

    uint32_t now = millis();
    if (!this->rx_buffer_.empty() && (now - this->last_rx_time_ > 100)) {
      this->rx_buffer_.clear();
    }
    this->last_rx_time_ = now;

    if (this->rx_buffer_.empty() && byte != 0xAA) {
      continue;
    }

    this->rx_buffer_.push_back(byte);

    if (this->rx_buffer_.size() >= 2) {
      size_t expected_len = this->rx_buffer_.at(1) + 2;
      if (expected_len > 64) {
        this->rx_buffer_.clear();
        continue;
      }

      if (this->rx_buffer_.size() == expected_len) {
        this->parse_rx_packet_(this->rx_buffer_);
        this->rx_buffer_.clear();
      }
    }
  }
}

void ChanghongAC::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value()) {
    this->mode = *call.get_mode();
  }
  if (call.get_target_temperature().has_value()) {
    this->target_temperature = *call.get_target_temperature();
  }
  if (call.get_fan_mode().has_value()) {
    this->fan_mode = *call.get_fan_mode();
  }
  if (call.get_swing_mode().has_value()) {
    this->swing_mode = *call.get_swing_mode();
  }
  if (call.get_preset().has_value()) {
    this->preset = *call.get_preset();
  }

  this->send_command_packet_(true);
  this->publish_state();
}

uint8_t ChanghongAC::calc_checksum_(const uint8_t *data, size_t len) {
  uint32_t sum = 0;
  for (size_t i = 0; i < len; i++) {
    sum += *(data + i);
  }
  return (uint8_t) (sum & 0xFF);
}

void ChanghongAC::send_ping_packet_() {
  std::vector<uint8_t> ping = {0xAA, 0x02, 0x01, 0xAD};
  this->write_array(ping);
  this->flush();
  ESP_LOGV(TAG, "TX Ping: AA 02 01 AD");
}
void ChanghongAC::send_command_packet_(bool is_user_action) {
  std::vector<uint8_t> pkt(22, 0x00);

  pkt.at(0) = 0xAA;
  pkt.at(1) = 0x14;
  pkt.at(2) = 0x02;
  pkt.at(3) = (this->mode == climate::CLIMATE_MODE_OFF) ? 0x00 : 0x01;

  bool v_swing = (this->swing_mode == climate::CLIMATE_SWING_VERTICAL ||
                  this->swing_mode == climate::CLIMATE_SWING_BOTH);
  pkt.at(5) = v_swing ? 0x00 : 0x06;
  pkt.at(6) = is_user_action ? 0x03 : 0x0B;

  switch (this->mode) {
    case climate::CLIMATE_MODE_HEAT_COOL: pkt.at(7) = 0x00; break;
    case climate::CLIMATE_MODE_COOL: pkt.at(7) = 0x01; break;
    case climate::CLIMATE_MODE_DRY: pkt.at(7) = 0x02; break;
    case climate::CLIMATE_MODE_HEAT: pkt.at(7) = 0x03; break;
    case climate::CLIMATE_MODE_FAN_ONLY: pkt.at(7) = 0x04; break;
    default: pkt.at(7) = 0x01; break;
  }

  uint8_t flags = this->display_on_ ? 0x10 : 0x00;
  if (this->preset.has_value()) {
    switch (*this->preset) {
      case climate::CLIMATE_PRESET_BOOST: flags |= 0x08; break;
      case climate::CLIMATE_PRESET_COMFORT: flags |= 0x04; break;
      case climate::CLIMATE_PRESET_SLEEP: flags |= 0x01; break;
      default: break;
    }
  }
  pkt.at(8) = flags;

  if (this->fan_mode.has_value()) {
    switch (*this->fan_mode) {
      case climate::CLIMATE_FAN_AUTO: pkt.at(9) = 0x00; break;
      case climate::CLIMATE_FAN_LOW: pkt.at(9) = 0x01; break;
      case climate::CLIMATE_FAN_MEDIUM: pkt.at(9) = 0x02; break;
      case climate::CLIMATE_FAN_HIGH: pkt.at(9) = 0x03; break;
      default: pkt.at(9) = 0x00; break;
    }
  }

  int temp = (int) roundf(this->target_temperature);
  if (temp < 16) temp = 16;
  if (temp > 32) temp = 32;
  pkt.at(10) = (uint8_t) temp;

  bool h_swing = (this->swing_mode == climate::CLIMATE_SWING_HORIZONTAL ||
                  this->swing_mode == climate::CLIMATE_SWING_BOTH);
  pkt.at(11) = h_swing ? 0x1D : 0x1C;

  pkt.at(17) = this->session_hi_;
  pkt.at(18) = this->session_lo_;
  pkt.at(21) = this->calc_checksum_(pkt.data(), 21);

  this->write_array(pkt);
  this->flush();
  ESP_LOGD(TAG, "TX Cmd: %s", format_hex_pretty(pkt).c_str());
}

void ChanghongAC::parse_rx_packet_(const std::vector<uint8_t> &packet) {
  ESP_LOGD(TAG, "RX Pkt (%d bytes): %s", packet.size(), format_hex_pretty(packet).c_str());
  if (packet.size() < 22) return;

  uint8_t expected_cs = this->calc_checksum_(packet.data(), packet.size() - 1);
  if (expected_cs != packet.back()) {
    ESP_LOGW(TAG, "Checksum mismatch! Expected 0x%02X, got 0x%02X", expected_cs, packet.back());
    return;
  }

  bool state_changed = false;

  climate::ClimateMode new_mode = this->mode;
  if (packet.at(3) == 0x00) {
    new_mode = climate::CLIMATE_MODE_OFF;
  } else {
    switch (packet.at(7)) {
      case 0x00: new_mode = climate::CLIMATE_MODE_HEAT_COOL; break;
      case 0x01: new_mode = climate::CLIMATE_MODE_COOL; break;
      case 0x02: new_mode = climate::CLIMATE_MODE_DRY; break;
      case 0x03: new_mode = climate::CLIMATE_MODE_HEAT; break;
      case 0x04: new_mode = climate::CLIMATE_MODE_FAN_ONLY; break;
    }
  }
  if (this->mode != new_mode) {
    this->mode = new_mode;
    state_changed = true;
  }

  uint8_t flags = packet.at(8);
  bool new_display = (flags & 0x10) != 0;
  if (this->display_on_ != new_display) {
    this->display_on_ = new_display;
    if (this->display_switch_ != nullptr) {
      this->display_switch_->publish_state(this->display_on_);
    }
  }

  climate::ClimatePreset new_preset = climate::CLIMATE_PRESET_NONE;
  if (flags & 0x08) {
    new_preset = climate::CLIMATE_PRESET_BOOST;
  } else if (flags & 0x04) {
    new_preset = climate::CLIMATE_PRESET_COMFORT;
  } else if (flags & 0x01) {
    new_preset = climate::CLIMATE_PRESET_SLEEP;
  }
  if (this->preset != new_preset) {
    this->preset = new_preset;
    state_changed = true;
  }

  climate::ClimateFanMode new_fan = climate::CLIMATE_FAN_AUTO;
  switch (packet.at(9)) {
    case 0x00: new_fan = climate::CLIMATE_FAN_AUTO; break;
    case 0x01: new_fan = climate::CLIMATE_FAN_LOW; break;
    case 0x02: new_fan = climate::CLIMATE_FAN_MEDIUM; break;
    case 0x03: new_fan = climate::CLIMATE_FAN_HIGH; break;
  }
  if (this->fan_mode != new_fan) {
    this->fan_mode = new_fan;
    state_changed = true;
  }

  if (packet.at(10) >= 16 && packet.at(10) <= 32) {
    float new_target = (float) packet.at(10);
    if (this->target_temperature != new_target) {
      this->target_temperature = new_target;
      state_changed = true;
    }
  }

  uint8_t raw_room_temp = packet.at(11);
  if (raw_room_temp < 0x80 && raw_room_temp > 0) {
    float new_current = (float) raw_room_temp;
    if (this->current_temperature != new_current) {
      this->current_temperature = new_current;
      state_changed = true;
    }
  }

  bool v_swing = (packet.at(5) == 0x00);
  bool h_swing = (this->swing_mode == climate::CLIMATE_SWING_HORIZONTAL ||
                  this->swing_mode == climate::CLIMATE_SWING_BOTH);
  climate::ClimateSwingMode new_swing = climate::CLIMATE_SWING_OFF;
  if (v_swing && h_swing) {
    new_swing = climate::CLIMATE_SWING_BOTH;
  } else if (v_swing) {
    new_swing = climate::CLIMATE_SWING_VERTICAL;
  } else if (h_swing) {
    new_swing = climate::CLIMATE_SWING_HORIZONTAL;
  }
  if (this->swing_mode != new_swing) {
    this->swing_mode = new_swing;
    state_changed = true;
  }

  if (this->error_code_sensor_ != nullptr) {
    float err = (float) packet.at(19);
    if (!this->error_code_sensor_->has_state() || this->error_code_sensor_->state != err) {
      this->error_code_sensor_->publish_state(err);
    }
  }

  if (state_changed) {
    this->publish_state();
  }
}

}  // namespace changhong_ac
}  // namespace esphome
