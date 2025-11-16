#include <chrono>
static const int FRAME_SIZE = 160;
static const int BUFFER_SIZE = 1024 * 512;
// 20ms of 8kHz mono audio
static const size_t AUDIO_BLOCK_SIZE =
    960;  // arbitrary packet size for transmission
static const size_t REFILL_THRESHOLD = 1024 * 512;

using namespace std::chrono_literals;
static const std::chrono::milliseconds TIMER_TICK = 20ms;
