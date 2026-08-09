#include "text_dashboard.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace esphome::pixoo64::dashboard {

void TextDashboard::OnShow(uint32_t now_ms) { this->page_started_ms_ = now_ms; }

void TextDashboard::Tick(uint32_t now_ms) {
  this->current_ms_ = now_ms;
  if (this->font_ == nullptr || this->text_ == nullptr)
    return;
  if (this->CopyText_())
    this->page_started_ms_ = now_ms;
}

void TextDashboard::Render(display::Display &display) const {
  display.fill(Color(0, 0, 0));
  if (this->font_ == nullptr || this->line_count_ == 0)
    return;

  if (this->short_text_) {
    display.printf(32, 32, this->font_, display::TextAlign::CENTER, "%s",
                   this->content_);
    return;
  }

  const uint32_t elapsed_ms = this->current_ms_ - this->page_started_ms_;
  uint64_t page_elapsed_ms =
      elapsed_ms % this->page_cycle_duration_ms_;
  size_t page = 0;
  for (; page + 1 < this->page_count_; ++page) {
    if (page_elapsed_ms < this->page_durations_ms_[page])
      break;
    page_elapsed_ms -= this->page_durations_ms_[page];
  }
  const size_t lines_per_page = this->LinesPerPage_();
  const int line_height = this->LineHeight_();
  const size_t first_line = page * lines_per_page;
  const size_t visible_lines =
      std::min(lines_per_page, this->line_count_ - first_line);
  const int top =
      (kPanelHeight - static_cast<int>(visible_lines) * line_height) / 2;

  for (size_t i = 0; i < visible_lines; ++i) {
    const Line &line = this->lines_[first_line + i];
    this->CopyRange_(line.start, line.length);
    const int width = line.width;
    const int y = top + static_cast<int>(i) * line_height;
    display.start_clipping(kContentLeft, y, kContentRight, y + line_height);
    if (line.scrolls) {
      const int travel = kContentWidth + width;
      const int phase = static_cast<int>(
          (page_elapsed_ms / 1000u * kScrollPixelsPerSecond +
           page_elapsed_ms % 1000u * kScrollPixelsPerSecond / 1000u) %
          static_cast<uint32_t>(travel));
      display.print(kContentRight - phase, y, this->font_,
                    display::TextAlign::TOP_LEFT, this->render_buffer_);
    } else {
      display.print(kContentLeft + (kContentWidth - width) / 2, y, this->font_,
                    display::TextAlign::TOP_LEFT, this->render_buffer_);
    }
    display.end_clipping();
  }
}

bool TextDashboard::CopyText_() {
  const std::string &state = this->text_->state;
  char sanitized[kMaximumTextBytes + 1];
  size_t length = 0;
  for (size_t position = 0;
       position < state.size() && length < kMaximumTextBytes;) {
    const char character = state[position];
    if (character == '\\' && position + 1 < state.size() &&
        state[position + 1] == 'n') {
      sanitized[length++] = '\n';
      position += 2;
      continue;
    }
    if (character == '\r') {
      const bool crlf =
          position + 1 < state.size() && state[position + 1] == '\n';
      sanitized[length++] = '\n';
      position += crlf ? 2 : 1;
      continue;
    }
    if (character == '\0') {
      sanitized[length++] = '?';
      ++position;
      continue;
    }

    const size_t sequence_length =
        Utf8SequenceLength_(state.data() + position, state.size() - position);
    if (sequence_length == 0) {
      sanitized[length++] = '?';
      ++position;
      continue;
    }
    if (length + sequence_length > kMaximumTextBytes)
      break;
    std::memcpy(sanitized + length, state.data() + position, sequence_length);
    length += sequence_length;
    position += sequence_length;
  }
  sanitized[length] = '\0';

  if (length == this->content_length_ &&
      std::memcmp(this->content_, sanitized, length) == 0)
    return false;

  std::memcpy(this->content_, sanitized, length + 1);
  this->content_length_ = length;
  this->LayoutText_();
  return true;
}

void TextDashboard::LayoutText_() {
  this->line_count_ = 0;
  this->page_count_ = 0;
  this->short_text_ = false;
  if (this->content_length_ == 0)
    return;

  if (std::memchr(this->content_, '\n', this->content_length_) == nullptr &&
      this->TextWidth_(0, this->content_length_) <= kContentWidth) {
    this->AddLine_(0, this->content_length_, false);
    this->short_text_ = true;
    this->page_count_ = 1;
    this->BuildPageDurations_();
    return;
  }

  size_t start = 0;
  while (true) {
    size_t end = start;
    while (end < this->content_length_ && this->content_[end] != '\n')
      ++end;
    this->LayoutParagraph_(start, end);
    if (end == this->content_length_)
      break;
    start = end + 1;
  }
  const size_t lines_per_page = this->LinesPerPage_();
  this->page_count_ = (this->line_count_ + lines_per_page - 1) / lines_per_page;
  this->BuildPageDurations_();
}

void TextDashboard::LayoutParagraph_(size_t start, size_t end) {
  while (start < end && IsSpace_(this->content_[start]))
    ++start;
  if (start == end) {
    this->AddLine_(start, 0, false);
    return;
  }

  size_t position = start;
  while (position < end) {
    const size_t word_start = position;
    while (position < end && !IsSpace_(this->content_[position]))
      ++position;
    const size_t word_end = position;
    if (this->TextWidth_(word_start, word_end - word_start) > kContentWidth) {
      this->AddLine_(word_start, word_end - word_start, true);
      while (position < end && IsSpace_(this->content_[position]))
        ++position;
      continue;
    }

    size_t line_end = word_end;
    while (true) {
      size_t next_start = position;
      while (next_start < end && IsSpace_(this->content_[next_start]))
        ++next_start;
      if (next_start == end) {
        this->AddLine_(word_start, line_end - word_start, false);
        return;
      }

      size_t next_end = next_start;
      while (next_end < end && !IsSpace_(this->content_[next_end]))
        ++next_end;
      if (this->TextWidth_(word_start, next_end - word_start) <=
          kContentWidth) {
        line_end = next_end;
        position = next_end;
        continue;
      }

      this->AddLine_(word_start, line_end - word_start, false);
      position = next_start;
      break;
    }
  }
}

void TextDashboard::AddLine_(size_t start, size_t length, bool scrolls) {
  if (this->line_count_ == kMaximumLines)
    return;
  this->lines_[this->line_count_++] =
      Line{static_cast<uint8_t>(start), static_cast<uint8_t>(length),
           this->TextWidth_(start, length), scrolls};
}

void TextDashboard::BuildPageDurations_() {
  this->page_cycle_duration_ms_ = 0;
  const size_t lines_per_page = this->LinesPerPage_();
  for (size_t page = 0; page < this->page_count_; ++page) {
    uint32_t duration_ms = kPageDurationMs;
    const size_t first_line = page * lines_per_page;
    const size_t visible_lines =
        std::min(lines_per_page, this->line_count_ - first_line);
    for (size_t i = 0; i < visible_lines; ++i) {
      const Line &line = this->lines_[first_line + i];
      if (line.scrolls)
        duration_ms = std::max(duration_ms, ScrollDurationMs_(line.width));
    }
    this->page_durations_ms_[page] = duration_ms;
    this->page_cycle_duration_ms_ += duration_ms;
  }
}

uint32_t TextDashboard::ScrollDurationMs_(int width) {
  const uint64_t travel =
      kContentWidth + static_cast<uint64_t>(std::max(0, width));
  const uint64_t duration_ms =
      (travel * 1000u + kScrollPixelsPerSecond - 1) /
      kScrollPixelsPerSecond;
  return static_cast<uint32_t>(
      std::min<uint64_t>(duration_ms, UINT32_MAX));
}

void TextDashboard::CopyRange_(size_t start, size_t length) const {
  std::memcpy(this->render_buffer_, this->content_ + start, length);
  this->render_buffer_[length] = '\0';
}

int TextDashboard::TextWidth_(size_t start, size_t length) const {
  this->CopyRange_(start, length);
  return this->BufferWidth_();
}

int TextDashboard::BufferWidth_() const {
  int width = 0;
  int x_offset = 0;
  int baseline = 0;
  int height = 0;
  this->font_->measure(this->render_buffer_, &width, &x_offset, &baseline,
                       &height);
  return width;
}

int TextDashboard::LineHeight_() const {
  return std::max(1, this->font_->get_height());
}

size_t TextDashboard::LinesPerPage_() const {
  return std::max<size_t>(
      1, std::min(kMaximumLinesPerPage,
                  static_cast<size_t>(kPanelHeight / this->LineHeight_())));
}

bool TextDashboard::IsSpace_(char character) {
  return character == ' ' || character == '\t';
}

size_t TextDashboard::Utf8SequenceLength_(const char *data, size_t available) {
  const uint8_t first = static_cast<uint8_t>(data[0]);
  size_t length = 0;
  if (first < 0x80)
    return 1;
  if (first >= 0xC2 && first <= 0xDF)
    length = 2;
  else if (first >= 0xE0 && first <= 0xEF)
    length = 3;
  else if (first >= 0xF0 && first <= 0xF4)
    length = 4;
  else
    return 0;

  if (available < length)
    return 0;
  const uint8_t second = static_cast<uint8_t>(data[1]);
  if ((second & 0xC0) != 0x80 || (first == 0xE0 && second < 0xA0) ||
      (first == 0xED && second > 0x9F) || (first == 0xF0 && second < 0x90) ||
      (first == 0xF4 && second > 0x8F))
    return 0;
  for (size_t i = 2; i < length; ++i) {
    if ((static_cast<uint8_t>(data[i]) & 0xC0) != 0x80)
      return 0;
  }
  return length;
}

}  // namespace esphome::pixoo64::dashboard
