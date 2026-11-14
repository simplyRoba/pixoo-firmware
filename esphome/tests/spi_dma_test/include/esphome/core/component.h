#pragma once
#include <cassert>
#include <freertos/task.h>
namespace esphome {
namespace setup_priority { constexpr float HARDWARE=800; }
class Component { bool failed_=false; public: virtual ~Component()=default; virtual void setup() {} virtual void loop() {} virtual void on_shutdown() {} virtual bool teardown() { return true; } virtual void on_powerdown() {} virtual void dump_config() {} virtual float get_setup_priority() const { return 0; } bool is_failed() const { return failed_; } void mark_failed() { assert(xTaskGetCurrentTaskHandle() == reinterpret_cast<void *>(1)); failed_=true; } };
}
