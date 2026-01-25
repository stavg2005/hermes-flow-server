#pragma once
#include <boost/asio/stream_file.hpp>
#include <expected>
#include <memory>

#include "PartialFileGuard.hpp"


namespace hermes::infra {

class FileSink {
 public:
  explicit FileSink(boost::asio::io_context& ioc) : file_(ioc) {}

  // Movable (enabled by unique_ptr)
  FileSink(FileSink&&) = default;
  FileSink& operator=(FileSink&&) = default;

  // Destructor: CRITICAL ORDERING
  ~FileSink() {
    boost::system::error_code ec;
    file_.close(ec);
  }

  std::expected<void, std::string> Prepare(const std::filesystem::path& path) {
    boost::system::error_code ec;

    // 1. Create Parent Directories
    if (auto parent = path.parent_path(); !parent.empty()) {
      std::filesystem::create_directories(parent, ec);
      if (ec) return std::unexpected("Dir error: " + ec.message());
    }

    // 2. Open the Async Stream File
    file_.open(path.string(),
               boost::asio::stream_file::write_only |
                   boost::asio::stream_file::create |
                   boost::asio::stream_file::truncate,
               ec);

    if (ec) return std::unexpected("File open error: " + ec.message());

    guard_ = std::make_unique<PartialFileGuard>(path);

    return {};
  }

  void Commit() {
    if (guard_) {
      guard_->disarm();
    }
  }

  using executor_type = boost::asio::stream_file::executor_type;
  executor_type get_executor() noexcept { return file_.get_executor(); }
  // ASIO Concept Compliance
  // This allows S3Session to write to this class directly.
  template <typename ConstBufferSequence, typename CompletionToken>
  auto async_write_some(const ConstBufferSequence& buffers,
                        CompletionToken&& token) {
    return file_.async_write_some(buffers,
                                  std::forward<CompletionToken>(token));
  }

 private:
  boost::asio::stream_file file_;
  std::unique_ptr<PartialFileGuard> guard_;
};

}  // namespace hermes::infra
