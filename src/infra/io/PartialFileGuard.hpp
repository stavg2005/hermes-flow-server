
#include <string>
#include <filesystem>
#include "spdlog/spdlog.h"
/**
 * @brief RAII guard. Deletes file on destruction unless committed.
 */
class PartialFileGuard {
   public:
    // Takes ownership of the path
    explicit PartialFileGuard(std::filesystem::path path)
        : path_(std::move(path)), engaged_(true) {}

    // Disarm the guard on success
    void disarm() { engaged_ = false; }

    ~PartialFileGuard() {
        if (engaged_ && !path_.empty() && std::filesystem::exists(path_)) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
            if (ec) {
                spdlog::warn("Failed to remove partial file {}: {}", path_.string(), ec.message());
            } else {
                spdlog::info("Removed partial download file: {}", path_.string());
            }
        }
    }

    // Delete move/copy constructors
    PartialFileGuard(const PartialFileGuard&) = delete;
    PartialFileGuard& operator=(const PartialFileGuard&) = delete;
    PartialFileGuard(PartialFileGuard&&) = delete;
    PartialFileGuard& operator=(PartialFileGuard&&) = delete;

   private:
    std::filesystem::path path_;
    bool engaged_ = true;
};
