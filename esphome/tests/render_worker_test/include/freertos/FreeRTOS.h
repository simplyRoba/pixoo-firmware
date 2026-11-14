#pragma once
#include <cstdint>
#include <mutex>
using TickType_t = uint32_t;
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(mux) (mux)->lock()
#define portEXIT_CRITICAL(mux) (mux)->unlock()
#define pdMS_TO_TICKS(n) (n)
#define pdPASS 1
#define pdTRUE 1
