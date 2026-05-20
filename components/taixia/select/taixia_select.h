#pragma once

#include "esphome/core/component.h"
#include "esphome/components/select/select.h"
#include "../taixia.h"

#include <vector>

namespace esphome {
namespace taixia {

class AirConditionerSelect : public select::Select, public TaiXiaListener, public Component {
 public:
  void dump_config() override;

  void set_fuzzy_mode_select(select::Select *select) { this->fuzzy_mode_select_ = select; }
  void set_display_mode_select(select::Select *select) { this->display_mode_select_ = select; }
  void set_motion_detect_select(select::Select *select) { this->motion_detect_select_ = select; }
  void set_swing_vertical_level_select(select::Select *select) { this->swing_vertical_level_select_ = select; }
  void set_swing_horizontal_level_select(select::Select *select) { this->swing_horizontal_level_select_ = select; }
  void set_quick_mode_select(select::Select *select) { this->quick_mode_select_ = select; }

  void set_service_id(uint8_t service_id) { this->service_id_ = service_id; }
  void set_select_mappings(std::vector<uint8_t> mappings) { this->mappings_ = std::move(mappings); }

  void set_taixia_parent(TaiXia *parent) { this->parent_ = parent; }

  // Cross-publish helper: find the label whose mapping equals `value` and
  // publish_state it, also engaging the command lock so an in-flight
  // readback doesn't overwrite. Used by motion_detect's control() to sync
  // sibling swing selects to "0" (and back) without waiting for poll.
  void publish_value(uint8_t value);

 protected:
  void control(const std::string &value) override;

  TaiXia *parent_;
  uint8_t service_id_;
  std::vector<uint8_t> mappings_;

  // Command lock: after write, hold the optimistic value for ~3 s so that
  // device readback during the round trip doesn't flash UI back to the old
  // state. Same pattern as TaiXiaSwitch / TaiXiaClimate.
  bool command_active_{false};
  static constexpr const char *COMMAND_TIMEOUT_NAME = "taixia_select_cmd";

  // motion_detect (H'19) state-machine helpers:
  // Panasonic's remote 動向感應 button forces H'0F/H'11 to 0 when activated
  // and restores them when deactivated. The AC firmware doesn't do this
  // automatically when H'19 is written via TaiSEIA, so we replicate the
  // behaviour here. Every AirConditionerSelect instance tracks all three
  // values so the swing selects can refuse writes while motion is active.
  uint8_t current_swing_vert_{0xFF};   // last seen H'0F value
  uint8_t current_swing_horiz_{0xFF};  // last seen H'11 value
  uint8_t current_motion_{0xFF};       // last seen H'19 value (any non-0 = active)
  uint8_t saved_swing_vert_{0xFF};     // restore target when motion → 0
  uint8_t saved_swing_horiz_{0xFF};    // restore target when motion → 0

  select::Select *fuzzy_mode_select_{nullptr};
  select::Select *display_mode_select_{nullptr};
  select::Select *motion_detect_select_{nullptr};
  select::Select *swing_vertical_level_select_{nullptr};
  select::Select *swing_horizontal_level_select_{nullptr};
  select::Select *quick_mode_select_{nullptr};

  void handle_response(std::vector<uint8_t> &response) override;
};

class WashingMachineSelect : public select::Select, public TaiXiaListener, public Component {
 public:
  void dump_config() override;

  void set_wash_program_select(select::Select *select) { this->wash_program_select_ = select; }
  void set_wash_other_function_select(select::Select *select) { this->wash_other_function_select_ = select; }
  void set_wash_mode_select(select::Select *select) { this->wash_mode_select_ = select; }
  void set_warm_water_program_select(select::Select *select) { this->warm_water_program_select_ = select; }
  void set_service_id(uint8_t service_id) { this->service_id_ = service_id; }
  void set_select_mappings(std::vector<uint8_t> mappings) { this->mappings_ = std::move(mappings); }

  void set_taixia_parent(TaiXia *parent) { this->parent_ = parent; }

 protected:
  void control(const std::string &value) override;

  TaiXia *parent_;
  uint8_t service_id_;
  std::vector<uint8_t> mappings_;

  select::Select *wash_program_select_{nullptr};
  select::Select *wash_other_function_select_{nullptr};
  select::Select *wash_mode_select_{nullptr};
  select::Select *warm_water_program_select_{nullptr};

  void handle_response(std::vector<uint8_t> &response) override;
};

class DehumidifierSelect : public select::Select, public TaiXiaListener, public Component {
 public:
  void dump_config() override;

  void set_operating_program_select(select::Select *select) { this->operating_program_select_ = select; }
  void set_air_purfifier_select(select::Select *select) { this->air_purfifier_select_ = select; }
  void set_sound_select(select::Select *select) { this->sound_select_ = select; }
  void set_service_id(uint8_t service_id) { this->service_id_ = service_id; }
  void set_select_mappings(std::vector<uint8_t> mappings) { this->mappings_ = std::move(mappings); }

  void set_taixia_parent(TaiXia *parent) { this->parent_ = parent; }

 protected:
  void control(const std::string &value) override;

  TaiXia *parent_;
  uint8_t service_id_;
  std::vector<uint8_t> mappings_;

  select::Select *operating_program_select_{nullptr};
  select::Select *air_purfifier_select_{nullptr};
  select::Select *sound_select_{nullptr};

  void handle_response(std::vector<uint8_t> &response) override;
};

class AirPurifierSelect : public select::Select, public TaiXiaListener, public Component {
 public:
  void dump_config() override;

  void set_operating_program_select(select::Select *select) { this->operating_program_select_ = select; }
  void set_service_id(uint8_t service_id) { this->service_id_ = service_id; }
  void set_select_mappings(std::vector<uint8_t> mappings) { this->mappings_ = std::move(mappings); }

  void set_taixia_parent(TaiXia *parent) { this->parent_ = parent; }

 protected:
  void control(const std::string &value) override;

  TaiXia *parent_;
  uint8_t service_id_;
  std::vector<uint8_t> mappings_;

  select::Select *operating_program_select_{nullptr};

  void handle_response(std::vector<uint8_t> &response) override;
};

class ErvSelect : public select::Select, public TaiXiaListener, public Component {
 public:
  void dump_config() override;

  void set_ventilate_mode_select(select::Select *select) { this->ventilate_mode_select_ = select; }
  void set_pre_heat_cool_select(select::Select *select) { this->pre_heat_cool_select_ = select; }
  void set_service_id(uint8_t service_id) { this->service_id_ = service_id; }
  void set_select_mappings(std::vector<uint8_t> mappings) { this->mappings_ = std::move(mappings); }

  void set_taixia_parent(TaiXia *parent) { this->parent_ = parent; }

 protected:
  void control(const std::string &value) override;

  TaiXia *parent_;
  uint8_t service_id_;
  std::vector<uint8_t> mappings_;

  select::Select *ventilate_mode_select_{nullptr};
  select::Select *pre_heat_cool_select_{nullptr};

  void handle_response(std::vector<uint8_t> &response) override;
};

class ElectricFanSelect : public select::Select, public TaiXiaListener, public Component {
 public:
  void dump_config() override;

  void set_operating_program_select(select::Select *select) { this->operating_program_select_ = select; }
  void set_service_id(uint8_t service_id) { this->service_id_ = service_id; }
  void set_select_mappings(std::vector<uint8_t> mappings) { this->mappings_ = std::move(mappings); }

  void set_taixia_parent(TaiXia *parent) { this->parent_ = parent; }

 protected:
  void control(const std::string &value) override;

  TaiXia *parent_;
  uint8_t service_id_;
  std::vector<uint8_t> mappings_;

  select::Select *operating_program_select_{nullptr};

  void handle_response(std::vector<uint8_t> &response) override;
};

}  // namespace taixia
}  // namespace esphome
