#include "PitchShifter.h"

#include <algorithm>
#include <cmath>
#include <limits>  // Required for std::numeric_limits

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
PitchShifter::PitchShifter(int sample_rate, int channels, int window_ms)
    : sample_rate_(sample_rate),
      channels_(channels),
      write_ptr_(0),
      phase_(0.0F) {
  constexpr int MS_PER_SEC = 1000;
  max_delay_samples_ = (sample_rate_ * window_ms) / MS_PER_SEC;
  buffer_size_ = max_delay_samples_ * 2;

  // Resolved implicit widening conversion by casting to size_t before
  // multiplication
  delay_buffer_.resize(
      (static_cast<size_t>(buffer_size_) * static_cast<size_t>(channels_)),
      0.0F);
}

void PitchShifter::reset() {
  write_ptr_ = 0;
  phase_ = 0.0F;
  std::ranges::fill(delay_buffer_, 0.0F);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
float PitchShifter::read_interpolated(float delay_frames, int channel) const {
  float read_pos = static_cast<float>(write_ptr_) - delay_frames;

  // Added braces to all control statements
  while (read_pos < 0.0F) {
    read_pos += static_cast<float>(buffer_size_);
  }
  while (read_pos >= static_cast<float>(buffer_size_)) {
    read_pos -= static_cast<float>(buffer_size_);
  }

  int idx1 = static_cast<int>(read_pos);

  if (idx1 >= buffer_size_) {
    idx1 = buffer_size_ - 1;
  }

  int idx2 = (idx1 + 1) % buffer_size_;
  float frac = read_pos - static_cast<float>(idx1);

  float val1 = delay_buffer_[(idx1 * channels_) + channel];
  float val2 = delay_buffer_[(idx2 * channels_) + channel];

  return val1 + (frac * (val2 - val1));
}

float PitchShifter::get_crossfade_weight(float p) {
  constexpr float TRIANGLE_SLOPE = 2.0F;
  // Generates a triangular window: peaks at 1.0 when p=0.5
  return 1.0F - std::abs((TRIANGLE_SLOPE * p) - 1.0F);
}

PitchShifter::CrossfadeParams PitchShifter::calculate_crossfade_parameters()
    const {
  constexpr float HALF_PHASE_OFFSET = 0.5F;

  // Primary read head
  float delay1 = phase_ * static_cast<float>(max_delay_samples_);
  float weight1 = get_crossfade_weight(phase_);

  // Secondary read head (offset by 180 degrees to hide clicks)
  float phase2 = phase_ + HALF_PHASE_OFFSET;
  if (phase2 >= 1.0F) {
    phase2 -= 1.0F;
  }

  float delay2 = phase2 * static_cast<float>(max_delay_samples_);
  float weight2 = get_crossfade_weight(phase2);

  return {delay1, weight1, delay2, weight2};
}

void PitchShifter::process_frame_channels(std::span<int16_t> frame,
                                          const CrossfadeParams& params) {
  for (int c = 0; c < channels_; ++c) {
    // 1. Write the current input sample to the delay line
    delay_buffer_[(write_ptr_ * channels_) + c] = static_cast<float>(frame[c]);

    // 2. Read from the two delayed positions using linear interpolation
    float out1 = read_interpolated(params.delay1, c);
    float out2 = read_interpolated(params.delay2, c);

    // 3. Apply the triangular crossfade to mix them
    float mixed_out = (out1 * params.weight1) + (out2 * params.weight2);

    // 4. Clamp to prevent integer overflow clipping
    frame[c] = static_cast<int16_t>(
        std::clamp(static_cast<int>(mixed_out),
                   static_cast<int>(std::numeric_limits<int16_t>::min()),
                   static_cast<int>(std::numeric_limits<int16_t>::max())));
  }
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void PitchShifter::advance_pointers(float delay_change_rate,
                                    float phase_increment) {
  // Advance circular write pointer
  write_ptr_ = (write_ptr_ + 1) % buffer_size_;

  // Advance and wrap the master phase accumulator
  if (delay_change_rate < 0.0F) {
    phase_ -= phase_increment;
    while (phase_ < 0.0F) {
      phase_ += 1.0F;
    }
  } else {
    phase_ += phase_increment;
    while (phase_ >= 1.0F) {
      phase_ -= 1.0F;
    }
  }
}

void PitchShifter::process(std::span<int16_t> buffer, float semitones) {
  if (semitones == 0.0F) {
    return;
  }

  constexpr float SEMITONES_PER_OCTAVE = 12.0F;
  constexpr float OCTAVE_RATIO = 2.0F;

  // Convert musical semitones to a linear pitch ratio
  float pitch_ratio = std::pow(OCTAVE_RATIO, semitones / SEMITONES_PER_OCTAVE);
  float delay_change_rate = 1.0F - pitch_ratio;
  float phase_increment =
      std::abs(delay_change_rate) / static_cast<float>(max_delay_samples_);

  size_t num_frames = buffer.size() / static_cast<size_t>(channels_);

  for (size_t f = 0; f < num_frames; ++f) {
    auto crossfade_params = calculate_crossfade_parameters();

    // Isolate the exact subspan of memory for the current frame's channels
    size_t frame_offset = f * static_cast<size_t>(channels_);
    auto current_frame =
        buffer.subspan(frame_offset, static_cast<size_t>(channels_));

    process_frame_channels(current_frame, crossfade_params);
    advance_pointers(delay_change_rate, phase_increment);
  }
}
