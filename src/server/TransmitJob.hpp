#include <memory>
class MediaEngine;
class RTPPacketizer;

class TransmitJob {

public:
  void start();
  void stop();

private:
  std::shared_ptr<MediaEngine> engine_;
  std::shared_ptr<RTPPacketizer> packetizer_;
};