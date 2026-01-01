#pragma once

#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>


/**
 * @brief High-level HTTP Server Facade.
 * * @details
 * **Compilation Firewall (PIMPL Pattern):**
 * This class holds a `unique_ptr` to an internal `Impl` struct defined only
 * in the .cpp file.
 * * **Why?**
 * 1. **Compile Speed:** Changes to Boost.Beast/Asio headers in `Server.cpp`
 * do not force a recompile of `main.cpp` or other files including `Server.hpp`.
 * 2. **ABI Stability:** The memory layout of `Server` does not change even if
 * we add members to the implementation.
 */
class Server : public std::enable_shared_from_this<Server> {
   public:
    Server(boost::asio::io_context& io, const std::string& address, const std::string& port,
           unsigned int num_threads);
    ~Server();

    void Start();
    void Stop();

   private:
    struct Impl;
    std::unique_ptr<Impl> pImpl_;
};
