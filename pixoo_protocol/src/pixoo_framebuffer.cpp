#include "pixoo_framebuffer.h"

namespace pixoo {

Framebuffer::Framebuffer() = default;

int Framebuffer::PayloadIndex(int x, int y) {
  return (y * kWidth + x) * kChannels;
}

void Framebuffer::SetPixel(int x, int y, Rgb color) {
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return;
  const int i = PayloadIndex(x, y);
  data_[i] = color.r;
  data_[i + 1] = color.g;
  data_[i + 2] = color.b;
}

Rgb Framebuffer::GetPixel(int x, int y) const {
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return Rgb{};
  const int i = PayloadIndex(x, y);
  return Rgb{data_[i], data_[i + 1], data_[i + 2]};
}

uint8_t *Framebuffer::PixelBytes(int x, int y) {
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return nullptr;
  return data_.data() + PayloadIndex(x, y);
}

const uint8_t *Framebuffer::PixelBytes(int x, int y) const {
  if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) return nullptr;
  return data_.data() + PayloadIndex(x, y);
}

void Framebuffer::Fill(Rgb color) {
  for (int i = 0; i < kFramePayloadBytes; i += kChannels) {
    data_[i] = color.r;
    data_[i + 1] = color.g;
    data_[i + 2] = color.b;
  }
}

void Framebuffer::Clear() { Fill(Rgb{}); }

std::vector<uint8_t> Framebuffer::ToPayload() const {
  return std::vector<uint8_t>(this->data_.begin(), this->data_.end());
}

}  // namespace pixoo
