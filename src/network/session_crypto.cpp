#include "network/session_crypto.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cstring>

namespace ithax::network {

namespace {

void SecureZero(void *data, std::size_t size) noexcept {
  if (data == nullptr || size == 0U) {
    return;
  }
  volatile std::uint8_t *bytes = static_cast<volatile std::uint8_t *>(data);
  while (size-- > 0U) {
    *bytes++ = 0U;
  }
}

} // namespace

SessionCrypto::SessionCrypto(
    const std::array<std::uint8_t, AES_KEY_BYTES> &key,
    const std::array<std::uint8_t, AES_IV_BYTES> &iv)
    : m_key(key), m_iv(iv) {}

SessionCrypto::~SessionCrypto() noexcept {
  SecureZero(m_key.data(), m_key.size());
  SecureZero(m_iv.data(), m_iv.size());
}

std::vector<std::uint8_t> SessionCrypto::Encrypt(
    const std::vector<std::uint8_t> &plaintext) {
  EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
  if (context == nullptr) {
    throw SessionCryptoError("failed to allocate the cipher context");
  }
  try {
    if (EVP_EncryptInit_ex(context, EVP_aes_256_cbc(), nullptr, m_key.data(),
                           m_iv.data()) != 1) {
      throw SessionCryptoError("failed to initialize the cipher");
    }
    if (EVP_CIPHER_CTX_set_padding(context, 1) != 1) {
      throw SessionCryptoError("failed to configure PKCS#7 padding");
    }
    std::vector<std::uint8_t> output(
        plaintext.size() + AES_BLOCK_BYTES, 0U);
    int written = 0;
    if (EVP_EncryptUpdate(context, output.data(), &written,
                          plaintext.data(),
                          static_cast<int>(plaintext.size())) != 1) {
      throw SessionCryptoError("failed to encrypt the payload");
    }
    int final_written = 0;
    if (EVP_EncryptFinal_ex(context, output.data() + written, &final_written) !=
        1) {
      throw SessionCryptoError("failed to finalize the cipher");
    }
    output.resize(static_cast<std::size_t>(written + final_written));
    UpdateIv(output);
    EVP_CIPHER_CTX_free(context);
    return output;
  } catch (...) {
    EVP_CIPHER_CTX_free(context);
    throw;
  }
}

std::vector<std::uint8_t> SessionCrypto::Decrypt(
    const std::vector<std::uint8_t> &ciphertext) {
  if (ciphertext.empty() || ciphertext.size() % AES_BLOCK_BYTES != 0U) {
    throw SessionCryptoError("ciphertext length is not block aligned");
  }
  EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();
  if (context == nullptr) {
    throw SessionCryptoError("failed to allocate the cipher context");
  }
  try {
    if (EVP_DecryptInit_ex(context, EVP_aes_256_cbc(), nullptr, m_key.data(),
                           m_iv.data()) != 1) {
      throw SessionCryptoError("failed to initialize the decipher");
    }
    if (EVP_CIPHER_CTX_set_padding(context, 1) != 1) {
      throw SessionCryptoError("failed to configure PKCS#7 padding");
    }
    std::vector<std::uint8_t> output(ciphertext.size(), 0U);
    int written = 0;
    if (EVP_DecryptUpdate(context, output.data(), &written,
                          ciphertext.data(),
                          static_cast<int>(ciphertext.size())) != 1) {
      throw SessionCryptoError("failed to decrypt the payload");
    }
    int final_written = 0;
    if (EVP_DecryptFinal_ex(context, output.data() + written,
                            &final_written) != 1) {
      throw SessionCryptoError("padding validation failed");
    }
    output.resize(static_cast<std::size_t>(written + final_written));
    UpdateIv(ciphertext);
    EVP_CIPHER_CTX_free(context);
    return output;
  } catch (...) {
    EVP_CIPHER_CTX_free(context);
    throw;
  }
}

const std::array<std::uint8_t, AES_IV_BYTES> &SessionCrypto::CurrentIv()
    const noexcept {
  return m_iv;
}

void SessionCrypto::UpdateIv(
    const std::vector<std::uint8_t> &ciphertext) noexcept {
  if (ciphertext.size() < AES_IV_BYTES) {
    return;
  }
  std::copy_n(ciphertext.end() - static_cast<std::ptrdiff_t>(AES_IV_BYTES),
              AES_IV_BYTES, m_iv.begin());
}

} // namespace ithax::network
