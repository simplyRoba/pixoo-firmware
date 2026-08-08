// Parser for panel->ESP32 UART frames on ribbon pin 2 (RX only).
// Frames use the same 0xAA <len LE16> <cmd> <payload> 0xBB format as the SPI
// link. The panel sends its ledboard version (cmd 0x10) after SPI init. See
// docs/hardware.md §6.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "pixoo_cmd.h"

namespace pixoo {

constexpr std::size_t kPanelUartMaxPayload = 64;

struct PanelUartFrame {
  uint8_t cmd{0};
  std::array<uint8_t, kPanelUartMaxPayload> payload{};
  std::size_t payload_size{0};
};

class PanelUartParser {
 public:
  // Feeds one received byte. Returns true and fills *frame once a complete,
  // well-framed message ends (valid header, length, and 0xBB tail).
  bool Feed(uint8_t byte, PanelUartFrame *frame);

  // Largest payload accepted; longer length fields resync the parser.
  static constexpr std::size_t kMaxPayload = kPanelUartMaxPayload;

 private:
  enum class State : uint8_t {
    kHeader,
    kLengthLow,
    kLengthHigh,
    kCommand,
    kPayload,
    kTail,
  };

  void Reset();

  State state_{State::kHeader};
  uint16_t length_{0};
  uint16_t payload_read_{0};
  uint8_t command_{0};
  uint8_t payload_[kMaxPayload]{};
};

}  // namespace pixoo
