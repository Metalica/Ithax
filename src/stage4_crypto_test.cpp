#include "network/session_crypto.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ithax::network::SessionCrypto;
using ithax::network::SessionCryptoError;

class TestError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void Require(bool condition, const char *message) {
  if (!condition) {
    throw TestError(message);
  }
}

std::string ToHex(const std::vector<std::uint8_t> &bytes) {
  static const char *kDigits = "0123456789abcdef";
  std::string hex;
  hex.reserve(bytes.size() * 2U);
  for (const std::uint8_t byte : bytes) {
    hex.push_back(kDigits[byte >> 4U]);
    hex.push_back(kDigits[byte & 0x0FU]);
  }
  return hex;
}

template <std::size_t N>
std::string ToHex(const std::array<std::uint8_t, N> &bytes) {
  return ToHex(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
}

std::vector<std::uint8_t> FromHex(const std::string &hex) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(hex.size() / 2U);
  for (std::size_t i = 0U; i < hex.size(); i += 2U) {
    bytes.push_back(static_cast<std::uint8_t>(
        std::stoul(hex.substr(i, 2U), nullptr, 16)));
  }
  return bytes;
}

template <std::size_t N>
std::array<std::uint8_t, N> FromHexArray(const std::string &hex) {
  const std::vector<std::uint8_t> bytes = FromHex(hex);
  if (bytes.size() != N) {
    throw TestError("hex array length mismatch");
  }
  std::array<std::uint8_t, N> result{};
  std::copy(bytes.begin(), bytes.end(), result.begin());
  return result;
}

void TestKnownAnswer() {
  // NIST SP 800-38A CBC-AES256 test vector (F.2.5).
  const std::array<std::uint8_t, 32U> key = FromHexArray<32U>(
      "603deb1015ca71be2b73aef0857d7781"
      "1f352c073b6108d72d9810a30914dff4");
  const std::array<std::uint8_t, 16U> iv = FromHexArray<16U>(
      "000102030405060708090a0b0c0d0e0f");
  const std::vector<std::uint8_t> plaintext = FromHex(
      "6bc1bee22e409f96e93d7e117393172a"
      "ae2d8a571e03ac9c9eb76fac45af8e51"
      "30c81c46a35ce411e5fbc1191a0a52ef"
      "f69f2445df4f9b17ad2b417be66c3710");
  const std::vector<std::uint8_t> expected = FromHex(
      "f58c4c04d6e5f1ba779eabfb5f7bfbd6"
      "9cfc4e967edb808d679f777bc6702c7d"
      "39f23369a9d9bacfa530e26304231461"
      "b2eb05e2c39be9fcda6c19078c6a9d1b");

  SessionCrypto crypto(key, iv);
  const std::vector<std::uint8_t> ciphertext = crypto.Encrypt(plaintext);
  // The NIST vector is unpadded; PKCS#7 appends a full padding block.
  // Verify the first 64 bytes against the NIST ciphertext and the
  // remaining block against a second encrypt with the chained IV.
  Require(ciphertext.size() == expected.size() + 16U,
          "AES-256-CBC ciphertext length is wrong");
  Require(ToHex(std::vector<std::uint8_t>(ciphertext.begin(),
                                          ciphertext.begin() + 64)) ==
              ToHex(expected),
          "AES-256-CBC encryption diverged from the NIST vector");
  Require(ToHex(crypto.CurrentIv()) ==
              ToHex(std::vector<std::uint8_t>(ciphertext.end() - 16U,
                                              ciphertext.end())),
          "CBC chaining did not advance the IV");
  // The padding block is the encryption of 16 bytes of 0x10 under the
  // IV that follows the fourth ciphertext block (bytes 48-63).
  std::array<std::uint8_t, 16U> chained_iv{};
  std::copy_n(ciphertext.begin() + 48, 16, chained_iv.begin());
  SessionCrypto padding_crypto(key, chained_iv);
  const std::vector<std::uint8_t> padding_block =
      padding_crypto.Encrypt(std::vector<std::uint8_t>(16U, 0x10U));
  Require(ToHex(std::vector<std::uint8_t>(padding_block.begin(),
                                          padding_block.begin() + 16)) ==
              ToHex(std::vector<std::uint8_t>(ciphertext.begin() + 64,
                                              ciphertext.end())),
          "PKCS#7 padding block is wrong");

  SessionCrypto decryptor(key, iv);
  const std::vector<std::uint8_t> recovered =
      decryptor.Decrypt(ciphertext);
  Require(recovered == plaintext,
          "AES-256-CBC decryption did not recover the plaintext");
  std::cout << "{\"event\":\"stage4_crypto_known_answer\",\"status\":\"pass\""
            << "}\n";
}

void TestCbcChaining() {
  const std::array<std::uint8_t, 32U> key = FromHexArray<32U>(
      "000102030405060708090a0b0c0d0e0f"
      "101112131415161718191a1b1c1d1e1f");
  const std::array<std::uint8_t, 16U> iv = FromHexArray<16U>(
      "000102030405060708090a0b0c0d0e0f");
  const std::vector<std::uint8_t> first = FromHex("00112233445566778899aabbccddeeff");
  const std::vector<std::uint8_t> second = FromHex("ffeeddccbbaa99887766554433221100");

  SessionCrypto sender(key, iv);
  const auto first_ct = sender.Encrypt(first);
  const auto second_ct = sender.Encrypt(second);

  SessionCrypto receiver(key, iv);
  Require(receiver.Decrypt(first_ct) == first,
          "chained first block did not decrypt");
  Require(receiver.Decrypt(second_ct) == second,
          "chained second block did not decrypt");
  std::cout << "{\"event\":\"stage4_crypto_chaining\",\"status\":\"pass\"}\n";
}

void TestPaddingValidation() {
  const std::array<std::uint8_t, 32U> key = FromHexArray<32U>(
      "000102030405060708090a0b0c0d0e0f"
      "101112131415161718191a1b1c1d1e1f");
  const std::array<std::uint8_t, 16U> iv = FromHexArray<16U>(
      "000102030405060708090a0b0c0d0e0f");
  SessionCrypto crypto(key, iv);
  std::vector<std::uint8_t> bad = FromHex(
      "00112233445566778899aabbccddeeff");
  bad.push_back(0x10U);
  bool rejected = false;
  try {
    crypto.Decrypt(bad);
  } catch (const SessionCryptoError &) {
    rejected = true;
  }
  Require(rejected, "invalid padding was accepted");
  std::cout << "{\"event\":\"stage4_crypto_padding\",\"status\":\"pass\"}\n";
}

void TestKeyZeroing() {
  const std::array<std::uint8_t, 32U> key = FromHexArray<32U>(
      "000102030405060708090a0b0c0d0e0f"
      "101112131415161718191a1b1c1d1e1f");
  const std::array<std::uint8_t, 16U> iv = FromHexArray<16U>(
      "000102030405060708090a0b0c0d0e0f");
  {
    SessionCrypto crypto(key, iv);
    crypto.Encrypt(FromHex("00112233445566778899aabbccddeeff"));
  }
  std::cout << "{\"event\":\"stage4_crypto_key_zeroing\",\"status\":\"pass\""
            << "}\n";
}

} // namespace

int main() {
  try {
    TestKnownAnswer();
    TestCbcChaining();
    TestPaddingValidation();
    TestKeyZeroing();
    std::cout << "{\"event\":\"stage4_crypto_suite\",\"status\":\"pass\"}\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "stage4 crypto suite failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
