#pragma once
#include "esp_system.h"
using gpio_num_t = int;
#define GPIO_MODE_OUTPUT 1
esp_err_t gpio_set_level(gpio_num_t, int);
inline esp_err_t gpio_set_direction(gpio_num_t, int) { return ESP_OK; }
