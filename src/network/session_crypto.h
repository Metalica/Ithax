#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace ithax::network {

constexpr std::size_t AES_KEY_BYTES = 32U;
constexpr std::size_t AES_IV_BYTES = 16U;
constexpr std::size_t AES_BLOCK_BYTES = 16U;

class SessionCryptoError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class SessionCrypto {
public:
  SessionCrypto(const std::array<std::uint8_t, AES_KEY_BYTES> &key,
                const std::array<std::uint8_t, AES_IV_BYTES> &iv);
  ~SessionCrypto() noexcept;

  SessionCrypto(const SessionCrypto &) = delete;
  SessionCrypto &operator=(const SessionCrypto &) = delete;
  SessionCrypto(SessionCrypto &&) = delete;
  SessionCrypto &operator=(SessionCrypto &&) = delete;

  std::vector<std::uint8_t> Encrypt(
      const std::vector<std::uint8_t> &plaintext);
  std::vector<std::uint8_t> Decrypt(
      const std::vector<std::uint8_t> &ciphertext);

  const std::array<std::uint8_t, AES_IV_BYTES> &CurrentIv() const noexcept;

private:
  void UpdateIv(const std::vector<std::uint8_t> &ciphertext) noexcept;

  std::array<std::uint8_t, AES_KEY_BYTES> m_key;
  std::array<std::uint8_t, AES_IV_BYTES> m_iv;
};

} // namespace ithax::network
