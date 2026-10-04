#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIXOO_JPEG_MEMORY_LIMIT (3328u * 1024u)
#define PIXOO_JPEG_SCAN_LIMIT 64u

typedef enum {
  PIXOO_JPEG_SUCCESS,
  PIXOO_JPEG_INVALID,
  PIXOO_JPEG_MALFORMED,
  PIXOO_JPEG_MEMORY,
  PIXOO_JPEG_WORK_LIMIT,
  PIXOO_JPEG_CANCELLED,
  PIXOO_JPEG_OUTPUT_FAILED
} pixoo_jpeg_status;

typedef struct {
  size_t memory_limit;
  unsigned scan_limit;
  uint64_t time_limit_ms;
  int (*cancel)(void *context);
  void (*yield)(void *context);
  int (*row)(void *context, uint32_t width, uint32_t height,
             uint32_t y, const uint8_t *rgb);
  void *context;
} pixoo_jpeg_options;

typedef struct {
  size_t peak_memory;
  size_t live_memory;
  uint64_t elapsed_ms;
  unsigned scans;
  uint32_t width;
  uint32_t height;
} pixoo_jpeg_statistics;

/* Rows are borrowed and emitted only from the final, fully consumed scan.
 * The caller owns transactional publication of these rows. Zero limits select
 * the defaults (3.25 MiB, 64 scans, 10 seconds); larger limits are clamped. */
pixoo_jpeg_status pixoo_decode_progressive_jpeg(
    const uint8_t *encoded, size_t size, uint32_t expected_width,
    uint32_t expected_height, const pixoo_jpeg_options *options,
    pixoo_jpeg_statistics *statistics);

#ifdef __cplusplus
}
#endif
