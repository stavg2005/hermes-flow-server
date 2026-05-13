#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>


namespace hermes::crypto {


struct IEncryptionStrategy {
  virtual ~IEncryptionStrategy() = default;
 
  virtual size_t encrypt(std::span<uint8_t> pcm, uint64_t packet_index) = 0;
};


std::unique_ptr<IEncryptionStrategy> create_aes_encryption_strategy(
    std::span<const uint8_t> key, std::span<const uint8_t> base_iv);

}  // namespace hermes::crypto
