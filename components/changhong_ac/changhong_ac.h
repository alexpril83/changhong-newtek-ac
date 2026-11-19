#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include <vector>

namespace esphome {
namespace changhong_ac {

class ChanghongAC;

class ChanghongDisplaySwitch : public switch_::Switch, public Component {
 public:
  void set_parent(ChanghongAC *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;
  ChanghongAC *parent_{nullptr};
};

class ChanghongAC : public climate::Climate, public uart::UARTDevice, public PollingComponent {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;

  void set_display_switch(ChanghongDisplaySwitch *sw) {
    this->display_switch_ = sw;
    sw->set_parent(this);
  }
  void set_error_code_sensor(sensor::Sensor *sens) { this->error_code_sensor_ = sens; }
  void set_display_state(bool state);

 protected:
  void control(const climate::ClimateCall &call) override;
  climate::ClimateTraits traits() override;

  void send_command_packet_(bool is_user_action);
  void send_ping_packet_();
  void parse_rx_packet_(const std::vector<uint8_t> &packet);
  uint8_t calc_checksum_(const uint8_t *data, size_t len);

  std::vector<uint8_t> rx_buffer_;
  uint32_t last_rx_time_{0};
  uint8_t session_hi_{0x12};
  uint8_t session_lo_{0x31};
  bool display_on_{true};

  ChanghongDisplaySwitch *display_switch_{nullptr};
  sensor::Sensor *error_code_sensor_{nullptr};
};

}  // namespace changhong_ac
}  // namespace esphome