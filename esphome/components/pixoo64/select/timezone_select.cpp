#include "timezone_select.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "timezone_catalog.h"

namespace esphome::pixoo64 {

static const char *const TAG = "pixoo64.timezone";

void TimezoneSelect::setup() {
  // The catalog owns the option labels; publish them to the entity's traits at
  // startup rather than duplicating them in YAML.
  const size_t count = pixoo::TimezoneCount();
  FixedVector<const char *> options;
  options.init(count);
  for (size_t i = 0; i < count; i++) {
    options.push_back(pixoo::TimezoneLabel(i));
  }
  this->traits.set_options(options);

  size_t index = pixoo::DefaultTimezoneIndex();
  if (this->restore_value_) {
    this->pref_ = this->make_entity_preference<size_t>();
    size_t stored = index;
    if (this->pref_.load(&stored) && stored < count) {
      index = stored;
    }
  }
  this->apply_index_(index);
  this->publish_state(index);
}

void TimezoneSelect::control(size_t index) {
  this->apply_index_(index);
  this->publish_state(index);
  if (this->restore_value_) {
    this->pref_.save(&index);
  }
}

void TimezoneSelect::apply_index_(size_t index) {
  const char *posix = pixoo::TimezonePosix(index);
  if (posix == nullptr) {
    return;
  }
  if (this->rtc_ != nullptr) {
    this->rtc_->set_timezone(posix);
  }
}

void TimezoneSelect::dump_config() {
  LOG_SELECT("", "Pixoo64 Timezone", this);
  ESP_LOGCONFIG(TAG, "  Restore Value: %s", YESNO(this->restore_value_));
}

}  // namespace esphome::pixoo64
