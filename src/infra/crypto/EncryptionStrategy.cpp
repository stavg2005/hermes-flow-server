#include "EncryptionStrategy.hpp"

#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <spdlog/spdlog.h>

#include <cstring>
#include <stdexcept>


namespace hermes::crypto {

constexpr size_t AES_BLOCK_SIZE = 16;
constexpr size_t AES_XOR_OFFSET = 8;

class AESEncryptionStrategy : public IEncryptionStrategy {
 private:
  CryptoPP::SecByteBlock key_;
  CryptoPP::SecByteBlock base_iv_;
  CryptoPP::CTR_Mode<CryptoPP::AES>::Encryption cipher_;

 public:
  AESEncryptionStrategy(std::span<const uint8_t> key,
                        std::span<const uint8_t> base_iv) {
    if (key.size() != AES_BLOCK_SIZE || base_iv.size() != AES_BLOCK_SIZE) {
      throw std::invalid_argument("AES-128 requires 16-byte key and IV");
    }



    key_.Assign(key.data(), key.size());
    base_iv_.Assign(base_iv.data(), base_iv.size());

    cipher_.SetKeyWithIV(key.data(), key.size(), base_iv.data(),
                         base_iv.size());
  }

  size_t encrypt(std::span<uint8_t> pcm, uint64_t packet_index) override {
    if (pcm.empty()) {
      return 0;
    }

    if (base_iv_.size() != AES_BLOCK_SIZE) {
      spdlog::error("Encryption failed: base_iv_ is not 16 bytes!");
      return 0;
    }

    CryptoPP::SecByteBlock current_iv(base_iv_.data(), base_iv_.size());

    // SAFE Little-Endian XOR using the full 48-bit SRTP index
    uint64_t tail = 0;
    std::memcpy(&tail, current_iv.data() + AES_XOR_OFFSET, sizeof(uint64_t));
    tail ^= packet_index;
    std::memcpy(current_iv.data() + AES_XOR_OFFSET, &tail, sizeof(uint64_t));

    try {
      cipher_.Resynchronize(current_iv.data(), static_cast<int>(current_iv.size()));
      cipher_.ProcessData(pcm.data(), pcm.data(), pcm.size());
    } catch (const CryptoPP::Exception& e) {
      spdlog::error("Crypto++ Exception during audio encryption: {}", e.what());
      return 0;
    }

    return pcm.size();
  }
};

// Implementation of the factory function declared in the header
std::unique_ptr<IEncryptionStrategy> create_aes_encryption_strategy(
    std::span<const uint8_t> key, std::span<const uint8_t> base_iv) {
  return std::make_unique<AESEncryptionStrategy>(key, base_iv);
}

}  // namespace hermes::crypto
