#pragma once
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_FAIL=-1, ESP_ERR_INVALID_ARG=2, ESP_ERR_NO_MEM=3, ESP_ERR_TIMEOUT=4;
enum esp_reset_reason_t { ESP_RST_POWERON, ESP_RST_EXT, ESP_RST_SW, ESP_RST_PANIC, ESP_RST_INT_WDT, ESP_RST_TASK_WDT, ESP_RST_WDT, ESP_RST_DEEPSLEEP, ESP_RST_BROWNOUT, ESP_RST_SDIO };
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_POWERON; }
inline const char *esp_err_to_name(int) { return "fake"; }
void esp_restart();
