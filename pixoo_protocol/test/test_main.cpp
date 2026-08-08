// Host tests for pixoo_protocol: frame encoding, framebuffer, and UART parser.
#include <unity.h>

#include <limits>
#include <vector>

#include "pixoo_cmd.h"
#include "pixoo_frame.h"
#include "pixoo_framebuffer.h"
#include "pixoo_uart.h"

using namespace pixoo;

void setUp() {}
void tearDown() {}

static void test_encode_frames_arbitrary_payload() {
  const std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
  const std::vector<uint8_t> f = EncodeFrame(static_cast<Cmd>(0x99), payload);
  const std::vector<uint8_t> want = {0xAA, 0x04, 0x00, 0x99, 0xDE,
                                     0xAD, 0xBE, 0xEF, 0xBB};
  TEST_ASSERT_EQUAL_UINT(want.size(), f.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want.data(), f.data(), want.size());
}

static void test_length_is_le16_and_excludes_framing() {
  std::vector<uint8_t> payload(0x3000, 0x00);
  const std::vector<uint8_t> f = EncodeFrame(static_cast<Cmd>(0x00), payload);
  TEST_ASSERT_EQUAL_UINT(0x3000u + 5u, f.size());
  TEST_ASSERT_EQUAL_UINT8(0xAA, f[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00, f[1]);
  TEST_ASSERT_EQUAL_UINT8(0x30, f[2]);
  TEST_ASSERT_EQUAL_UINT8(0xBB, f.back());
}

static void test_encode_empty_payload() {
  const std::vector<uint8_t> f = EncodeFrame(static_cast<Cmd>(0x21), nullptr, 0);
  const std::vector<uint8_t> want = {0xAA, 0x00, 0x00, 0x21, 0xBB};
  TEST_ASSERT_EQUAL_UINT(want.size(), f.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want.data(), f.data(), want.size());
}

static void test_bounded_frame_encoder_validates_before_writing() {
  const std::vector<uint8_t> payload = {1, 2, 3};
  std::vector<uint8_t> sentinel(8, 0xCC);
  TEST_ASSERT_EQUAL_UINT(0, EncodeFrameTo(Cmd::kInit, payload.data(),
                                           payload.size(), sentinel.data(), 7));
  TEST_ASSERT_EQUAL_UINT(0, EncodeFrameTo(Cmd::kInit, nullptr, 1,
                                           sentinel.data(), sentinel.size()));
  TEST_ASSERT_EQUAL_UINT(0, EncodeFrameTo(Cmd::kInit, payload.data(),
                                           payload.size(), nullptr,
                                           sentinel.size()));
  TEST_ASSERT_EQUAL_UINT(
      0, EncodeFrameTo(Cmd::kInit, payload.data(),
                       std::numeric_limits<size_t>::max(), sentinel.data(),
                       sentinel.size()));
  for (uint8_t byte : sentinel) TEST_ASSERT_EQUAL_UINT8(0xCC, byte);
}

static void test_bounded_frame_encoder_matches_vector_encoding() {
  const std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
  const std::vector<uint8_t> expected =
      EncodeFrame(static_cast<Cmd>(0x99), payload);
  std::vector<uint8_t> output(expected.size());
  const size_t written = EncodeFrameTo(static_cast<Cmd>(0x99), payload.data(),
                                       payload.size(), output.data(),
                                       output.size());
  TEST_ASSERT_EQUAL_UINT(expected.size(), written);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), output.data(), expected.size());
}

static void test_full_frame_rejects_wrong_size_without_writing() {
  std::vector<uint8_t> too_small(10, 0);
  std::vector<uint8_t> output(kFullFrameWireBytes, 0xCC);
  TEST_ASSERT_TRUE(EncodeFullFrame(too_small.data(), too_small.size()).empty());
  TEST_ASSERT_TRUE(EncodeFullFrame(nullptr, 0).empty());
  TEST_ASSERT_EQUAL_UINT(0, EncodeFullFrameTo(too_small.data(), too_small.size(),
                                               output.data(), output.size()));
  TEST_ASSERT_EQUAL_UINT(0, EncodeFullFrameTo(nullptr, kFramePayloadBytes,
                                               output.data(), output.size()));
  TEST_ASSERT_EQUAL_UINT(0, EncodeFullFrameTo(output.data(), kFramePayloadBytes,
                                               output.data(), output.size() - 1));
  for (uint8_t byte : output) TEST_ASSERT_EQUAL_UINT8(0xCC, byte);
}

static void test_full_frame_encoder_supports_exact_payload_overlap() {
  std::vector<uint8_t> output(kFramePayloadBytes + 5);
  for (size_t index = 0; index < static_cast<size_t>(kFramePayloadBytes);
       ++index)
    output[index + 4] = static_cast<uint8_t>(index);
  const std::vector<uint8_t> expected =
      EncodeFullFrame(output.data() + 4, kFramePayloadBytes);

  const size_t written = EncodeFullFrameTo(output.data() + 4, kFramePayloadBytes,
                                           output.data(), output.size());
  TEST_ASSERT_EQUAL_UINT(expected.size(), written);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), output.data(), expected.size());
}

static void test_init_frame_matches_panel_wire() {
  const std::vector<uint8_t> f = EncodeInit();
  const std::vector<uint8_t> want = {0xAA, 0x01, 0x00, 0x10, 0x00, 0xBB};
  TEST_ASSERT_EQUAL_UINT(want.size(), f.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want.data(), f.data(), want.size());
}

static void test_bounded_control_frames_match_known_wire_bytes() {
  uint8_t init_payload[] = {0x00};
  uint8_t init[kControlDmaTarget] = {};
  const size_t init_size = EncodeFrameTo(Cmd::kInit, init_payload,
                                         sizeof(init_payload), init,
                                         sizeof(init));
  const uint8_t want_init[] = {0xAA, 0x01, 0x00, 0x10, 0x00, 0xBB};
  TEST_ASSERT_EQUAL_UINT(sizeof(want_init), init_size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want_init, init, sizeof(want_init));

  const size_t continuation_size = EncodeContinuationTo(
      init_size, false, init + init_size, sizeof(init) - init_size);
  TEST_ASSERT_EQUAL_UINT(234, continuation_size);
  TEST_ASSERT_EQUAL_UINT8(0xAA, init[init_size]);
  TEST_ASSERT_EQUAL_UINT8(0xE5, init[init_size + 1]);
  TEST_ASSERT_EQUAL_UINT8(0x00, init[init_size + 2]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cmd::kContinuation),
                          init[init_size + 3]);
  TEST_ASSERT_EQUAL_UINT8(0xBB, init[init_size + continuation_size - 1]);

  uint8_t white_balance_payload[] = {56, 56, 56};
  uint8_t white_balance[kControlDmaTarget] = {};
  const size_t white_balance_size = EncodeFrameTo(
      Cmd::kWhiteBalance, white_balance_payload,
      sizeof(white_balance_payload), white_balance, sizeof(white_balance));
  const uint8_t want_white_balance[] = {0xAA, 0x03, 0x00, 0x22,
                                        56,   56,   56,   0xBB};
  TEST_ASSERT_EQUAL_UINT(sizeof(want_white_balance), white_balance_size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want_white_balance, white_balance,
                                sizeof(want_white_balance));
}

static void test_white_balance_encoder_preserves_payload() {
  const std::vector<uint8_t> f = EncodeWhiteBalance(10, 20, 30);
  const std::vector<uint8_t> want = {0xAA, 0x03, 0x00, 0x22, 0x0A,
                                     0x14, 0x1E, 0xBB};
  TEST_ASSERT_EQUAL_UINT(want.size(), f.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want.data(), f.data(), want.size());
}

// Continuation pads a transaction up to the stock DMA target. Byte counts
// match the panel protocol in docs/hardware.md §5.
static void test_continuation_pads_full_frame_to_dma_target() {
  // Full RGB frame is 12293 bytes on the wire; stock tops it to 0x30F5=12533.
  const std::vector<uint8_t> c = EncodeContinuation(12293, /*is_full_frame=*/true);
  TEST_ASSERT_EQUAL_UINT(240u, c.size());        // 235 payload + 5 framing
  TEST_ASSERT_EQUAL_UINT8(0x21, c[3]);
  TEST_ASSERT_EQUAL_UINT8(235u & 0xFF, c[1]);
  TEST_ASSERT_EQUAL_UINT8(235u >> 8, c[2]);
  TEST_ASSERT_EQUAL_UINT8(0xBB, c.back());
  TEST_ASSERT_EQUAL_UINT(12533u, 12293u + c.size());
}

static void test_continuation_pads_control_frames_to_240() {
  // Init frame is 6 bytes; white balance is 8. Stock tops both to 240.
  const std::vector<uint8_t> init = EncodeContinuation(6, /*is_full_frame=*/false);
  TEST_ASSERT_EQUAL_UINT(229u, init.size() - 5);  // captured payload len
  TEST_ASSERT_EQUAL_UINT(240u, 6u + init.size());
  const std::vector<uint8_t> wb = EncodeContinuation(8, /*is_full_frame=*/false);
  TEST_ASSERT_EQUAL_UINT(227u, wb.size() - 5);
  TEST_ASSERT_EQUAL_UINT(240u, 8u + wb.size());
}

static void test_continuation_skipped_when_no_room() {
  // At or past the target, or with < kMinFrameBytes leftover, no pad is sent.
  TEST_ASSERT_TRUE(EncodeContinuation(240, /*is_full_frame=*/false).empty());
  TEST_ASSERT_TRUE(EncodeContinuation(236, /*is_full_frame=*/false).empty());
  TEST_ASSERT_TRUE(EncodeContinuation(12533, /*is_full_frame=*/true).empty());
}

static void test_bounded_continuation_matches_vector_encoding() {
  const std::vector<uint8_t> expected = EncodeContinuation(12293, true);
  std::vector<uint8_t> output(expected.size());
  const size_t written =
      EncodeContinuationTo(12293, true, output.data(), output.size());
  TEST_ASSERT_EQUAL_UINT(expected.size(), written);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), output.data(), expected.size());
  TEST_ASSERT_EQUAL_UINT8(0xAA, output[0]);
  TEST_ASSERT_EQUAL_UINT8(0xEB, output[1]);
  TEST_ASSERT_EQUAL_UINT8(0x00, output[2]);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(Cmd::kContinuation), output[3]);
  for (size_t index = 4; index + 1 < output.size(); ++index)
    TEST_ASSERT_EQUAL_UINT8(0, output[index]);
  TEST_ASSERT_EQUAL_UINT8(0xBB, output.back());

  std::vector<uint8_t> too_small(expected.size() - 1, 0xCC);
  TEST_ASSERT_EQUAL_UINT(
      0, EncodeContinuationTo(12293, true, too_small.data(), too_small.size()));
  for (uint8_t byte : too_small) TEST_ASSERT_EQUAL_UINT8(0xCC, byte);
  TEST_ASSERT_EQUAL_UINT(0, EncodeContinuationTo(12533, true, output.data(),
                                                  output.size()));
  TEST_ASSERT_EQUAL_UINT(0, EncodeContinuationTo(
                                std::numeric_limits<size_t>::max(), true,
                                output.data(), output.size()));
}

// Boot frame observed on ribbon pin 2 (docs/hardware.md §6).
static void test_reproduces_captured_boot_frame() {
  const std::vector<uint8_t> payload = {0x42, 0x23, 0x05, 0x06};
  const std::vector<uint8_t> f = EncodeFrame(static_cast<Cmd>(0x10), payload);
  const std::vector<uint8_t> want = {0xAA, 0x04, 0x00, 0x10, 0x42,
                                     0x23, 0x05, 0x06, 0xBB};
  TEST_ASSERT_EQUAL_UINT(want.size(), f.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want.data(), f.data(), want.size());
}

static void test_framebuffer_payload_size() {
  Framebuffer fb;
  TEST_ASSERT_EQUAL_UINT(12288u, fb.ToPayload().size());
}

static void test_framebuffer_starts_black() {
  Framebuffer fb;
  const std::vector<uint8_t> p = fb.ToPayload();
  for (uint8_t b : p) TEST_ASSERT_EQUAL_UINT8(0, b);
}

static void test_framebuffer_set_get_pixel() {
  Framebuffer fb;
  fb.SetPixel(0, 0, Rgb{10, 20, 30});
  fb.SetPixel(63, 63, Rgb{200, 100, 50});
  Rgb a = fb.GetPixel(0, 0);
  Rgb b = fb.GetPixel(63, 63);
  TEST_ASSERT_EQUAL_UINT8(10, a.r);
  TEST_ASSERT_EQUAL_UINT8(20, a.g);
  TEST_ASSERT_EQUAL_UINT8(30, a.b);
  TEST_ASSERT_EQUAL_UINT8(200, b.r);
  TEST_ASSERT_EQUAL_UINT8(100, b.g);
  TEST_ASSERT_EQUAL_UINT8(50, b.b);
}

// Pixel (0,0) = payload bytes 0,1,2 in R,G,B order (matches the panel wire).
static void test_framebuffer_layout_matches_wire_capture() {
  Framebuffer fb;
  fb.SetPixel(0, 0, Rgb{255, 128, 64});
  const std::vector<uint8_t> p = fb.ToPayload();
  TEST_ASSERT_EQUAL_UINT8(255, p[0]);
  TEST_ASSERT_EQUAL_UINT8(128, p[1]);
  TEST_ASSERT_EQUAL_UINT8(64, p[2]);
  // rest of the frame stays black
  TEST_ASSERT_EQUAL_UINT8(0, p[3]);
}

static void test_framebuffer_out_of_bounds_is_safe() {
  Framebuffer fb;
  fb.SetPixel(-1, 0, Rgb{9, 9, 9});
  fb.SetPixel(64, 0, Rgb{9, 9, 9});
  fb.SetPixel(0, 64, Rgb{9, 9, 9});
  Rgb oob = fb.GetPixel(100, 100);
  TEST_ASSERT_EQUAL_UINT8(0, oob.r);
  const std::vector<uint8_t> p = fb.ToPayload();
  for (uint8_t b : p) TEST_ASSERT_EQUAL_UINT8(0, b);
}

// PixelBytes addresses the same three bytes SetPixel and GetPixel use, so a
// composite written through it is indistinguishable from one written through
// them, and rejects the same out-of-range coordinates.
static void test_framebuffer_pixel_bytes_alias_set_get() {
  Framebuffer fb;
  fb.SetPixel(7, 5, Rgb{10, 20, 30});
  uint8_t *pixel = fb.PixelBytes(7, 5);
  TEST_ASSERT_NOT_NULL(pixel);
  TEST_ASSERT_EQUAL_UINT8(10, pixel[0]);
  TEST_ASSERT_EQUAL_UINT8(20, pixel[1]);
  TEST_ASSERT_EQUAL_UINT8(30, pixel[2]);
  pixel[0] = 40;
  pixel[1] = 50;
  pixel[2] = 60;
  const Rgb read_back = fb.GetPixel(7, 5);
  TEST_ASSERT_EQUAL_UINT8(40, read_back.r);
  TEST_ASSERT_EQUAL_UINT8(50, read_back.g);
  TEST_ASSERT_EQUAL_UINT8(60, read_back.b);
  TEST_ASSERT_NULL(fb.PixelBytes(-1, 0));
  TEST_ASSERT_NULL(fb.PixelBytes(64, 0));
  TEST_ASSERT_NULL(fb.PixelBytes(0, -1));
  TEST_ASSERT_NULL(fb.PixelBytes(0, 64));
}

// The panel replies with its ledboard version (cmd 0x10, 4-byte payload) on
// pin 2 after SPI init. See docs/hardware.md §6.
static void test_panel_uart_parser_reads_captured_version_frame() {
  const std::vector<uint8_t> stream = {0xAA, 0x04, 0x00, 0x10, 0x42,
                                       0x23, 0x05, 0x06, 0xBB};
  PanelUartParser parser;
  PanelUartFrame frame;
  bool got = false;
  for (uint8_t b : stream) {
    if (parser.Feed(b, &frame)) got = true;
  }
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL_UINT8(0x10, frame.cmd);
  const std::vector<uint8_t> want = {0x42, 0x23, 0x05, 0x06};
  TEST_ASSERT_EQUAL_UINT(want.size(), frame.payload_size);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want.data(), frame.payload.data(), want.size());
}

static void test_panel_uart_parser_resyncs_after_garbage() {
  // Leading noise then a valid empty-payload frame.
  const std::vector<uint8_t> stream = {0x00, 0xFF, 0xAA, 0x00, 0x00, 0x21, 0xBB};
  PanelUartParser parser;
  PanelUartFrame frame;
  bool got = false;
  for (uint8_t b : stream) {
    if (parser.Feed(b, &frame)) got = true;
  }
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL_UINT8(0x21, frame.cmd);
  TEST_ASSERT_EQUAL_UINT(0u, frame.payload_size);
}

static void test_panel_uart_parser_rejects_bad_tail() {
  const std::vector<uint8_t> stream = {0xAA, 0x01, 0x00, 0x10, 0x00, 0x00};
  PanelUartParser parser;
  PanelUartFrame frame;
  for (uint8_t b : stream)
    TEST_ASSERT_FALSE(parser.Feed(b, &frame));
}

static void test_panel_uart_parser_accepts_fixed_boundary_and_rejects_oversize() {
  std::vector<uint8_t> boundary = {0xAA, 0x40, 0x00, 0x10};
  for (size_t index = 0; index < kPanelUartMaxPayload; ++index)
    boundary.push_back(static_cast<uint8_t>(index));
  boundary.push_back(0xBB);

  PanelUartParser parser;
  PanelUartFrame frame;
  bool got = false;
  for (uint8_t byte : boundary)
    if (parser.Feed(byte, &frame)) got = true;
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL_UINT(kPanelUartMaxPayload, frame.payload_size);
  for (size_t index = 0; index < kPanelUartMaxPayload; ++index)
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(index), frame.payload[index]);

  const std::vector<uint8_t> oversized_then_valid = {
      0xAA, 0x41, 0x00, 0xAA, 0x00, 0x00, 0x21, 0xBB};
  got = false;
  for (uint8_t byte : oversized_then_valid)
    if (parser.Feed(byte, &frame)) got = true;
  TEST_ASSERT_TRUE(got);
  TEST_ASSERT_EQUAL_UINT8(0x21, frame.cmd);
  TEST_ASSERT_EQUAL_UINT(0u, frame.payload_size);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_encode_frames_arbitrary_payload);
  RUN_TEST(test_length_is_le16_and_excludes_framing);
  RUN_TEST(test_encode_empty_payload);
  RUN_TEST(test_bounded_frame_encoder_validates_before_writing);
  RUN_TEST(test_bounded_frame_encoder_matches_vector_encoding);
  RUN_TEST(test_full_frame_rejects_wrong_size_without_writing);
  RUN_TEST(test_full_frame_encoder_supports_exact_payload_overlap);
  RUN_TEST(test_init_frame_matches_panel_wire);
  RUN_TEST(test_bounded_control_frames_match_known_wire_bytes);
  RUN_TEST(test_white_balance_encoder_preserves_payload);
  RUN_TEST(test_continuation_pads_full_frame_to_dma_target);
  RUN_TEST(test_continuation_pads_control_frames_to_240);
  RUN_TEST(test_continuation_skipped_when_no_room);
  RUN_TEST(test_bounded_continuation_matches_vector_encoding);
  RUN_TEST(test_reproduces_captured_boot_frame);
  RUN_TEST(test_framebuffer_payload_size);
  RUN_TEST(test_framebuffer_starts_black);
  RUN_TEST(test_framebuffer_set_get_pixel);
  RUN_TEST(test_framebuffer_layout_matches_wire_capture);
  RUN_TEST(test_framebuffer_out_of_bounds_is_safe);
  RUN_TEST(test_framebuffer_pixel_bytes_alias_set_get);
  RUN_TEST(test_panel_uart_parser_reads_captured_version_frame);
  RUN_TEST(test_panel_uart_parser_resyncs_after_garbage);
  RUN_TEST(test_panel_uart_parser_rejects_bad_tail);
  RUN_TEST(test_panel_uart_parser_accepts_fixed_boundary_and_rejects_oversize);
  return UNITY_END();
}
