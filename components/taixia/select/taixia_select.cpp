#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "taixia_select.h"

namespace esphome {
namespace taixia {

static const char *const TAG = "taixia.select";

  void AirConditionerSelect::publish_value(uint8_t value) {
    auto it = std::find(this->mappings_.cbegin(), this->mappings_.cend(), value);
    if (it == this->mappings_.cend())
      return;
    size_t idx = std::distance(this->mappings_.cbegin(), it);
    auto label = this->at(idx);
    if (!label.has_value())
      return;
    this->publish_state(label.value());
    // Engage own command lock so an in-flight poll readback doesn't flash
    // the UI back before the AC has actually adopted the new value.
    this->command_active_ = true;
    this->cancel_timeout(COMMAND_TIMEOUT_NAME);
    this->set_timeout(COMMAND_TIMEOUT_NAME, 3000, [this]() {
      this->command_active_ = false;
    });
  }

  static inline uint16_t get_u16(std::vector<uint8_t> &response, int start) {
    return (response[start] << 8) + response[start + 1];
  }

  static inline size_t get_mapping_idx(std::vector<uint8_t> &response, int start, std::vector<uint8_t> mappings) {
    if ((response[start + 1] == 0xFF) && (response[start + 2] == 0xFF)) {
      return -1;
    } else {
      uint8_t enum_value = get_u16(response, start + 1);

      auto it = std::find(mappings.cbegin(), mappings.cend(), enum_value);
      if (it == mappings.end()) {
        ESP_LOGW(TAG, "Invalid value %u", enum_value);
        return -1;
      }

      return std::distance(mappings.cbegin(), it);
    }

  }

  void AirConditionerSelect::dump_config() {
    ESP_LOGCONFIG(TAG, "Air Conditioner:");
    if (this->fuzzy_mode_select_ != nullptr)
      LOG_SELECT("  ", "Fuzzy Mode", this->fuzzy_mode_select_);
    if (this->display_mode_select_ != nullptr)
      LOG_SELECT("  ", "Display Mode", this->display_mode_select_);
    if (this->motion_detect_select_ != nullptr)
      LOG_SELECT("  ", "Motion Detect", this->motion_detect_select_);
    if (this->swing_vertical_level_select_ != nullptr)
      LOG_SELECT("  ", "Swing vertical level", this->swing_vertical_level_select_);
    if (this->swing_horizontal_level_select_ != nullptr)
      LOG_SELECT("  ", "Swing horizontal level", this->swing_horizontal_level_select_);
    if (this->quick_mode_select_ != nullptr)
      LOG_SELECT("  ", "Quick Mode", this->quick_mode_select_);
  }

  void AirConditionerSelect::handle_response(std::vector<uint8_t> &response) {
    uint8_t i;
    size_t mapping_idx = -1;

    ESP_LOGV(TAG, " handle_response %x %x %x %x %x %x %x %x %x", \
        response[0], response[1], response[2], response[3], \
        response[4], response[5], response[6], response[7], response[8]);

    // Every AirConditionerSelect instance tracks H'0F / H'11 / H'19 so that:
    //   - motion_detect's control() can save/restore swing across transitions
    //   - swing_*_level's control() can refuse writes while motion is active
    for (uint8_t k = 9; k < response[0] - 3; k += 3) {
      if ((response[k + 1] == 0xFF) && (response[k + 2] == 0xFF))
        continue;
      if (response[k] == SERVICE_ID_CLIMATE_SWING_VERTICAL_LEVEL)
        this->current_swing_vert_ = response[k + 2];
      else if (response[k] == SERVICE_ID_CLIMATE_SWING_HORIZONTAL_LEVEL)
        this->current_swing_horiz_ = response[k + 2];
      else if (response[k] == SERVICE_ID_CLIMATE_ACTIVITY)
        this->current_motion_ = response[k + 2];
    }

    for (i = 9; i < response[0] - 3; i+=3) {
      // Each select instance only cares about its own service_id. Skip others
      // up-front so we don't log "Invalid value N" warnings when another
      // select's value passes through.
      if (this->service_id_ != response[i])
        continue;

      switch (response[i]) {
        case SERVICE_ID_CLIMATE_FUZZY_MODE:
        case SERVICE_ID_CLIMATE_DISPLAY_MODE:
        case SERVICE_ID_CLIMATE_ACTIVITY:
        case SERVICE_ID_CLIMATE_SWING_VERTICAL_LEVEL:
        case SERVICE_ID_CLIMATE_SWING_HORIZONTAL_LEVEL:
        case SERVICE_ID_CLIMATE_BOOST:  // H'1A - Panasonic 3-state quick_mode
          mapping_idx = get_mapping_idx(response, i, this->mappings_);
          break;
        default:
          continue;
      }

      if (mapping_idx != -1) {
        // Skip readback publish during the command-lock window so an old
        // device state doesn't flash the UI back after a fresh write.
        if (this->command_active_)
          return;
        auto value = this->at(mapping_idx);
        this->publish_state(value.value());
        return;
      }
    }
  }

  void AirConditionerSelect::control(const std::string &value) {
    uint8_t command[6] = {0x06, SA_ID_CLIMATE, 0x00, 0x00, 0x00, 0x00};
    uint8_t buffer[6];
    auto idx = this->index_of(value);

    if (!idx.has_value()) {
      ESP_LOGW(TAG, "Invalid value %s", value.c_str());
      return;
    }

    uint8_t mapping = this->mappings_.at(idx.value());
    ESP_LOGV(TAG, "Setting value to %u:%s", mapping, value.c_str());

    // Lock swing selects while motion_detect is active (H'19 != 0). The AC's
    // motion-detect mode controls the louvers; manual changes are a no-op or
    // can confuse the AC. Refuse the write and snap the UI back to the
    // current device value.
    if ((this->service_id_ == SERVICE_ID_CLIMATE_SWING_VERTICAL_LEVEL ||
         this->service_id_ == SERVICE_ID_CLIMATE_SWING_HORIZONTAL_LEVEL) &&
        this->current_motion_ != 0xFF && this->current_motion_ != 0) {
      ESP_LOGW(TAG, "Swing change refused: motion_detect is active (H'19=%u). "
                    "Disable motion_detect first.", (unsigned)this->current_motion_);

      uint8_t cur = (this->service_id_ == SERVICE_ID_CLIMATE_SWING_VERTICAL_LEVEL)
                    ? this->current_swing_vert_ : this->current_swing_horiz_;
      // Snap UI back to current device value (find its label).
      auto it = std::find(this->mappings_.cbegin(), this->mappings_.cend(), cur);
      if (it != this->mappings_.cend()) {
        size_t snap_idx = std::distance(this->mappings_.cbegin(), it);
        auto label = this->at(snap_idx);
        if (label.has_value())
          this->publish_state(label.value());
      }
      return;
    }

    // motion_detect (H'19) special path: when transitioning 0 → non-0, save
    // current swing positions and force H'0F/H'11 to 0 so the AC actually
    // enters motion-detect mode (the remote button does this automatically;
    // a bare TaiSEIA H'19 write does not). When non-0 → 0, restore them.
    if (this->service_id_ == SERVICE_ID_CLIMATE_ACTIVITY) {
      uint8_t old_mapping = 0;
      auto old_idx = this->index_of(this->state);
      if (old_idx.has_value())
        old_mapping = this->mappings_.at(old_idx.value());

      bool entering = (mapping > 0) && (old_mapping == 0);
      bool leaving  = (mapping == 0) && (old_mapping > 0);

      if (entering) {
        if (this->current_swing_vert_ != 0xFF)
          this->saved_swing_vert_ = this->current_swing_vert_;
        if (this->current_swing_horiz_ != 0xFF)
          this->saved_swing_horiz_ = this->current_swing_horiz_;
        ESP_LOGI(TAG, "motion_detect ON: save swing V=%u H=%u, force both to 0",
                 (unsigned)this->saved_swing_vert_,
                 (unsigned)this->saved_swing_horiz_);

        command[2] = WRITE | SERVICE_ID_CLIMATE_SWING_VERTICAL_LEVEL;
        command[4] = 0;
        command[5] = this->parent_->checksum(command, 5);
        this->parent_->send_cmd(command, buffer, 6);

        command[2] = WRITE | SERVICE_ID_CLIMATE_SWING_HORIZONTAL_LEVEL;
        command[4] = 0;
        command[5] = this->parent_->checksum(command, 5);
        this->parent_->send_cmd(command, buffer, 6);

        // Sync sibling swing selects' UI immediately to 0 so user sees the
        // linkage without waiting for the next 10 s poll.
        if (this->swing_vertical_level_select_) {
          static_cast<AirConditionerSelect *>(this->swing_vertical_level_select_)
              ->publish_value(0);
        }
        if (this->swing_horizontal_level_select_) {
          static_cast<AirConditionerSelect *>(this->swing_horizontal_level_select_)
              ->publish_value(0);
        }
      }

      // Write H'19 itself
      command[2] = WRITE | this->service_id_;
      command[4] = mapping;
      command[5] = this->parent_->checksum(command, 5);
      this->parent_->send_cmd(command, buffer, 6);

      if (leaving && this->saved_swing_vert_ != 0xFF) {
        ESP_LOGI(TAG, "motion_detect OFF: restore swing V=%u H=%u",
                 (unsigned)this->saved_swing_vert_,
                 (unsigned)this->saved_swing_horiz_);

        command[2] = WRITE | SERVICE_ID_CLIMATE_SWING_VERTICAL_LEVEL;
        command[4] = this->saved_swing_vert_;
        command[5] = this->parent_->checksum(command, 5);
        this->parent_->send_cmd(command, buffer, 6);

        command[2] = WRITE | SERVICE_ID_CLIMATE_SWING_HORIZONTAL_LEVEL;
        command[4] = this->saved_swing_horiz_;
        command[5] = this->parent_->checksum(command, 5);
        this->parent_->send_cmd(command, buffer, 6);

        // Sync sibling swing selects' UI immediately to restored values.
        if (this->swing_vertical_level_select_) {
          static_cast<AirConditionerSelect *>(this->swing_vertical_level_select_)
              ->publish_value(this->saved_swing_vert_);
        }
        if (this->swing_horizontal_level_select_) {
          static_cast<AirConditionerSelect *>(this->swing_horizontal_level_select_)
              ->publish_value(this->saved_swing_horiz_);
        }
      }
    } else {
      // Normal single-write path for all other selects.
      command[2] = WRITE | this->service_id_;
      command[4] = mapping;
      command[5] = this->parent_->checksum(command, 5);
      this->parent_->send_cmd(command, buffer, 6);
    }

    // Optimistic UI feedback: publish chosen value immediately, then lock for
    // ~3 s so an in-flight readback doesn't overwrite it.
    this->publish_state(value);
    this->command_active_ = true;
    this->cancel_timeout(COMMAND_TIMEOUT_NAME);
    this->set_timeout(COMMAND_TIMEOUT_NAME, 3000, [this]() {
      this->command_active_ = false;
    });
  }

  void WashingMachineSelect::dump_config() {
    ESP_LOGCONFIG(TAG, "Washing Machine:");
    if (this->wash_program_select_ != nullptr)
      LOG_SELECT("  ", "Wash Program", this->wash_program_select_);
    if (this->wash_other_function_select_ != nullptr)
      LOG_SELECT("  ", "Wash Other Function", this->wash_other_function_select_);
    if (this->wash_mode_select_ != nullptr)
      LOG_SELECT("  ", "Wash Mode", this->wash_mode_select_);
    if (this->warm_water_program_select_ != nullptr)
      LOG_SELECT("  ", "Warm Water Program", this->warm_water_program_select_);
  }

  void WashingMachineSelect::handle_response(std::vector<uint8_t> &response) {
    uint8_t i;
    size_t mapping_idx = -1;

    ESP_LOGV(TAG, " handle_response %x %x %x %x %x %x %x %x %x", \
        response[0], response[1], response[2], response[3], \
        response[4], response[5], response[6], response[7], response[8]);

    for (i = 9; i < response[0] - 3; i+=3) {
      switch (response[i]) {
        case SERVICE_ID_WASHER_WASH_PROGRAM:
          mapping_idx = get_mapping_idx(response, i, this->mappings_);
        break;
        case SERVICE_ID_WASHER_OTHER_FUNCTION:
          mapping_idx = get_mapping_idx(response, i, this->mappings_);
        break;
        case SERVICE_ID_WASHER_WASH_MODE:
          mapping_idx = get_mapping_idx(response, i, this->mappings_);
        break;
        case SERVICE_ID_WASHER_WARM_WATER_PROGRAM:
            mapping_idx = get_mapping_idx(response, i, this->mappings_);
        break;
      }
      if ((mapping_idx != -1) && (this->service_id_ == response[i])) {
        auto value = this->at(mapping_idx);
        this->publish_state(value.value());
        return;
      }
    }
  }

  void WashingMachineSelect::control(const std::string &value) {
    uint8_t command[6] = {0x06, SA_ID_WASHER, 0x00, 0x00, 0x00, 0x00};
    uint8_t buffer[6];
    auto idx = this->index_of(value);

    if (idx.has_value()) {
      uint8_t mapping = this->mappings_.at(idx.value());
      ESP_LOGV(TAG, "Setting value to %u:%s", mapping, value.c_str());
      command[2] = WRITE | this->service_id_;
      command[4] = mapping;
      command[5] = this->parent_->checksum(command, 5);
      this->parent_->send_cmd(command, buffer, 6);
      return;
    }

    ESP_LOGW(TAG, "Invalid value %s", value.c_str());
  }

  void DehumidifierSelect::dump_config() {
    ESP_LOGCONFIG(TAG, "Dehumidifier:");
    if (this->operating_program_select_ != nullptr)
      LOG_SELECT("  ", "Operating Program", this->operating_program_select_);
    if (this->air_purfifier_select_ != nullptr)
      LOG_SELECT("  ", "Air Purfifier", this->air_purfifier_select_);
  }

  void DehumidifierSelect::handle_response(std::vector<uint8_t> &response) {
    uint8_t i;
    size_t mapping_idx = -1;

    ESP_LOGV(TAG, " handle_response %x %x %x %x %x %x %x %x %x", \
        response[0], response[1], response[2], response[3], \
        response[4], response[5], response[6], response[7], response[8]);

    for (i = 6; i < response[0] - 3; i+=3) {
      switch (response[i]) {
        case SERVICE_ID_DEHUMIDTFIER_MODE:
            mapping_idx = get_mapping_idx(response, i, this->mappings_);
        break;
        case SERVICE_ID_DEHUMIDTFIER_AIR_PURFIFIER:
            mapping_idx = get_mapping_idx(response, i, this->mappings_);
        break;
      }
      if ((mapping_idx != -1) && (this->service_id_ == response[i])) {
        auto value = this->at(mapping_idx);
        this->publish_state(value.value());
        return;
      }
    }
  }

  void DehumidifierSelect::control(const std::string &value) {
    uint8_t command[6] = {0x06, SA_ID_DEHUMIDIFIER, 0x00, 0x00, 0x00, 0x00};
    uint8_t buffer[6];
    auto idx = this->index_of(value);

    if (idx.has_value()) {
      uint8_t mapping = this->mappings_.at(idx.value());
      ESP_LOGV(TAG, "Setting value to %u:%s", mapping, value.c_str());
      command[2] = WRITE | this->service_id_;
      command[4] = mapping;
      command[5] = this->parent_->checksum(command, 5);
      this->parent_->send_cmd(command, buffer, 6);
      return;
    }

    ESP_LOGW(TAG, "Invalid value %s", value.c_str());
  }

  void AirPurifierSelect::dump_config() {
    ESP_LOGCONFIG(TAG, "TaiXIA Air Purifier Select:");
    if (this->operating_program_select_ != nullptr)
      LOG_SELECT("  ", "   Operating Program", this->operating_program_select_);
  }

  void AirPurifierSelect::handle_response(std::vector<uint8_t> &response) {
    uint8_t i;
    size_t mapping_idx = -1;

    ESP_LOGV(TAG, " handle_response %x %x %x %x %x %x %x %x %x", \
        response[0], response[1], response[2], response[3], \
        response[4], response[5], response[6], response[7], response[8]);

    for (i = 6; i < response[0] - 3; i+=3) {
      switch (response[i]) {
        case SERVICE_ID_PURIFIER_MODE:
            mapping_idx = get_mapping_idx(response, i, this->mappings_);
        break;
      }
      if ((mapping_idx != -1) && (this->service_id_ == response[i])) {
        auto value = this->at(mapping_idx);
        this->publish_state(value.value());
        return;
      }
    }
  }

  void AirPurifierSelect::control(const std::string &value) {
    uint8_t command[6] = {0x06, SA_ID_AIR_PURIFIER, 0x00, 0x00, 0x00, 0x00};
    uint8_t buffer[6];
    auto idx = this->index_of(value);

    if (idx.has_value()) {
      uint8_t mapping = this->mappings_.at(idx.value());
      ESP_LOGV(TAG, "Setting value to %u:%s", mapping, value.c_str());
      command[2] = WRITE | this->service_id_;
      command[4] = mapping;
      command[5] = this->parent_->checksum(command, 5);
      this->parent_->send_cmd(command, buffer, 6);
      return;
    }

    ESP_LOGW(TAG, "Invalid value %s", value.c_str());
  }

  void ErvSelect::dump_config() {
    ESP_LOGCONFIG(TAG, "TaiXIA Erv Select:");
    if (this->ventilate_mode_select_ != nullptr)
      LOG_SELECT("  ", "   Ventilate Mode", this->ventilate_mode_select_);
    if (this->pre_heat_cool_select_ != nullptr)
      LOG_SELECT("  ", "   Ventilate Mode", this->pre_heat_cool_select_);
  }

  void ErvSelect::handle_response(std::vector<uint8_t> &response) {
    uint8_t i;
    size_t mapping_idx = -1;

    ESP_LOGV(TAG, " handle_response %x %x %x %x %x %x %x %x %x", \
        response[0], response[1], response[2], response[3], \
        response[4], response[5], response[6], response[7], response[8]);

    for (i = 6; i < response[0] - 3; i+=3) {
      switch (response[i]) {
        case SERVICE_ID_ERV_VENTILATE_MODE:
            mapping_idx = get_mapping_idx(response, i, this->mappings_);
        break;
        case SERVICE_ID_ERV_PRE_HEAT_COOL:
            mapping_idx = get_mapping_idx(response, i, this->mappings_);
        break;
      }
      if ((mapping_idx != -1) && (this->service_id_ == response[i])) {
        auto value = this->at(mapping_idx);
        this->publish_state(value.value());
        return;
      }
    }
  }

  void ErvSelect::control(const std::string &value) {
    uint8_t command[6] = {0x06, SA_ID_ERV, 0x00, 0x00, 0x00, 0x00};
    uint8_t buffer[6];
    auto idx = this->index_of(value);

    if (idx.has_value()) {
      uint8_t mapping = this->mappings_.at(idx.value());
      ESP_LOGV(TAG, "Setting value to %u:%s", mapping, value.c_str());
      command[2] = WRITE | this->service_id_;
      command[4] = mapping;
      command[5] = this->parent_->checksum(command, 5);
      this->parent_->send_cmd(command, buffer, 6);
      return;
    }

    ESP_LOGW(TAG, "Invalid value %s", value.c_str());
  }

  void ElectricFanSelect::dump_config() {
    ESP_LOGCONFIG(TAG, "TaiXIA Fan Select:");
    if (this->operating_program_select_ != nullptr)
      LOG_SELECT("  ", "   Operating Program", this->operating_program_select_);
  }

  void ElectricFanSelect::handle_response(std::vector<uint8_t> &response) {
    uint8_t i;
    size_t mapping_idx = -1;

    ESP_LOGV(TAG, " handle_response %x %x %x %x %x %x %x %x %x", \
        response[0], response[1], response[2], response[3], \
        response[4], response[5], response[6], response[7], response[8]);

    for (i = 6; i < response[0] - 3; i+=3) {
      switch (response[i]) {
        case SERVICE_ID_FAN_MODE:
            mapping_idx = get_mapping_idx(response, i, this->mappings_);
        break;
      }
      if ((mapping_idx != -1) && (this->service_id_ == response[i])) {
        auto value = this->at(mapping_idx);
        this->publish_state(value.value());
        return;
      }
    }
  }

  void ElectricFanSelect::control(const std::string &value) {
    uint8_t command[6] = {0x06, SA_ID_FAN, 0x00, 0x00, 0x00, 0x00};
    uint8_t buffer[6];
    auto idx = this->index_of(value);

    if (idx.has_value()) {
      uint8_t mapping = this->mappings_.at(idx.value());
      ESP_LOGV(TAG, "Setting value to %u:%s", mapping, value.c_str());
      command[2] = WRITE | this->service_id_;
      command[4] = mapping;
      command[5] = this->parent_->checksum(command, 5);
      this->parent_->send_cmd(command, buffer, 6);
      return;
    }

    ESP_LOGW(TAG, "Invalid value %s", value.c_str());
  }

}  // namespace taixia
}  // namespace esphome
