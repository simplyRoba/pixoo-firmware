#pragma once
#include <cstdint>
#include "esp_system.h"
using uart_port_t = int;
struct uart_config_t { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; };
#define UART_NUM_2 2
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_APB 0
#define UART_PIN_NO_CHANGE -1
inline int uart_driver_install(int,int,int,int,void*,int) { return ESP_OK; }
inline int uart_driver_delete(int) { return ESP_OK; }
inline int uart_param_config(int,const uart_config_t*) { return ESP_OK; }
inline int uart_set_pin(int,int,int,int,int) { return ESP_OK; }
inline int uart_read_bytes(int,uint8_t*,int,int) { return 0; }
