#pragma once

#include <cstdint>
#include <span>
#include <vector>

class PitchShifter {
 public:
  PitchShifter(int sample_rate = 8000, int channels = 1, int window_ms = 50);

  // Processes the audio in-place. Safe for real-time threads.
  void process(std::span<int16_t> buffer, float semitones);
  void reset();

 private:
  int sample_rate_;
  int channels_;
  int max_delay_samples_;
  int buffer_size_;

  std::vector<float> delay_buffer_;
  int write_ptr_;
  float phase_;

  // Helper functions hidden from the outside world
  float read_interpolated(float delay_frames, int channel) const;
  float get_crossfade_weight(float p) const;
};
