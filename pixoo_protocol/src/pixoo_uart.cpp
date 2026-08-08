#include "pixoo_uart.h"

namespace pixoo {

void PanelUartParser::Reset() {
  this->state_ = State::kHeader;
  this->length_ = 0;
  this->payload_read_ = 0;
  this->command_ = 0;
}

bool PanelUartParser::Feed(uint8_t byte, PanelUartFrame *frame) {
  switch (this->state_) {
    case State::kHeader:
      if (byte == kFrameHeader) this->state_ = State::kLengthLow;
      break;
    case State::kLengthLow:
      this->length_ = byte;
      this->state_ = State::kLengthHigh;
      break;
    case State::kLengthHigh:
      this->length_ |= static_cast<uint16_t>(byte) << 8;
      if (this->length_ > kMaxPayload)
        this->Reset();
      else
        this->state_ = State::kCommand;
      break;
    case State::kCommand:
      this->command_ = byte;
      this->payload_read_ = 0;
      this->state_ = this->length_ == 0 ? State::kTail : State::kPayload;
      break;
    case State::kPayload:
      this->payload_[this->payload_read_++] = byte;
      if (this->payload_read_ == this->length_) this->state_ = State::kTail;
      break;
    case State::kTail:
      if (byte == kFrameTail && frame != nullptr) {
        frame->cmd = this->command_;
        for (std::size_t index = 0; index < this->length_; ++index)
          frame->payload[index] = this->payload_[index];
        frame->payload_size = this->length_;
        this->Reset();
        return true;
      }
      this->Reset();
      break;
  }
  return false;
}

}  // namespace pixoo
