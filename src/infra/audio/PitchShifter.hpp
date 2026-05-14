#pragma once

#include <cstdint>
#include <span>
#include <vector>

/**
 * @file PitchShifter.hpp
 * @brief Time-domain pitch shifting audio effect.
 */

/**
 * @class PitchShifter
 * @brief A Digital Signal Processing (DSP) class that modifies the pitch of an
 * audio signal in real-time.
 * @details This class implements a time-domain pitch shifting algorithm using a
 * variable delay line. It utilizes dual playheads (read pointers) and
 * triangular crossfading to eliminate audio discontinuities (glitches/clicks)
 * that occur when the delay pointers wrap around. It is designed to be highly
 * efficient and is safe for use in real-time audio threads.
 */
class PitchShifter {
 public:
  /**
   * @brief Constructs a new Pitch Shifter object.
   * @param sample_rate The sample rate of the incoming audio in Hz (e.g., 8000
   * for standard RTP/G.711 streams).
   * @param channels The number of interleaved audio channels (1 for mono, 2 for
   * stereo).
   * @param window_ms The size of the delay window in milliseconds.
   */
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  PitchShifter(int sample_rate = 8000, int channels = 1, int window_ms = 50);

  /**
   * @brief Processes a block of audio data in-place, applying the pitch shift.
   * @param buffer A span representing the interleaved audio buffer to be
   * processed. The data is modified in-place.
   * @param semitones The amount of pitch shift to apply, measured in musical
   * semitones.
   */
  void process(std::span<int16_t> buffer, float semitones);

  /**
   * @brief Resets the internal state of the pitch shifter.
   */
  void reset();

 private:
  int sample_rate_;        ///< The operating sample rate in Hz.
  int channels_;           ///< Number of audio channels.
  int max_delay_samples_;  ///< Maximum delay length in samples (derived from
                           ///< window_ms).
  int buffer_size_;        ///< Actual allocated size of the circular buffer
                           ///< (max_delay_samples_ * 2).

  std::vector<float> delay_buffer_;  ///< The circular delay buffer holding
                                     ///< recent audio history.
  int write_ptr_;  ///< Current write position in the circular buffer.
  float phase_;  ///< Master phase accumulator [0.0 to 1.0] controlling the read
                 ///< pointers.

  /**
   * @brief Reads a fractional delay value using linear interpolation.
   */
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  float read_interpolated(float delay_frames, int channel) const;

  /**
   * @brief Calculates the crossfade amplitude weight for a given phase.
   * @note Made static as it does not access any member variables.
   */

  static float get_crossfade_weight(float p);

  struct CrossfadeParams {
    float delay1;
    float weight1;
    float delay2;
    float weight2;
  };

  /**
   * @brief Calculates the read pointer delays and amplitude weights for the
   * current phase.
   */
  CrossfadeParams calculate_crossfade_parameters() const;

  /**
   * @brief Processes a single multi-channel frame of audio.
   */
  void process_frame_channels(std::span<int16_t> frame,
                              const CrossfadeParams& params);

  /**
   * @brief Advances the write pointer and phase accumulator.
   */

  void advance_pointers(float delay_change_rate, float phase_increment);
};
