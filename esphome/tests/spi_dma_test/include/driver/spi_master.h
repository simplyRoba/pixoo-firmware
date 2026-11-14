#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_system.h"
struct spi_transaction_t { uint32_t flags{}; size_t length{}; const void *tx_buffer{}; void *user{}; };
using spi_device_handle_t = void *;
struct spi_bus_config_t { int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num, max_transfer_sz; };
struct spi_device_interface_config_t { int clock_speed_hz, mode, spics_io_num, queue_size; void (*post_cb)(spi_transaction_t *); };
#define SPI2_HOST 2
#define SPI_DMA_CH_AUTO 0
#define SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL 1
esp_err_t spi_bus_initialize(int, const spi_bus_config_t *, int);
esp_err_t spi_bus_add_device(int, const spi_device_interface_config_t *, spi_device_handle_t *);
esp_err_t spi_bus_remove_device(spi_device_handle_t);
esp_err_t spi_bus_free(int);
esp_err_t spi_device_queue_trans(spi_device_handle_t, spi_transaction_t *, unsigned);
esp_err_t spi_device_get_trans_result(spi_device_handle_t, spi_transaction_t **, unsigned);
esp_err_t spi_device_transmit(spi_device_handle_t, spi_transaction_t *);
