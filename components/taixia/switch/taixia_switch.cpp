#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "taixia_switch.h"

namespace esphome {
namespace taixia {

static const char *const TAG = "taixia.switch";

  void TaiXiaSwitch::dump_config() {
    LOG_SWITCH("", "TaiXIA Switch", this);
    if (!this->parent_->have_sensors())
      this->parent_->send(6, 0, 0, SERVICE_ID_READ_STATUS, 0xffff);
  }

  void TaiXiaSwitch::write_state(bool state) {
    if (this->service_id_ >= 0) {
      bool org_state = state;
      if (((this->sa_id_ == SA_ID_CLIMATE) && (this->service_id_ == SERVICE_ID_CLIMATE_BEEPER)) ||
          ((this->sa_id_ == SA_ID_DEHUMIDIFIER) && (this->service_id_ == SERVICE_ID_DEHUMIDTFIER_BEEPER)))
        state = !state;
      this->parent_->switch_command(this->sa_id_, this->service_id_, state);
      this->publish_state(org_state);
      // Command Lock（大金風格）：發送指令後鎖定，防止設備回讀舊狀態覆蓋 UI
      this->command_active_ = true;
      this->cancel_timeout(COMMAND_TIMEOUT_NAME);
      this->set_timeout(COMMAND_TIMEOUT_NAME, 3000, [this]() {
        this->command_active_ = false;
      });
    }
  }

  void TaiXiaSwitch::handle_response(std::vector<uint8_t> &response) {
    uint8_t i;
    bool new_state = false;

    ESP_LOGV(TAG, " handle_response %x %x %x %x %x %x %x %x %x", \
        response[0], response[1], response[2], response[3], \
        response[4], response[5], response[6], response[7], response[8]);

    for (i = 3; i < response[0] - 3; i+=3) {
      if (this->service_id_ == response[i]) {
        new_state = bool(response[i + 2]);
        if (((this->sa_id_ == SA_ID_CLIMATE) && (this->service_id_ == SERVICE_ID_CLIMATE_BEEPER)) ||
            ((this->sa_id_ == SA_ID_DEHUMIDIFIER) && (this->service_id_ == SERVICE_ID_DEHUMIDTFIER_BEEPER))) {
            new_state = !new_state;
        }
        // BOOST (H'1A) is multi-valued on Panasonic: 0=normal, 1=BOOST,
        // 2=Quiet (set by IR remote). The BOOST switch must ONLY light up
        // for value==1 — Quiet is reported by the boost_mode text_sensor.
        if ((this->sa_id_ == SA_ID_CLIMATE) &&
            (this->service_id_ == SERVICE_ID_CLIMATE_BOOST)) {
          new_state = (response[i + 2] == 1);
        }
        goto done;
      }
    }
    return;
done:
    // Command Lock：鎖定期間不更新 UI，防止設備回讀舊狀態造成閃爍
    if (this->command_active_)
      return;
    if (this->state != new_state)
      this->publish_state(new_state);
  }

}  // namespace taixia
}  // namespace esphome
