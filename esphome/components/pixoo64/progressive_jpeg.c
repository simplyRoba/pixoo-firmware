#include "progressive_jpeg.h"

#ifdef USE_PIXOO64_NOW_PLAYING

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <time.h>
#include <jpeglib.h>
#include <jerror.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_timer.h"
#endif

typedef struct decode_state decode_state;
typedef union allocation allocation;
union allocation {
  struct {
    size_t bytes;
    decode_state *owner;
    allocation *next;
    allocation *previous;
  } h;
  long double alignment;
  void *pointer_alignment;
};

struct decode_state {
  struct jpeg_decompress_struct jpeg;
  struct jpeg_error_mgr error;
  jmp_buf jump;
  pixoo_jpeg_options options;
  pixoo_jpeg_statistics statistics;
  pixoo_jpeg_status status;
  size_t budget;
  allocation *allocations;
  uint8_t *row;
  uint64_t started_ms;
};

static void *external_malloc(size_t size) {
#ifdef ESP_PLATFORM
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  return malloc(size);
#endif
}

static void external_free(void *pointer) {
#ifdef ESP_PLATFORM
  heap_caps_free(pointer);
#else
  free(pointer);
#endif
}

static uint64_t monotonic_ms(void) {
#ifdef ESP_PLATFORM
  return (uint64_t)esp_timer_get_time() / 1000;
#else
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
#endif
}

static void fail(j_common_ptr jpeg);

static decode_state *owned_state(j_common_ptr jpeg) {
  /* client_data belongs to the caller. Only our private error handler
   * identifies an object whose client_data holds decode_state. */
  return jpeg->err && jpeg->err->error_exit == fail
             ? (decode_state *)jpeg->client_data : NULL;
}

/* These symbols replace the archive's complete jmemnobs backend. client_data
 * is installed before jpeg_create_decompress, including its first allocation. */
void *jpeg_get_small(j_common_ptr jpeg, size_t bytes) {
  decode_state *state = owned_state(jpeg);
  allocation *block;
  size_t total;
  if (bytes > SIZE_MAX - sizeof(allocation))
    return NULL;
  total = bytes + sizeof(allocation);
  if (state && total > state->budget - state->statistics.live_memory) {
    state->status = PIXOO_JPEG_MEMORY;
    return NULL;
  }
  block = (allocation *)external_malloc(total);
  if (!block) {
    if (state) state->status = PIXOO_JPEG_MEMORY;
    return NULL;
  }
  block->h.bytes = total;
  block->h.owner = state;
  block->h.previous = NULL;
  block->h.next = state ? state->allocations : NULL;
  if (state) {
    if (state->allocations) state->allocations->h.previous = block;
    state->allocations = block;
    state->statistics.live_memory += total;
    if (state->statistics.live_memory > state->statistics.peak_memory)
      state->statistics.peak_memory = state->statistics.live_memory;
  }
  return block + 1;
}

void jpeg_free_small(j_common_ptr jpeg, void *pointer, size_t bytes) {
  allocation *block;
  decode_state *state;
  (void)jpeg;
  (void)bytes;
  if (!pointer) return;
  block = (allocation *)pointer - 1;
  state = block->h.owner;
  if (state) {
    if (block->h.previous) block->h.previous->h.next = block->h.next;
    else state->allocations = block->h.next;
    if (block->h.next) block->h.next->h.previous = block->h.previous;
    state->statistics.live_memory -= block->h.bytes;
  }
  external_free(block);
}

void *jpeg_get_large(j_common_ptr jpeg, size_t bytes) {
  return jpeg_get_small(jpeg, bytes);
}
void jpeg_free_large(j_common_ptr jpeg, void *pointer, size_t bytes) {
  jpeg_free_small(jpeg, pointer, bytes);
}
size_t jpeg_mem_available(j_common_ptr jpeg, size_t minimum,
                          size_t maximum, size_t allocated) {
  decode_state *state = owned_state(jpeg);
  (void)minimum;
  (void)allocated;
  if (!state) return maximum;
  return state->budget - state->statistics.live_memory;
}
struct backing_store_struct;
void jpeg_open_backing_store(j_common_ptr jpeg,
                             struct backing_store_struct *info, long bytes) {
  decode_state *state = owned_state(jpeg);
  (void)info;
  (void)bytes;
  if (state) state->status = PIXOO_JPEG_MEMORY;
  ERREXIT(jpeg, JERR_NO_BACKING_STORE);
}
long jpeg_mem_init(j_common_ptr jpeg) { (void)jpeg; return 0; }
void jpeg_mem_term(j_common_ptr jpeg) { (void)jpeg; }

static void fail(j_common_ptr jpeg) {
  decode_state *state = (decode_state *)jpeg->client_data;
  if (state->status == PIXOO_JPEG_SUCCESS)
    state->status = jpeg->err->msg_code == JERR_OUT_OF_MEMORY
                        ? PIXOO_JPEG_MEMORY : PIXOO_JPEG_MALFORMED;
  longjmp(state->jump, 1);
}
static void message(j_common_ptr jpeg, int level) {
  if (level < 0) fail(jpeg);
}
static void silent(j_common_ptr jpeg) { (void)jpeg; }

static void checkpoint(decode_state *state) {
  if (state->options.cancel && state->options.cancel(state->options.context)) {
    state->status = PIXOO_JPEG_CANCELLED;
    longjmp(state->jump, 1);
  }
  if (monotonic_ms() - state->started_ms >= state->options.time_limit_ms) {
    state->status = PIXOO_JPEG_WORK_LIMIT;
    longjmp(state->jump, 1);
  }
  if (state->options.yield) state->options.yield(state->options.context);
}

/* Mutable decode state lives outside this setjmp frame. No C++ objects or
 * destructors can be crossed by libjpeg's error longjmp. */
static void run_decode(decode_state *state, const uint8_t *encoded,
                       size_t size, uint32_t width, uint32_t height) {
  unsigned divisor = 1;
  unsigned shorter = width < height ? width : height;
  uint64_t estimate = 0;
  int component;
  if (setjmp(state->jump)) return;
  checkpoint(state);
  state->jpeg.err = jpeg_std_error(&state->error);
  state->error.error_exit = fail;
  state->error.emit_message = message;
  state->error.output_message = silent;
  state->jpeg.client_data = state;
  jpeg_create_decompress(&state->jpeg);
  jpeg_mem_src(&state->jpeg, encoded, (unsigned long)size);
  if (jpeg_read_header(&state->jpeg, TRUE) != JPEG_HEADER_OK ||
      !state->jpeg.progressive_mode || state->jpeg.data_precision != 8 ||
      state->jpeg.image_width != width || state->jpeg.image_height != height ||
      (state->jpeg.num_components != 1 && state->jpeg.num_components != 3) ||
      (state->jpeg.jpeg_color_space != JCS_GRAYSCALE &&
       state->jpeg.jpeg_color_space != JCS_YCbCr &&
       state->jpeg.jpeg_color_space != JCS_RGB)) {
    state->status = PIXOO_JPEG_INVALID;
    return;
  }
  /* Full-resolution coefficients are retained even when IDCT is scaled.
   * Round to complete MCU rows/columns, as virtual arrays do. */
  for (component = 0; component < state->jpeg.num_components; ++component) {
    jpeg_component_info *c = &state->jpeg.comp_info[component];
    uint64_t columns = ((uint64_t)width + state->jpeg.max_h_samp_factor * 8 - 1) /
                       (state->jpeg.max_h_samp_factor * 8);
    uint64_t rows = ((uint64_t)height + state->jpeg.max_v_samp_factor * 8 - 1) /
                    (state->jpeg.max_v_samp_factor * 8);
    estimate += columns * rows * c->h_samp_factor * c->v_samp_factor *
                DCTSIZE2 * sizeof(JCOEF);
  }
  if (estimate > state->budget - state->statistics.live_memory) {
    state->status = PIXOO_JPEG_MEMORY;
    return;
  }
  while (divisor < 8 && shorter >= 64 * divisor * 2) divisor *= 2;
  state->jpeg.scale_num = 1;
  state->jpeg.scale_denom = divisor;
  state->jpeg.out_color_space = JCS_RGB;
  state->jpeg.buffered_image = TRUE;
  if (!jpeg_start_decompress(&state->jpeg)) ERREXIT(&state->jpeg, JERR_BAD_STATE);
  while (!jpeg_input_complete(&state->jpeg)) {
    checkpoint(state);
    if (state->jpeg.input_scan_number > (int)state->options.scan_limit) {
      state->status = PIXOO_JPEG_WORK_LIMIT;
      return;
    }
    if (jpeg_consume_input(&state->jpeg) == JPEG_SUSPENDED)
      ERREXIT(&state->jpeg, JERR_INPUT_EOF);
  }
  state->statistics.scans = (unsigned)state->jpeg.input_scan_number;
  if (state->statistics.scans > state->options.scan_limit) {
    state->status = PIXOO_JPEG_WORK_LIMIT;
    return;
  }
  checkpoint(state);
  if (!jpeg_start_output(&state->jpeg, state->jpeg.input_scan_number))
    ERREXIT(&state->jpeg, JERR_BAD_STATE);
  state->statistics.width = state->jpeg.output_width;
  state->statistics.height = state->jpeg.output_height;
  state->row = (uint8_t *)jpeg_get_small((j_common_ptr)&state->jpeg,
                                        state->jpeg.output_width * 3u);
  if (!state->row) ERREXIT1(&state->jpeg, JERR_OUT_OF_MEMORY, 0);
  while (state->jpeg.output_scanline < state->jpeg.output_height) {
    JSAMPROW row = state->row;
    uint32_t y = state->jpeg.output_scanline;
    checkpoint(state);
    if (jpeg_read_scanlines(&state->jpeg, &row, 1) != 1)
      ERREXIT(&state->jpeg, JERR_INPUT_EOF);
    if (!state->options.row(state->options.context, state->jpeg.output_width,
                            state->jpeg.output_height, y, state->row)) {
      state->status = PIXOO_JPEG_OUTPUT_FAILED;
      return;
    }
  }
  if (!jpeg_finish_output(&state->jpeg) || !jpeg_finish_decompress(&state->jpeg))
    ERREXIT(&state->jpeg, JERR_INPUT_EOF);
  if (state->jpeg.src->bytes_in_buffer != 0) {
    state->status = PIXOO_JPEG_MALFORMED;
    return;
  }
  checkpoint(state);
}

pixoo_jpeg_status pixoo_decode_progressive_jpeg(
    const uint8_t *encoded, size_t size, uint32_t width, uint32_t height,
    const pixoo_jpeg_options *options, pixoo_jpeg_statistics *statistics) {
  decode_state *state;
  pixoo_jpeg_status result;
  size_t budget;
  if (statistics) memset(statistics, 0, sizeof(*statistics));
  if (!encoded || !options || !options->row || !size || size > 512u * 1024u ||
      !width || !height || width > 4096 || height > 4096)
    return PIXOO_JPEG_INVALID;
  budget = options->memory_limit ? options->memory_limit : PIXOO_JPEG_MEMORY_LIMIT;
  if (budget > PIXOO_JPEG_MEMORY_LIMIT) budget = PIXOO_JPEG_MEMORY_LIMIT;
  if (budget < sizeof(*state)) return PIXOO_JPEG_MEMORY;
  state = (decode_state *)external_malloc(sizeof(*state));
  if (!state) return PIXOO_JPEG_MEMORY;
  memset(state, 0, sizeof(*state));
  state->options = *options;
  if (!state->options.scan_limit || state->options.scan_limit > PIXOO_JPEG_SCAN_LIMIT)
    state->options.scan_limit = PIXOO_JPEG_SCAN_LIMIT;
  if (!state->options.time_limit_ms || state->options.time_limit_ms > 10000)
    state->options.time_limit_ms = 10000;
  state->budget = budget;
  state->statistics.live_memory = sizeof(*state);
  state->statistics.peak_memory = sizeof(*state);
  state->started_ms = monotonic_ms();
  run_decode(state, encoded, size, width, height);
  result = state->status;
  if (state->jpeg.mem) jpeg_destroy_decompress(&state->jpeg);
  /* Includes the independent row and any orphan from partial creation. */
  while (state->allocations)
    jpeg_free_small((j_common_ptr)&state->jpeg, state->allocations + 1, 0);
  state->statistics.live_memory = 0;
  if (statistics) *statistics = state->statistics;
  external_free(state);
  return result;
}

#endif
