#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "taixia_number.h"

namespace esphome {
namespace taixia {

static const char *const TAG = "taixia.nubmer";

  static inline uint16_t get_u16(std::vector<uint8_t> &response, int start) {
    return (response[start] << 8) + response[start + 1];
  }

  void TaiXiaNumber::dump_config() {
    LOG_NUMBER(TAG, " TaiXIA Number", this);

    if (!this->parent_->have_sensors())
      this->parent_->send(6, 0, 0, SERVICE_ID_READ_STATUS, 0xffff);
  }

  void TaiXiaNumber::control(float value) {
    if (this->service_id_ > 0) {
      this->parent_->set_number(this->sa_id_, this->service_id_, value);
      this->publish_state(value);
    }
  }

  void TaiXiaNumber::handle_response(std::vector<uint8_t> &response) {
    uint8_t i;

    ESP_LOGV(TAG, " handle_response %x %x %x %x %x %x %x %x %x", \
        response[0], response[1], response[2], response[3], \
        response[4], response[5], response[6], response[7], response[8]);

    for (i = 3; i < response[0] - 3; i+=3) {
      if (this->service_id_ == response[i]) {
        uint16_t raw = get_u16(response, i + 1);
        if (raw == 0xFFFF) {
          // TaiXia 協定中 0xFFFF 表示「無效 / 不適用」（如設備關機時計時器無意義）
          // 保留 HA 顯示的上一個有效值，不覆蓋
          //ESP_LOGV(TAG, "service_id=0x%02x: value is 0xFFFF (invalid), skipping publish", this->service_id_);
          return;
        }
        this->publish_state(raw);
        return;
      }
    }
  }

}  // namespace taixia
}  // namespace esphome
