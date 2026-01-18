#pragma once
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <cstdint>
#include <string_view>

namespace hermes::config {
namespace json = boost::json;
namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace sys = boost::system;
namespace json = boost::json;
namespace websocket = boost::beast::websocket;
namespace beast = boost::beast;
using tcp = boost::asio::ip::tcp;
using udp = boost::asio::ip::udp;
enum class NodeErrorCode : std::uint8_t {
  Success = 0,
  Underrun,
  EndOfStream,
  FileIOError,
  FormatError,
  Critical
};

enum class AppError : std::uint8_t {
  Success = 0,
  ConfigError,
  ParseError,
  NetworkError,
  FileSystemError,
  LogicError,
  Critical
};

struct NodeError {
  NodeErrorCode code;
  std::string message;
  std::string node_id;

  static NodeError From(NodeErrorCode code, std::string msg,
                        std::string node_id) {
    return {code, std::move(msg), std::move(node_id)};
  }
  template <typename... Args>
  static NodeError From(NodeErrorCode code, std::string node_id,
                        std::format_string<Args...> fmt, Args&&... args) {
    return NodeError{code, std::format(fmt, std::forward<Args>(args)...),
                     node_id};
  }
};
struct ErrorInfo {
  AppError code;
  std::string message;

  static ErrorInfo From(AppError code, std::string msg) {
    return {code, std::move(msg)};
  }
};

inline std::string_view to_string(NodeErrorCode err) {
  switch (err) {
    case NodeErrorCode::Success:
      return "Success";
    case NodeErrorCode::Underrun:
      return "Audio Underrun";
    case NodeErrorCode::FileIOError:
      return "File IO Error";
    case NodeErrorCode::EndOfStream:
      return "EndOfStream";
    case NodeErrorCode::FormatError:
      return "Json Format Error";
    case NodeErrorCode::Critical:
      return "Critical Error";
  }
  return "Unknown Error";
}
};  // namespace hermes::config
