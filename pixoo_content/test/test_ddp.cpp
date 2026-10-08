#include <unity.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "ddp_frame.h"

using namespace pixoo::ddp;

static std::vector<uint8_t> Packet(uint32_t offset, size_t length,
                                   bool push = false, uint8_t value = 0xA5) {
  std::vector<uint8_t> data(kHeaderBytes + length, value);
  data[0] = push ? 0x41 : 0x40;
  data[1] = 1;
  data[2] = 0x0B;
  data[3] = 1;
  data[4] = offset >> 24;
  data[5] = offset >> 16;
  data[6] = offset >> 8;
  data[7] = offset;
  data[8] = length >> 8;
  data[9] = length;
  return data;
}

static void Apply(FrameBuffer &buffer, const std::vector<uint8_t> &data,
                  DatagramResult expected) {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected),
                       static_cast<int>(buffer.Apply(data.data(), data.size())));
}

static void AssertFrame(const FrameBuffer &buffer,
                        const std::array<uint8_t, kFrameBytes> &expected) {
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), buffer.frame().data(), kFrameBytes);
}

static void test_initial_state_and_reset() {
  FrameBuffer buffer;
  const std::array<uint8_t, kFrameBytes> black{};
  TEST_ASSERT_FALSE(buffer.has_frame());
  AssertFrame(buffer, black);
  Apply(buffer, Packet(0, 3, true), DatagramResult::kPublished);
  Apply(buffer, Packet(10, 1), DatagramResult::kAccepted);
  buffer.Reset();
  TEST_ASSERT_FALSE(buffer.has_frame());
  AssertFrame(buffer, black);
  Apply(buffer, Packet(kFrameBytes, 0, true), DatagramResult::kPublished);
  TEST_ASSERT_TRUE(buffer.has_frame());
  AssertFrame(buffer, black);
  buffer.Reset();
  buffer.Reset();
  TEST_ASSERT_FALSE(buffer.has_frame());
  AssertFrame(buffer, black);
}

static void test_ledfx_nine_chunk_frames_and_sequence_variants() {
  for (uint8_t type : {uint8_t{0x0B}, uint8_t{0x01}}) {
    for (bool per_packet_sequence : {false, true}) {
      FrameBuffer buffer;
      std::array<uint8_t, kFrameBytes> expected{};
      const std::array<uint8_t, kFrameBytes> black{};
      for (size_t i = 0; i < expected.size(); ++i) {
        expected[i] = static_cast<uint8_t>(i * 37 + i / 256);
      }
      size_t chunks = 0;
      for (size_t offset = 0; offset < kFrameBytes; offset += kMaxPayloadBytes) {
        const size_t length = std::min(kMaxPayloadBytes, kFrameBytes - offset);
        const bool push = offset + length == kFrameBytes;
        auto data = Packet(offset, length, push);
        data[1] = per_packet_sequence ? static_cast<uint8_t>(chunks + 1) : 15;
        data[2] = type;
        std::copy_n(expected.data() + offset, length, data.data() + kHeaderBytes);
        Apply(buffer, data, push ? DatagramResult::kPublished : DatagramResult::kAccepted);
        if (!push) {
          TEST_ASSERT_FALSE(buffer.has_frame());
          AssertFrame(buffer, black);
        }
        ++chunks;
      }
      TEST_ASSERT_EQUAL_UINT(9, chunks);
      TEST_ASSERT_TRUE(buffer.has_frame());
      AssertFrame(buffer, expected);
    }
  }
}

static void test_arbitrary_byte_offsets_partial_push_and_latest_wins() {
  FrameBuffer buffer;
  std::array<uint8_t, kFrameBytes> expected{};
  Apply(buffer, Packet(101, 2, false, 7), DatagramResult::kAccepted);
  Apply(buffer, Packet(1, 1, false, 8), DatagramResult::kAccepted);
  Apply(buffer, Packet(0, 1, true, 9), DatagramResult::kPublished);
  expected[101] = expected[102] = 7;
  expected[1] = 8;
  expected[0] = 9;
  AssertFrame(buffer, expected);
  Apply(buffer, Packet(1, 1, false, 10), DatagramResult::kAccepted);
  AssertFrame(buffer, expected);
  Apply(buffer, Packet(102, 1, true, 11), DatagramResult::kPublished);
  expected[1] = 10;
  expected[102] = 11;
  AssertFrame(buffer, expected);
  Apply(buffer, Packet(0, 0, true), DatagramResult::kPublished);
  AssertFrame(buffer, expected);
}

static void test_duplicates_and_ignored_lower_sequence_nibble() {
  FrameBuffer buffer;
  auto data = Packet(4, 2, true, 3);
  for (uint8_t seq = 0; seq < 16; ++seq) {
    data[1] = seq;
    Apply(buffer, data, DatagramResult::kPublished);
    Apply(buffer, data, DatagramResult::kPublished);
  }
  auto pending = Packet(4, 2, false, 6);
  Apply(buffer, pending, DatagramResult::kAccepted);
  Apply(buffer, pending, DatagramResult::kAccepted);
  TEST_ASSERT_EQUAL_UINT8(3, buffer.frame()[4]);
  Apply(buffer, data, DatagramResult::kPublished);
  TEST_ASSERT_EQUAL_UINT8(3, buffer.frame()[4]);
  Apply(buffer, pending, DatagramResult::kAccepted);
  Apply(buffer, Packet(0, 0, true), DatagramResult::kPublished);
  TEST_ASSERT_EQUAL_UINT8(6, buffer.frame()[4]);
}

// A rejected PUSH must neither publish staged bytes nor alter the assembly.
static void AssertRejected(const uint8_t *data, size_t size) {
  FrameBuffer buffer;
  std::array<uint8_t, kFrameBytes> expected{};
  Apply(buffer, Packet(0, 8, true, 0x31), DatagramResult::kPublished);
  std::fill_n(expected.begin(), 8, 0x31);
  Apply(buffer, Packet(1, 2, false, 0x62), DatagramResult::kAccepted);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DatagramResult::kRejected),
                       static_cast<int>(buffer.Apply(data, size)));
  TEST_ASSERT_TRUE(buffer.has_frame());
  AssertFrame(buffer, expected);
  Apply(buffer, Packet(0, 0, true), DatagramResult::kPublished);
  expected[1] = expected[2] = 0x62;
  AssertFrame(buffer, expected);

  FrameBuffer empty;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DatagramResult::kRejected),
                       static_cast<int>(empty.Apply(data, size)));
  TEST_ASSERT_FALSE(empty.has_frame());
  expected.fill(0);
  AssertFrame(empty, expected);
  Apply(empty, Packet(0, 0, true), DatagramResult::kPublished);
  AssertFrame(empty, expected);
}

static void test_rejected_flags_versions_types_destinations_and_sequence() {
  auto data = Packet(0, 8, true, 0xFF);
  for (unsigned flags = 0; flags <= 255; ++flags) {
    if (flags == 0x40 || flags == 0x41) continue;
    data[0] = flags;
    AssertRejected(data.data(), data.size());
  }
  data[0] = 0x41;
  for (unsigned type = 0; type <= 255; ++type) {
    if (type == 0x01 || type == 0x0B) continue;
    data[2] = type;
    AssertRejected(data.data(), data.size());
  }
  data[2] = 0x0B;
  for (unsigned dest = 0; dest <= 255; ++dest) {
    if (dest == 1) continue;
    data[3] = dest;
    AssertRejected(data.data(), data.size());
  }
  data[3] = 1;
  for (unsigned seq = 16; seq <= 255; ++seq) {
    data[1] = seq;
    AssertRejected(data.data(), data.size());
  }
}

static void test_rejected_datagram_sizes_and_declared_lengths() {
  auto data = Packet(0, 8, true, 0xFF);
  AssertRejected(nullptr, 0);
  AssertRejected(nullptr, data.size());
  AssertRejected(nullptr, std::numeric_limits<size_t>::max());
  for (size_t size = 0; size < kHeaderBytes; ++size) {
    AssertRejected(data.data(), size);
  }
  AssertRejected(data.data(), data.size() - 1);
  data.push_back(0);
  AssertRejected(data.data(), data.size());
  data = Packet(0, kMaxPayloadBytes + 1, true);
  AssertRejected(data.data(), data.size());
  AssertRejected(data.data(), kMaxDatagramBytes);
  data = Packet(0, 0, true);
  data[8] = data[9] = 0xFF;
  AssertRejected(data.data(), data.size());
  AssertRejected(data.data(), std::numeric_limits<size_t>::max());
}

static void test_frame_bounds_overflow_and_endpoint() {
  for (uint32_t offset : {uint32_t{kFrameBytes + 1}, uint32_t{0xFFFF},
                          uint32_t{0x10000}, uint32_t{0x1000000},
                          uint32_t{0xFFFFFFFE}, uint32_t{0xFFFFFFFF}}) {
    for (size_t length : {size_t{0}, size_t{8}}) {
      const auto data = Packet(offset, length, true);
      AssertRejected(data.data(), data.size());
    }
  }
  for (const auto &data : {Packet(kFrameBytes, 1, true),
                           Packet(kFrameBytes - 1, 2, true),
                           Packet(kFrameBytes - kMaxPayloadBytes + 1,
                                  kMaxPayloadBytes, true)}) {
    AssertRejected(data.data(), data.size());
  }
  FrameBuffer buffer;
  Apply(buffer, Packet(kFrameBytes, 0), DatagramResult::kAccepted);
  TEST_ASSERT_FALSE(buffer.has_frame());
  Apply(buffer, Packet(kFrameBytes - 1, 1, true, 0x12), DatagramResult::kPublished);
  TEST_ASSERT_EQUAL_UINT8(0x12, buffer.frame().back());
  Apply(buffer, Packet(kFrameBytes - kMaxPayloadBytes, kMaxPayloadBytes, true, 0x34),
        DatagramResult::kPublished);
  TEST_ASSERT_EQUAL_UINT8(0x34, buffer.frame().back());
}

// The oracle consumes generator metadata, never bytes decoded by FrameBuffer.
struct ReferenceDatagram {
  uint32_t offset{0};
  std::vector<uint8_t> payload;
  uint8_t flags{0x40};
  uint8_t sequence{0};
  uint8_t type{0x0B};
  uint8_t destination{1};
  size_t declared_length{0};
  size_t wire_size{10};
  bool intended_valid{true};

  bool Legal() const {
    return (flags == 64 || flags == 65) && sequence < 16 &&
           (type == 1 || type == 11) && destination == 1 &&
           wire_size >= 10 && wire_size <= 1450 &&
           declared_length <= 1440 && wire_size == 10 + declared_length &&
           uint64_t{offset} + declared_length <= 64 * 64 * 3;
  }

  std::vector<uint8_t> Encode() const {
    auto bytes = Packet(offset, payload.size(), flags == 0x41);
    std::copy(payload.begin(), payload.end(), bytes.begin() + kHeaderBytes);
    bytes[0] = flags;
    bytes[1] = sequence;
    bytes[2] = type;
    bytes[3] = destination;
    bytes[8] = declared_length >> 8;
    bytes[9] = declared_length;
    bytes.resize(wire_size, 0xEF);
    return bytes;
  }
};

struct ReferenceAssembly {
  std::array<uint8_t, kFrameBytes> pending{};
  std::array<uint8_t, kFrameBytes> latest{};
  bool has_frame{false};

  DatagramResult Apply(const ReferenceDatagram &packet) {
    if (!packet.Legal()) return DatagramResult::kRejected;
    for (size_t i = 0; i < packet.payload.size(); ++i) {
      pending[packet.offset + i] = packet.payload[i];
    }
    if (packet.flags == 65) {
      latest = pending;
      has_frame = true;
      return DatagramResult::kPublished;
    }
    return DatagramResult::kAccepted;
  }
};

// Fixed-width arithmetic gives the same stream on every native platform.
static uint32_t RandomWord(uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

static void test_seeded_assembly_against_reference() {
  constexpr uint32_t kSeed = 0xDDF06417U;
  uint32_t random = kSeed;
  FrameBuffer buffer;
  ReferenceAssembly reference;
  size_t applications = 0;
  size_t resets = 0;
  auto check_state = [&](const char *context) {
    TEST_ASSERT_EQUAL_MESSAGE(reference.has_frame, buffer.has_frame(), context);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(reference.latest.data(),
                                        buffer.frame().data(), kFrameBytes, context);
  };
  auto check_apply = [&](const ReferenceDatagram &packet, const char *context) {
    TEST_ASSERT_EQUAL_MESSAGE(packet.intended_valid, packet.Legal(), context);
    const auto wire = packet.Encode();
    const auto expected = reference.Apply(packet);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected),
        static_cast<int>(buffer.Apply(wire.data(), wire.size())), context);
    ++applications;
    check_state(context);
  };
  check_state("initial reference state");
  for (size_t batch = 0; batch < 100; ++batch) {
    std::array<size_t, 9> order{{0, 1, 2, 3, 4, 5, 6, 7, 8}};
    for (size_t i = order.size() - 1; i > 0; --i) {
      std::swap(order[i], order[RandomWord(random) % (i + 1)]);
    }
    for (size_t i = 0; i < order.size(); ++i) {
      ReferenceDatagram chunk;
      chunk.offset = order[i] * 1440;
      chunk.payload.resize(std::min(size_t{1440}, kFrameBytes - chunk.offset));
      for (auto &byte : chunk.payload) byte = static_cast<uint8_t>(RandomWord(random));
      chunk.declared_length = chunk.payload.size();
      chunk.wire_size = 10 + chunk.declared_length;
      chunk.sequence = (batch + i) % 16;
      chunk.type = (batch % 2) ? 1 : 11;
      // PUSH follows arrival order, not the chunk's position in the image.
      chunk.flags = (i == 8 || i == batch % 9) ? 0x41 : 0x40;
      char context[100];
      std::snprintf(context, sizeof(context), "seed=0x%08X batch=%zu arrival=%zu chunk=%zu",
                    static_cast<unsigned>(kSeed), batch, i, order[i]);
      check_apply(chunk, context);
      if (i == batch % 9) check_apply(chunk, context);
    }
  }
  for (size_t step = 0; step < 12000; ++step) {
    char context[100];
    std::snprintf(context, sizeof(context), "seed=0x%08X step=%zu kind=%zu",
                  static_cast<unsigned>(kSeed), step, step % 24);
    if (step % 97 == 0) {
      buffer.Reset();
      reference = ReferenceAssembly{};
      ++resets;
      check_state(context);
    }

    ReferenceDatagram packet;
    const size_t lengths[] = {0, 1, 2, 3, 1439, 1440,
                             RandomWord(random) % 1441};
    const size_t length = lengths[RandomWord(random) % 7];
    packet.offset = RandomWord(random) % (kFrameBytes - length + 1);
    if (step % 11 == 0) packet.offset = kFrameBytes - length;
    if (step % 13 == 0) packet.offset = 0;
    packet.payload.resize(length);
    for (auto &byte : packet.payload) byte = static_cast<uint8_t>(RandomWord(random));
    packet.flags = (RandomWord(random) & 1) ? 0x41 : 0x40;
    packet.sequence = static_cast<uint8_t>(RandomWord(random) % 16);
    packet.type = (RandomWord(random) & 1) ? 0x01 : 0x0B;
    packet.declared_length = length;
    packet.wire_size = 10 + length;

    // Preserve the intended write while corrupting one wire-contract property.
    const size_t kind = step % 24;
    packet.intended_valid = kind < 12;
    switch (kind) {
      case 0:  // Explicit zero-length PUSH at the endpoint.
        packet.offset = kFrameBytes;
        packet.payload.clear();
        packet.declared_length = 0;
        packet.wire_size = 10;
        packet.flags = 0x41;
        break;
      case 12: packet.flags ^= uint8_t{1} << (1 + RandomWord(random) % 7); break;
      case 13: packet.type = (RandomWord(random) & 1) ? 0 : 0xFF; break;
      case 14: packet.destination = 2 + RandomWord(random) % 254; break;
      case 15: packet.sequence |= uint8_t{1} << (4 + RandomWord(random) % 4); break;
      case 16: packet.declared_length = length == 0 ? 1 : length - 1; break;
      case 17: packet.declared_length = length + 1; break;
      case 18: packet.wire_size = RandomWord(random) % 10; break;
      case 19: --packet.wire_size; break;
      case 20: ++packet.wire_size; break;
      case 21:
        packet.declared_length = 1441 + RandomWord(random) % 1024;
        packet.wire_size = 10 + packet.declared_length;
        break;
      case 22: {
        const uint32_t offsets[] = {0xFFFF, 0x10000, 0x1000000,
                                   0xFFFFFFFE, 0xFFFFFFFF};
        packet.offset = offsets[RandomWord(random) % 5];
        break;
      }
      case 23: packet.offset = kFrameBytes - length + 1; break;
      default: break;
    }
    check_apply(packet, context);
    if (kind == 1) check_apply(packet, context);  // Exact duplicate, including PUSH.
    if (!packet.intended_valid) {
      // Publish immediately: no subsequent write can hide a rejected mutation.
      ReferenceDatagram push;
      push.offset = kFrameBytes;
      push.flags = 0x41;
      check_apply(push, context);
    }
  }
  ReferenceDatagram final_push;
  final_push.flags = 0x41;
  check_apply(final_push, "final pending assembly");
  TEST_ASSERT_EQUAL_UINT(19501, applications);
  TEST_ASSERT_EQUAL_UINT(124, resets);
}

void RunDdpTests() {
  RUN_TEST(test_initial_state_and_reset);
  RUN_TEST(test_ledfx_nine_chunk_frames_and_sequence_variants);
  RUN_TEST(test_arbitrary_byte_offsets_partial_push_and_latest_wins);
  RUN_TEST(test_duplicates_and_ignored_lower_sequence_nibble);
  RUN_TEST(test_rejected_flags_versions_types_destinations_and_sequence);
  RUN_TEST(test_rejected_datagram_sizes_and_declared_lengths);
  RUN_TEST(test_frame_bounds_overflow_and_endpoint);
  RUN_TEST(test_seeded_assembly_against_reference);
}
