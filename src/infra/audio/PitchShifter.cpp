#include "PitchShifter.h"

#include <algorithm>
#include <cmath>

PitchShifter::PitchShifter(int sample_rate, int channels, int window_ms)
    : sample_rate_(sample_rate),
      channels_(channels),
      write_ptr_(0),
      phase_(0.0F) {
  max_delay_samples_ = (sample_rate_ * window_ms) / 1000;
  buffer_size_ = max_delay_samples_ * 2;
  delay_buffer_.resize(buffer_size_ * channels_, 0.0f);
}

void PitchShifter::reset() {
  write_ptr_ = 0;
  phase_ = 0.0F;
  std::ranges::fill(delay_buffer_, 0);
}
float PitchShifter::read_interpolated(float delay_frames, int channel) const {
  float read_pos = static_cast<float>(write_ptr_) - delay_frames;

  // תיקון 1: שימוש בלולאה למקרה ש-delay_frames גדול מאוד,
  // והבטחה שהערך נשאר חיובי.
  while (read_pos < 0.0f) read_pos += static_cast<float>(buffer_size_);
  while (read_pos >= static_cast<float>(buffer_size_))
    read_pos -= static_cast<float>(buffer_size_);

  int idx1 = static_cast<int>(read_pos);

  // תיקון 2: הגנה אולטימטיבית - מבטיחים שאינדקס לעולם לא יחרוג, גם בגלל עיגול
  if (idx1 >= buffer_size_) idx1 = buffer_size_ - 1;

  int idx2 = (idx1 + 1) % buffer_size_;
  float frac = read_pos - static_cast<float>(idx1);

  // גישה בטוחה למערך
  float val1 = delay_buffer_[idx1 * channels_ + channel];
  float val2 = delay_buffer_[idx2 * channels_ + channel];

  return val1 + frac * (val2 - val1);
}

float PitchShifter::get_crossfade_weight(float p) const {
  return 1.0f - std::abs(2.0f * p - 1.0f);
}

void PitchShifter::process(std::span<int16_t> buffer, float semitones) {
  if (semitones == 0.0f) return;

  size_t num_samples = buffer.size();
  size_t num_frames = num_samples / channels_;

  float pitch_ratio = std::pow(2.0f, semitones / 12.0f);
  float delay_change_rate = 1.0f - pitch_ratio;
  float phase_increment =
      std::abs(delay_change_rate) / static_cast<float>(max_delay_samples_);

  for (size_t f = 0; f < num_frames; ++f) {
    for (int c = 0; c < channels_; ++c) {
      size_t sample_idx = f * channels_ + c;

      delay_buffer_[write_ptr_ * channels_ + c] =
          static_cast<float>(buffer[sample_idx]);

      float delay1 = phase_ * static_cast<float>(max_delay_samples_);
      float phase2 = phase_ + 0.5f;
      if (phase2 >= 1.0f) phase2 -= 1.0f;
      float delay2 = phase2 * static_cast<float>(max_delay_samples_);

      float out1 = read_interpolated(delay1, c);
      float out2 = read_interpolated(delay2, c);

      float weight1 = get_crossfade_weight(phase_);
      float weight2 = get_crossfade_weight(phase2);

      float mixed_out = (out1 * weight1) + (out2 * weight2);

      buffer[sample_idx] = static_cast<int16_t>(
          std::clamp(static_cast<int>(mixed_out), -32768, 32767));
    }

    write_ptr_ = (write_ptr_ + 1) % buffer_size_;

    if (delay_change_rate < 0.0f) {
      phase_ -= phase_increment;
      // תיקון 3: שימוש ב-while למקרה ש-increment > 1.0
      while (phase_ < 0.0f) phase_ += 1.0f;
    } else {
      phase_ += phase_increment;
      while (phase_ >= 1.0f) phase_ -= 1.0f;
    }
  }
}
