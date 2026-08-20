#include "artwork_decoder.h"

#ifdef USE_PIXOO64_NOW_PLAYING

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <new>

#include <JPEGDEC.h>
#include <pngle.h>

#include "now_playing_art.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
std::atomic<TaskHandle_t> png_external_allocation_task{nullptr};

// pngle 1.1.0 has no allocator injection API. The linker wraps calloc(), but
// only calls made by the one task inside this scope are redirected. Every
// other firmware task continues to use the normal allocator. pngle frees these
// allocations through free(), which accepts capability-heap pointers.
class ScopedPngExternalAllocator {
 public:
  ScopedPngExternalAllocator() : task_(xTaskGetCurrentTaskHandle()) {
    if (this->task_ == nullptr)
      return;
    TaskHandle_t expected = nullptr;
    this->active_ = png_external_allocation_task.compare_exchange_strong(
        expected, this->task_, std::memory_order_acq_rel,
        std::memory_order_acquire);
  }

  ~ScopedPngExternalAllocator() {
    if (!this->active_)
      return;
    TaskHandle_t expected = this->task_;
    png_external_allocation_task.compare_exchange_strong(
        expected, nullptr, std::memory_order_acq_rel,
        std::memory_order_acquire);
  }

  bool active() const { return this->active_; }

 private:
  TaskHandle_t task_{nullptr};
  bool active_{false};
};
}  // namespace

extern "C" void *__real_calloc(size_t count, size_t size);
extern "C" void *__wrap_calloc(size_t count, size_t size) {
  const TaskHandle_t owner =
      png_external_allocation_task.load(std::memory_order_acquire);
  if (owner != nullptr && owner == xTaskGetCurrentTaskHandle())
    return heap_caps_calloc(count, size,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return __real_calloc(count, size);
}
#endif

namespace esphome::pixoo64::artwork {
namespace {

constexpr uint8_t kPngSignature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a,
                                      0x1a, 0x0a};
constexpr uint32_t kPngIhdr = 0x49484452u;
constexpr uint32_t kPngIdat = 0x49444154u;
constexpr uint32_t kPngIend = 0x49454e44u;
constexpr uint32_t kPngTrns = 0x74524e53u;
constexpr uint32_t kPngActl = 0x6163544cu;
constexpr size_t kPngFeedBytes = 256;
constexpr uint32_t kJpegCancellationIntervalPixels = 64;
constexpr uint32_t kDecoderYieldIntervalPixels = 1024;

void YieldDecoderWorker() {
#ifdef ESP_PLATFORM
  vTaskDelay(pdMS_TO_TICKS(1));
#endif
}

pngle_t *CreatePngDecoder() {
#ifdef ESP_PLATFORM
  // pngle_t contains its 32 KiB inflate dictionary and is decode-only bulk
  // state. Allocate it explicitly in PSRAM rather than relying on the global
  // malloc preference or an internal-RAM fallback.
  void *storage = heap_caps_calloc(
      1, PNGLE_T_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (storage == nullptr)
    return nullptr;
  auto *png = static_cast<pngle_t *>(storage);
  pngle_reset(png);
  return png;
#else
  return pngle_new();
#endif
}

void DestroyPngDecoder(pngle_t *png) {
  if (png == nullptr)
    return;
#ifdef ESP_PLATFORM
  pngle_reset(png);
  heap_caps_free(png);
#else
  pngle_destroy(png);
#endif
}

JPEGDEC *CreateJpegDecoder() {
#ifdef ESP_PLATFORM
  // JPEGDEC owns about 18 KiB of tables and MCU scratch used only by this
  // worker decode. It is not render-hot and must not consume internal RAM.
  void *storage = heap_caps_calloc(
      1, sizeof(JPEGDEC), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (storage == nullptr)
    return nullptr;
  return new (storage) JPEGDEC();
#else
  return new (std::nothrow) JPEGDEC();
#endif
}

void DestroyJpegDecoder(JPEGDEC *jpeg) {
  if (jpeg == nullptr)
    return;
#ifdef ESP_PLATFORM
  jpeg->~JPEGDEC();
  heap_caps_free(jpeg);
#else
  delete jpeg;
#endif
}

uint16_t ReadBigEndian16(const uint8_t *bytes) {
  return static_cast<uint16_t>((static_cast<uint16_t>(bytes[0]) << 8) |
                               bytes[1]);
}

uint32_t ReadBigEndian32(const uint8_t *bytes) {
  return (static_cast<uint32_t>(bytes[0]) << 24) |
         (static_cast<uint32_t>(bytes[1]) << 16) |
         (static_cast<uint32_t>(bytes[2]) << 8) |
         static_cast<uint32_t>(bytes[3]);
}

bool IsPngChunkLetter(uint8_t value) {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

uint32_t PngCrc32(const uint8_t *bytes, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= bytes[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ (0xedb88320u &
                          (0u - static_cast<uint32_t>(crc & 1u)));
  }
  return crc ^ 0xffffffffu;
}

DecodeStatus ValidateDimensions(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0)
    return DecodeStatus::kMalformed;
  if (width > kMaxSourceWidth || height > kMaxSourceHeight)
    return DecodeStatus::kDimensionsTooLarge;
  const uint64_t pixels = static_cast<uint64_t>(width) * height;
  if (pixels > kMaxSourcePixels)
    return DecodeStatus::kDimensionsTooLarge;
  return DecodeStatus::kSuccess;
}

bool IsSupportedPngDepth(uint8_t color_type, uint8_t depth) {
  switch (color_type) {
    case 0:
      return depth == 1 || depth == 2 || depth == 4 || depth == 8 ||
             depth == 16;
    case 2:
    case 4:
    case 6:
      return depth == 8 || depth == 16;
    case 3:
      return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    default:
      return false;
  }
}

bool IsStartOfFrameMarker(uint8_t marker) {
  if (marker < 0xc0 || marker > 0xcf)
    return false;
  return marker != 0xc4 && marker != 0xc8 && marker != 0xcc;
}

struct SampleContext {
  uint16_t *destination{nullptr};
  uint32_t source_width{0};
  uint32_t source_height{0};
  CropRect crop{};
  uint32_t x_map[kArtworkWidth]{};
  uint32_t y_map[kArtworkHeight]{};
  uint8_t written[(kArtworkPixelCount + 7) / 8]{};
  uint32_t written_count{0};
  uint32_t callback_count{0};
  uint32_t callback_pixels{0};
  uint32_t pixels_since_yield{0};
  CancellationCallback cancellation{nullptr};
  void *cancellation_context{nullptr};
  bool composite_alpha{false};
  bool failed{false};
  bool work_limit_exceeded{false};
  bool cancelled{false};
};

void YieldDecoderIfDue(SampleContext *context, uint32_t pixels) {
  context->pixels_since_yield += pixels;
  if (context->pixels_since_yield < kDecoderYieldIntervalPixels)
    return;
  context->pixels_since_yield %= kDecoderYieldIntervalPixels;
  YieldDecoderWorker();
}

bool CancellationRequested(SampleContext *context) {
  if (context == nullptr || context->cancelled)
    return context != nullptr && context->cancelled;
  if (context->cancellation != nullptr &&
      context->cancellation(context->cancellation_context)) {
    context->cancelled = true;
    return true;
  }
  return false;
}

void InitializeSampleContext(SampleContext *context, uint32_t width,
                             uint32_t height, uint16_t *destination,
                             bool composite_alpha) {
  context->destination = destination;
  context->source_width = width;
  context->source_height = height;
  context->crop = CenterCrop(width, height);
  context->composite_alpha = composite_alpha;
  for (uint32_t x = 0; x < kArtworkWidth; ++x)
    context->x_map[x] = MapDestinationCoordinate(
        x, context->crop.x, context->crop.width);
  for (uint32_t y = 0; y < kArtworkHeight; ++y)
    context->y_map[y] = MapDestinationCoordinate(
        y, context->crop.y, context->crop.height);
}

// Finds all destination coordinates selecting one source coordinate. A
// downscale has zero or one result; an upscale may repeat one source sample.
// The mapping array is monotonic, so the small fixed search is bounded by 64.
bool FindDestinationRange(const uint32_t *mapping, uint32_t crop_start,
                          uint32_t crop_extent, uint32_t source,
                          uint32_t *first, uint32_t *last) {
  if (source < crop_start || source >= crop_start + crop_extent)
    return false;

  if (crop_extent >= kArtworkWidth) {
    const uint32_t relative = source - crop_start;
    const uint32_t estimate = static_cast<uint32_t>(
        (static_cast<uint64_t>(relative) * kArtworkWidth) / crop_extent);
    const uint32_t begin = estimate > 1 ? estimate - 2 : 0;
    const uint32_t end = std::min<uint32_t>(kArtworkWidth, estimate + 3);
    for (uint32_t index = begin; index < end; ++index) {
      if (mapping[index] == source) {
        *first = index;
        *last = index + 1;
        return true;
      }
    }
    return false;
  }

  uint32_t begin = 0;
  while (begin < kArtworkWidth && mapping[begin] < source)
    ++begin;
  if (begin == kArtworkWidth || mapping[begin] != source)
    return false;
  uint32_t end = begin + 1;
  while (end < kArtworkWidth && mapping[end] == source)
    ++end;
  *first = begin;
  *last = end;
  return true;
}

void WriteSourceSample(SampleContext *context, uint32_t source_x,
                       uint32_t source_y, uint8_t red, uint8_t green,
                       uint8_t blue, uint8_t alpha) {
  if (context->failed)
    return;
  uint32_t first_x = 0;
  uint32_t last_x = 0;
  if (!FindDestinationRange(context->x_map, context->crop.x,
                            context->crop.width, source_x, &first_x,
                            &last_x))
    return;
  uint32_t first_y = 0;
  uint32_t last_y = 0;
  if (!FindDestinationRange(context->y_map, context->crop.y,
                            context->crop.height, source_y, &first_y,
                            &last_y))
    return;

  for (uint32_t y = first_y; y < last_y; ++y) {
    for (uint32_t x = first_x; x < last_x; ++x) {
      const size_t destination_index = y * kArtworkWidth + x;
      const uint8_t mask = static_cast<uint8_t>(1u << (destination_index & 7u));
      uint8_t &written_byte = context->written[destination_index >> 3u];
      if ((written_byte & mask) != 0) {
        context->failed = true;
        return;
      }
      written_byte |= mask;
      ++context->written_count;
      if (context->composite_alpha) {
        context->destination[destination_index] = CompositeRgbaOverRgb565(
            red, green, blue, alpha, context->destination[destination_index]);
      } else {
        context->destination[destination_index] =
            Rgb888ToRgb565(red, green, blue);
      }
    }
  }
}

struct PngDecodeContext {
  SampleContext samples{};
  uint32_t expected_width{0};
  uint32_t expected_height{0};
  bool initialized{false};
  bool done{false};
};

void PngInitCallback(pngle_t *png, uint32_t width, uint32_t height) {
  auto *context = static_cast<PngDecodeContext *>(pngle_get_user_data(png));
  if (context == nullptr || CancellationRequested(&context->samples))
    return;
  context->initialized = true;
  if (width != context->expected_width || height != context->expected_height)
    context->samples.failed = true;
}

void PngDrawCallback(pngle_t *png, uint32_t x, uint32_t y, uint32_t width,
                     uint32_t height, const uint8_t rgba[4]) {
  auto *context = static_cast<PngDecodeContext *>(pngle_get_user_data(png));
  if (context == nullptr || CancellationRequested(&context->samples) ||
      context->samples.failed)
    return;
  SampleContext &samples = context->samples;
  if (++samples.callback_count > kMaxDecodeCallbacks) {
    samples.failed = true;
    samples.work_limit_exceeded = true;
    return;
  }
  // Interlaced PNG is rejected before pngle runs, so each callback must be one
  // source pixel. Enforcing this keeps callback work tied to source_pixels.
  if (width != 1 || height != 1 || x >= samples.source_width ||
      y >= samples.source_height || rgba == nullptr) {
    samples.failed = true;
    return;
  }
  if (++samples.callback_pixels > kMaxDecodeCallbackPixels) {
    samples.failed = true;
    samples.work_limit_exceeded = true;
    return;
  }
  WriteSourceSample(&samples, x, y, rgba[0], rgba[1], rgba[2], rgba[3]);
  YieldDecoderIfDue(&samples, 1);
}

void PngDoneCallback(pngle_t *png) {
  auto *context = static_cast<PngDecodeContext *>(pngle_get_user_data(png));
  if (context != nullptr && !CancellationRequested(&context->samples))
    context->done = true;
}

int JpegDrawCallback(JPEGDRAW *draw) {
  if (draw == nullptr || draw->pUser == nullptr)
    return 0;
  auto *samples = static_cast<SampleContext *>(draw->pUser);
  if (CancellationRequested(samples) || samples->failed)
    return 0;
  if (++samples->callback_count > kMaxDecodeCallbacks) {
    samples->failed = true;
    samples->work_limit_exceeded = true;
    return 0;
  }
  if (draw->x < 0 || draw->y < 0 || draw->iWidth <= 0 ||
      draw->iWidthUsed <= 0 || draw->iWidthUsed > draw->iWidth ||
      draw->iHeight <= 0 || draw->pPixels == nullptr)
    return samples->failed = true, 0;

  const uint64_t right = static_cast<uint64_t>(draw->x) + draw->iWidthUsed;
  const uint64_t bottom = static_cast<uint64_t>(draw->y) + draw->iHeight;
  const uint64_t pixels = static_cast<uint64_t>(draw->iWidthUsed) *
                          draw->iHeight;
  if (right > samples->source_width || bottom > samples->source_height ||
      pixels > kMaxDecodeCallbackPixels - samples->callback_pixels) {
    samples->failed = true;
    samples->work_limit_exceeded = pixels >
        kMaxDecodeCallbackPixels - samples->callback_pixels;
    return 0;
  }
  samples->callback_pixels += static_cast<uint32_t>(pixels);

  uint32_t pixels_until_cancellation_check =
      kJpegCancellationIntervalPixels;
  for (int y = 0; y < draw->iHeight; ++y) {
    for (int x = 0; x < draw->iWidthUsed; ++x) {
      const uint16_t pixel = draw->pPixels[y * draw->iWidth + x];
      const uint8_t red = static_cast<uint8_t>(((pixel >> 11) & 0x1f) * 255 / 31);
      const uint8_t green = static_cast<uint8_t>(((pixel >> 5) & 0x3f) * 255 / 63);
      const uint8_t blue = static_cast<uint8_t>((pixel & 0x1f) * 255 / 31);
      WriteSourceSample(samples, static_cast<uint32_t>(draw->x + x),
                        static_cast<uint32_t>(draw->y + y), red, green, blue,
                        255);
      if (samples->failed)
        return 0;
      YieldDecoderIfDue(samples, 1);
      if (--pixels_until_cancellation_check == 0) {
        if (CancellationRequested(samples))
          return 0;
        pixels_until_cancellation_check =
            kJpegCancellationIntervalPixels;
      }
    }
  }
  return 1;
}

DecodeStatus DecodePng(const uint8_t *encoded, size_t encoded_size,
                       const ImageInfo &info, uint16_t *destination,
                       CancellationCallback cancellation,
                       void *cancellation_context) {
#ifdef ESP_PLATFORM
  ScopedPngExternalAllocator external_allocations;
  if (!external_allocations.active())
    return DecodeStatus::kOutOfMemory;
#endif

  PngDecodeContext context{};
  context.samples.cancellation = cancellation;
  context.samples.cancellation_context = cancellation_context;
  if (CancellationRequested(&context.samples))
    return DecodeStatus::kCancelled;
  context.expected_width = info.width;
  context.expected_height = info.height;
  InitializeSampleContext(&context.samples, info.width, info.height,
                          destination, true);

  pngle_t *png = CreatePngDecoder();
  if (png == nullptr)
    return DecodeStatus::kOutOfMemory;
  pngle_set_user_data(png, &context);
  pngle_set_init_callback(png, PngInitCallback);
  pngle_set_draw_callback(png, PngDrawCallback);
  pngle_set_done_callback(png, PngDoneCallback);
  pngle_set_display_gamma(png, 0.0);

  size_t total_consumed = 0;
  bool feed_failed = false;
  while (total_consumed < encoded_size) {
    if (CancellationRequested(&context.samples))
      break;
    const size_t offered =
        std::min(kPngFeedBytes, encoded_size - total_consumed);
    const int consumed =
        pngle_feed(png, encoded + total_consumed, offered);
    // Zero consumption is a terminal one-call stall. Every successful feed
    // advances at least one byte, so the loop is bounded by encoded_size.
    if (consumed <= 0 || static_cast<size_t>(consumed) > offered) {
      feed_failed = true;
      break;
    }
    // pngle may leave a partial header or fixed-size chunk value unconsumed.
    // Advancing only by its return value retains those bytes at the front of
    // the next bounded feed while appending bytes from the original body.
    total_consumed += static_cast<size_t>(consumed);
    YieldDecoderWorker();
  }
  CancellationRequested(&context.samples);
  DestroyPngDecoder(png);

  if (context.samples.cancelled)
    return DecodeStatus::kCancelled;
  if (context.samples.work_limit_exceeded)
    return DecodeStatus::kWorkLimitExceeded;
  if (feed_failed || total_consumed != encoded_size || !context.initialized ||
      !context.done || context.samples.failed)
    return DecodeStatus::kDecodeFailed;
  if (context.samples.callback_count !=
          static_cast<uint64_t>(info.width) * info.height ||
      context.samples.written_count != kArtworkPixelCount)
    return DecodeStatus::kIncomplete;
  return DecodeStatus::kSuccess;
}

DecodeStatus DecodeJpeg(uint8_t *encoded, size_t encoded_size,
                        const ImageInfo &info, uint16_t *destination,
                        CancellationCallback cancellation,
                        void *cancellation_context) {
  SampleContext context{};
  context.cancellation = cancellation;
  context.cancellation_context = cancellation_context;
  if (CancellationRequested(&context))
    return DecodeStatus::kCancelled;

  JPEGDEC *jpeg = CreateJpegDecoder();
  if (jpeg == nullptr)
    return DecodeStatus::kOutOfMemory;
  if (!jpeg->openRAM(encoded, static_cast<int>(encoded_size),
                     JpegDrawCallback)) {
    DestroyJpegDecoder(jpeg);
    return DecodeStatus::kDecodeFailed;
  }
  if (jpeg->getJPEGType() != JPEG_MODE_BASELINE ||
      jpeg->getWidth() != static_cast<int>(info.width) ||
      jpeg->getHeight() != static_cast<int>(info.height)) {
    jpeg->close();
    DestroyJpegDecoder(jpeg);
    return DecodeStatus::kDecodeFailed;
  }
  if (CancellationRequested(&context)) {
    jpeg->close();
    DestroyJpegDecoder(jpeg);
    return DecodeStatus::kCancelled;
  }

  uint32_t divisor = 1;
  int scale_option = 0;
  const uint32_t shorter = std::min(info.width, info.height);
  if (shorter >= kArtworkWidth * 8) {
    divisor = 8;
    scale_option = JPEG_SCALE_EIGHTH;
  } else if (shorter >= kArtworkWidth * 4) {
    divisor = 4;
    scale_option = JPEG_SCALE_QUARTER;
  } else if (shorter >= kArtworkWidth * 2) {
    divisor = 2;
    scale_option = JPEG_SCALE_HALF;
  }

  // JPEGDEC's reduced IDCT produces ceil(source/divisor) dimensions. The
  // deterministic nearest-neighbor map center-crops that reduced raster, which
  // preserves the source aspect ratio while avoiding source-sized storage.
  const uint32_t decoded_width = (info.width + divisor - 1) / divisor;
  const uint32_t decoded_height = (info.height + divisor - 1) / divisor;
  InitializeSampleContext(&context, decoded_width, decoded_height, destination,
                          false);
  jpeg->setUserPointer(&context);
  jpeg->setPixelType(RGB565_LITTLE_ENDIAN);
  jpeg->setMaxOutputSize(1);
  const bool decoded = jpeg->decode(0, 0, scale_option) != 0;
  jpeg->close();
  DestroyJpegDecoder(jpeg);

  CancellationRequested(&context);
  if (context.cancelled)
    return DecodeStatus::kCancelled;
  if (context.work_limit_exceeded)
    return DecodeStatus::kWorkLimitExceeded;
  if (!decoded || context.failed)
    return DecodeStatus::kDecodeFailed;
  if (context.written_count != kArtworkPixelCount)
    return DecodeStatus::kIncomplete;
  return DecodeStatus::kSuccess;
}

}  // namespace

ImageMagic ClassifyMagic(const uint8_t *bytes, size_t size) {
  if (bytes == nullptr || size < 2)
    return ImageMagic::kUnknown;
  if (size >= sizeof(kPngSignature) &&
      std::memcmp(bytes, kPngSignature, sizeof(kPngSignature)) == 0)
    return ImageMagic::kPng;
  if (bytes[0] == 0xff && bytes[1] == 0xd8)
    return ImageMagic::kJpeg;
  if (size >= 6 && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F' &&
      bytes[3] == '8' && (bytes[4] == '7' || bytes[4] == '9') &&
      bytes[5] == 'a')
    return ImageMagic::kGif;
  if (size >= 12 && bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' &&
      bytes[3] == 'F' && bytes[8] == 'W' && bytes[9] == 'E' &&
      bytes[10] == 'B' && bytes[11] == 'P')
    return ImageMagic::kWebp;
  return ImageMagic::kUnknown;
}

DecodeStatus InspectPng(const uint8_t *encoded, size_t encoded_size,
                        ImageInfo *info) {
  if (encoded == nullptr || info == nullptr)
    return DecodeStatus::kInvalidArgument;
  *info = {};
  info->format = ImageMagic::kPng;
  if (encoded_size > kMaxEncodedBytes)
    return DecodeStatus::kEncodedTooLarge;
  if (encoded_size < sizeof(kPngSignature) ||
      std::memcmp(encoded, kPngSignature, sizeof(kPngSignature)) != 0)
    return DecodeStatus::kMalformed;

  size_t offset = sizeof(kPngSignature);
  uint32_t chunk_count = 0;
  bool saw_ihdr = false;
  bool saw_idat = false;
  bool idat_ended = false;
  while (offset < encoded_size) {
    if (++chunk_count > kMaxPngChunks)
      return DecodeStatus::kWorkLimitExceeded;
    if (encoded_size - offset < 12)
      return DecodeStatus::kIncomplete;
    const uint32_t length = ReadBigEndian32(encoded + offset);
    const uint8_t *type_bytes = encoded + offset + 4;
    if (!IsPngChunkLetter(type_bytes[0]) ||
        !IsPngChunkLetter(type_bytes[1]) ||
        !IsPngChunkLetter(type_bytes[2]) ||
        !IsPngChunkLetter(type_bytes[3]))
      return DecodeStatus::kMalformed;
    if (length > encoded_size - offset - 12)
      return DecodeStatus::kIncomplete;
    const uint32_t type = ReadBigEndian32(type_bytes);
    const uint8_t *data = encoded + offset + 8;
    const uint32_t expected_crc = ReadBigEndian32(data + length);
    if (PngCrc32(type_bytes, static_cast<size_t>(length) + 4) != expected_crc)
      return DecodeStatus::kMalformed;
    if (!saw_ihdr && type != kPngIhdr)
      return DecodeStatus::kMalformed;
    if (type == kPngActl)
      return DecodeStatus::kAnimatedPng;

    if (type == kPngIhdr) {
      if (saw_ihdr || length != 13)
        return DecodeStatus::kMalformed;
      saw_ihdr = true;
      info->width = ReadBigEndian32(data);
      info->height = ReadBigEndian32(data + 4);
      const DecodeStatus dimensions =
          ValidateDimensions(info->width, info->height);
      if (dimensions != DecodeStatus::kSuccess)
        return dimensions;
      const uint8_t depth = data[8];
      const uint8_t color_type = data[9];
      if (!IsSupportedPngDepth(color_type, depth) || data[10] != 0 ||
          data[11] != 0 || data[12] > 1)
        return DecodeStatus::kMalformed;
      if (data[12] != 0)
        return DecodeStatus::kInterlacedPng;
      info->has_alpha = color_type == 4 || color_type == 6;
    } else if (type == kPngIdat) {
      if (!saw_ihdr || length == 0 || idat_ended)
        return DecodeStatus::kMalformed;
      saw_idat = true;
    } else if (saw_idat && type != kPngIend) {
      idat_ended = true;
    }
    if (type == kPngTrns)
      info->has_alpha = true;
    if (type == kPngIend) {
      if (length != 0 || !saw_idat)
        return DecodeStatus::kMalformed;
      offset += 12;
      return offset == encoded_size ? DecodeStatus::kSuccess
                                    : DecodeStatus::kMalformed;
    }
    offset += static_cast<size_t>(length) + 12;
  }
  return DecodeStatus::kIncomplete;
}

DecodeStatus InspectJpeg(const uint8_t *encoded, size_t encoded_size,
                         ImageInfo *info) {
  if (encoded == nullptr || info == nullptr)
    return DecodeStatus::kInvalidArgument;
  *info = {};
  info->format = ImageMagic::kJpeg;
  if (encoded_size > kMaxEncodedBytes)
    return DecodeStatus::kEncodedTooLarge;
  if (encoded_size < 4 || encoded[0] != 0xff || encoded[1] != 0xd8)
    return DecodeStatus::kMalformed;

  size_t offset = 2;
  uint32_t marker_count = 0;
  bool saw_frame = false;
  uint8_t component_ids[3]{};
  uint8_t frame_components = 0;
  while (offset < encoded_size) {
    if (++marker_count > kMaxJpegMarkers)
      return DecodeStatus::kWorkLimitExceeded;
    if (encoded[offset] != 0xff)
      return DecodeStatus::kMalformed;
    while (offset < encoded_size && encoded[offset] == 0xff)
      ++offset;
    if (offset == encoded_size)
      return DecodeStatus::kIncomplete;
    const uint8_t marker = encoded[offset++];
    if (marker == 0x00 || marker == 0xd8 ||
        (marker >= 0xd0 && marker <= 0xd7))
      return DecodeStatus::kMalformed;
    if (marker == 0xd9)
      return DecodeStatus::kIncomplete;
    if (marker == 0x01)
      continue;
    if (encoded_size - offset < 2)
      return DecodeStatus::kIncomplete;
    const uint16_t segment_length = ReadBigEndian16(encoded + offset);
    if (segment_length < 2 || segment_length > encoded_size - offset)
      return DecodeStatus::kIncomplete;
    const uint8_t *segment = encoded + offset + 2;
    const size_t payload_size = segment_length - 2;

    if (IsStartOfFrameMarker(marker)) {
      if (marker == 0xc2)
        return DecodeStatus::kProgressiveJpeg;
      if (marker != 0xc0)
        return DecodeStatus::kUnsupportedFormat;
      if (saw_frame || payload_size < 6)
        return DecodeStatus::kMalformed;
      frame_components = segment[5];
      if ((frame_components != 1 && frame_components != 3) ||
          segment_length != static_cast<uint16_t>(8 + 3 * frame_components) ||
          segment[0] != 8)
        return DecodeStatus::kUnsupportedFormat;
      info->height = ReadBigEndian16(segment + 1);
      info->width = ReadBigEndian16(segment + 3);
      const DecodeStatus dimensions =
          ValidateDimensions(info->width, info->height);
      if (dimensions != DecodeStatus::kSuccess)
        return dimensions;
      uint32_t mcu_blocks = 0;
      for (uint8_t i = 0; i < frame_components; ++i) {
        const uint8_t *component = segment + 6 + 3 * i;
        const uint8_t horizontal = component[1] >> 4;
        const uint8_t vertical = component[1] & 0x0f;
        if (component[0] == 0 || horizontal == 0 || horizontal > 4 ||
            vertical == 0 || vertical > 4 || component[2] > 3)
          return DecodeStatus::kMalformed;
        for (uint8_t prior = 0; prior < i; ++prior)
          if (component_ids[prior] == component[0])
            return DecodeStatus::kMalformed;
        component_ids[i] = component[0];
        mcu_blocks += horizontal * vertical;
      }
      if (mcu_blocks > 6)
        return DecodeStatus::kUnsupportedFormat;
      saw_frame = true;
    }

    offset += segment_length;
    if (marker != 0xda)
      continue;
    if (!saw_frame || payload_size < 4)
      return DecodeStatus::kMalformed;
    const uint8_t scan_components = segment[0];
    if (scan_components != frame_components ||
        segment_length != static_cast<uint16_t>(6 + 2 * scan_components))
      return DecodeStatus::kUnsupportedFormat;
    for (uint8_t i = 0; i < scan_components; ++i) {
      const uint8_t id = segment[1 + 2 * i];
      const uint8_t tables = segment[2 + 2 * i];
      bool matched = false;
      for (uint8_t frame = 0; frame < frame_components; ++frame)
        matched = matched || component_ids[frame] == id;
      if (!matched || (tables >> 4) > 3 || (tables & 0x0f) > 3)
        return DecodeStatus::kMalformed;
    }
    const size_t spectral = 1 + 2 * scan_components;
    if (segment[spectral] != 0 || segment[spectral + 1] != 63 ||
        segment[spectral + 2] != 0)
      return DecodeStatus::kUnsupportedFormat;

    // Only one complete baseline scan is accepted. Entropy bytes are bounded by
    // the encoded cap; byte stuffing and restart markers are validated until an
    // EOI marker that must terminate the body exactly.
    while (offset < encoded_size) {
      if (encoded[offset] != 0xff) {
        ++offset;
        continue;
      }
      size_t marker_offset = offset;
      while (marker_offset < encoded_size && encoded[marker_offset] == 0xff)
        ++marker_offset;
      if (marker_offset == encoded_size)
        return DecodeStatus::kIncomplete;
      const uint8_t entropy_marker = encoded[marker_offset];
      if (entropy_marker == 0x00) {
        offset = marker_offset + 1;
        continue;
      }
      if (++marker_count > kMaxJpegMarkers)
        return DecodeStatus::kWorkLimitExceeded;
      if (entropy_marker >= 0xd0 && entropy_marker <= 0xd7) {
        offset = marker_offset + 1;
        continue;
      }
      if (entropy_marker == 0xd9)
        return marker_offset + 1 == encoded_size ? DecodeStatus::kSuccess
                                                  : DecodeStatus::kMalformed;
      return DecodeStatus::kUnsupportedFormat;
    }
    return DecodeStatus::kIncomplete;
  }
  return DecodeStatus::kIncomplete;
}

DecodeStatus InspectArtwork(const uint8_t *encoded, size_t encoded_size,
                            ImageInfo *info) {
  if (info == nullptr)
    return DecodeStatus::kInvalidArgument;
  const ImageMagic magic = ClassifyMagic(encoded, encoded_size);
  switch (magic) {
    case ImageMagic::kPng:
      return InspectPng(encoded, encoded_size, info);
    case ImageMagic::kJpeg:
      return InspectJpeg(encoded, encoded_size, info);
    case ImageMagic::kGif:
    case ImageMagic::kWebp:
    case ImageMagic::kUnknown:
      *info = {};
      info->format = magic;
      return DecodeStatus::kUnsupportedFormat;
  }
  return DecodeStatus::kUnsupportedFormat;
}

CropRect CenterCrop(uint32_t source_width, uint32_t source_height) {
  const uint32_t extent = std::min(source_width, source_height);
  return {(source_width - extent) / 2, (source_height - extent) / 2,
          extent, extent};
}

uint32_t MapDestinationCoordinate(uint32_t destination_coordinate,
                                  uint32_t crop_start,
                                  uint32_t crop_extent) {
  if (crop_extent == 0)
    return crop_start;
  if (destination_coordinate >= kArtworkWidth)
    destination_coordinate = kArtworkWidth - 1;
  // Pixel centers are mapped with integer arithmetic: destination center
  // (2*d+1)/128 selects floor(center*crop_extent) in the centered source crop.
  // This fills all 64x64 outputs for both upscales and downscales without a
  // source-sized intermediate image.
  const uint64_t numerator =
      (2u * static_cast<uint64_t>(destination_coordinate) + 1u) * crop_extent;
  const uint32_t relative = static_cast<uint32_t>(
      numerator / (2u * static_cast<uint64_t>(kArtworkWidth)));
  return crop_start + std::min(relative, crop_extent - 1);
}

uint16_t Rgb888ToRgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>((static_cast<uint16_t>(red >> 3) << 11) |
                               (static_cast<uint16_t>(green >> 2) << 5) |
                               static_cast<uint16_t>(blue >> 3));
}

uint16_t CompositeRgbaOverRgb565(uint8_t red, uint8_t green, uint8_t blue,
                                 uint8_t alpha, uint16_t background) {
  if (alpha == 255)
    return Rgb888ToRgb565(red, green, blue);
  if (alpha == 0)
    return background;
  const uint8_t background_red = static_cast<uint8_t>(
      (((background >> 11) & 0x1f) * 255 + 15) / 31);
  const uint8_t background_green = static_cast<uint8_t>(
      (((background >> 5) & 0x3f) * 255 + 31) / 63);
  const uint8_t background_blue = static_cast<uint8_t>(
      ((background & 0x1f) * 255 + 15) / 31);
  const uint32_t inverse = 255u - alpha;
  const uint8_t out_red = static_cast<uint8_t>(
      (static_cast<uint32_t>(red) * alpha + background_red * inverse + 127) /
      255);
  const uint8_t out_green = static_cast<uint8_t>(
      (static_cast<uint32_t>(green) * alpha +
       background_green * inverse + 127) /
      255);
  const uint8_t out_blue = static_cast<uint8_t>(
      (static_cast<uint32_t>(blue) * alpha + background_blue * inverse + 127) /
      255);
  return Rgb888ToRgb565(out_red, out_green, out_blue);
}

bool GenerateArtworkPlaceholder(uint64_t identity, uint16_t *destination,
                                size_t destination_count) {
  return pixoo::now_playing::GenerateArtworkPlaceholder(
      identity, destination, destination_count);
}

DecodeStatus DecodeArtwork(
    const uint8_t *encoded, size_t encoded_size,
    uint64_t placeholder_identity, const uint16_t *placeholder,
    uint16_t *destination, size_t destination_count, ImageInfo *decoded_info,
    CancellationCallback cancellation, void *cancellation_context) {
  if (encoded == nullptr || destination == nullptr ||
      destination_count < kArtworkPixelCount || encoded_size == 0)
    return DecodeStatus::kInvalidArgument;
  ImageInfo info{};
  const DecodeStatus inspected = InspectArtwork(encoded, encoded_size, &info);
  if (decoded_info != nullptr)
    *decoded_info = info;
  if (inspected != DecodeStatus::kSuccess)
    return inspected;
  if (cancellation != nullptr && cancellation(cancellation_context))
    return DecodeStatus::kCancelled;

  if (info.format == ImageMagic::kPng) {
    if (placeholder != nullptr)
      std::memmove(destination, placeholder, kArtworkRgb565Bytes);
    else if (!GenerateArtworkPlaceholder(placeholder_identity, destination,
                                         destination_count))
      return DecodeStatus::kInvalidArgument;
    return DecodePng(encoded, encoded_size, info, destination, cancellation,
                     cancellation_context);
  }
  if (info.format == ImageMagic::kJpeg) {
    std::memset(destination, 0, kArtworkRgb565Bytes);
    return DecodeJpeg(const_cast<uint8_t *>(encoded), encoded_size, info,
                      destination, cancellation, cancellation_context);
  }
  return DecodeStatus::kUnsupportedFormat;
}

}  // namespace esphome::pixoo64::artwork

#endif  // USE_PIXOO64_NOW_PLAYING
