#pragma once
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <string_view>

namespace json = boost::json;
namespace asio = boost::asio;
namespace http = boost::beast::http;
namespace sys = boost::system;
namespace json = boost::json;
namespace websocket = boost::beast::websocket;
namespace beast = boost::beast;
using tcp = boost::asio::ip::tcp;

enum class NodeError { Success = 0, Underrun, EndOfStream, FileIOError, FormatError, Critical };

inline std::string_view to_string(NodeError err) {
    switch (err) {
        case NodeError::Success:
            return "Success";
        case NodeError::Underrun:
            return "Audio Underrun";
        case ::NodeError::FileIOError:
            return "File IO Error";
        case ::NodeError::EndOfStream:
            return "EndOfStream";
        case ::NodeError::FormatError:
            return "Json Format Error";
        case ::NodeError::Critical:
            return "Critical Error";
    }
}
