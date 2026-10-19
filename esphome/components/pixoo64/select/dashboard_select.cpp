#include "dashboard_select.h"

#include <cstring>

namespace esphome::pixoo64 {

void DashboardSelect::setup() {
  size_t index = this->index_of(this->initial_option_).value_or(0);
  if (this->restore_value_) {
    this->pref_ = this->make_entity_preference<pixoo::DashboardRestore>(0x44534801);
    pixoo::DashboardRestore stored;
    if (this->pref_.load(&stored)) {
      // A present versioned record is authoritative, even if its ID vanished.
      if (std::memchr(stored.id, '\0', sizeof(stored.id)) != nullptr)
        index = pixoo::RestoreDashboardIndex(stored.id, this->traits.get_options().begin(), this->size(), index);
    } else if (!this->legacy_options_.empty()) {
      auto legacy = this->make_entity_preference<size_t>();
      size_t old_index;
      if (legacy.load(&old_index)) {
        const char *id = old_index < this->legacy_options_.size()
                             ? this->legacy_options_[old_index].c_str() : nullptr;
        index = pixoo::RestoreDashboardIndex(id, this->traits.get_options().begin(), this->size(), index);
      }
    }
  }
  this->control(index);
}

void DashboardSelect::control(size_t index) {
  if (!this->has_index(index)) return;
  this->publish_state(index);
  if (this->restore_value_) {
    const auto stored = pixoo::SaveDashboardId(this->option_at(index));
    this->pref_.save(&stored);
  }
}

}  // namespace esphome::pixoo64
