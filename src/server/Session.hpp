#include "boost/asio/io_context.hpp"
#include <boost/asio.hpp>
#include <memory>

class TransmitJob;
class WebSocketController;
class SessionContext;
class Graph;

class Session : public std::enable_shared_from_this<Session> {
public:
  void start();
  void stop();
  void cleanup();

  Session(boost::asio::io_context &io, SessionContext &context);

private:
  boost::asio::io_context &io_;
  std::shared_ptr<TransmitJob> transmittion;
  std::unique_ptr<WebSocketController> WebSocket;
  std::optional<uint64_t> session_id_;
  std::unique_ptr<Graph> graph_;
};