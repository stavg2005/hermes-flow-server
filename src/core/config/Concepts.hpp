#pragma once
#include <boost/asio/buffer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <concepts>

namespace hermes::config {

/**
 * @brief Matches any type that acts like a Boost.Asio Write Stream (Socket, File, etc.)
 * Compatible with boost::asio::async_write()
 */
template <typename T>
concept AsyncWriteStream = requires(T& t, boost::asio::const_buffer buffer) {
    // Must have async_write_some compatible with use_awaitable
    { t.async_write_some(buffer, boost::asio::use_awaitable) }
        -> std::convertible_to<boost::asio::awaitable<size_t>>;
};

}
