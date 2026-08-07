#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "network/marshal/marshal.h"

namespace ithax::network {

constexpr std::uint32_t HANDSHAKE_STEP_TIMEOUT_MS = 30'000U;

enum class HandshakeState {
  WaitVersion,
  WaitCommand,
  WaitCrypto,
  WaitAuth,
  WaitFuncResult,
  Session,
};

class HandshakeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class HandshakeProtocolError : public HandshakeError {
public:
  using HandshakeError::HandshakeError;
};

class HandshakeStateError : public HandshakeError {
public:
  using HandshakeError::HandshakeError;
};

struct VersionExchange {
  std::int64_t birthday = 0;
  std::int64_t macho_version = 0;
  std::int64_t user_count = 0;
  double version_number = 0.0;
  std::int64_t build_version = 0;
  std::string project_version;
};

struct CryptoRequest {
  std::string key_version;
  std::array<std::uint8_t, 32U> session_key{};
  std::array<std::uint8_t, 16U> session_iv{};
  bool has_session_key = false;
  bool has_session_iv = false;
};

struct LoginChallenge {
  std::string user_name;
  std::string user_password_hash;
  std::string user_language_id;
  std::int64_t user_affiliate_id = 0;
};

struct HandshakeResult {
  std::string challenge_response_hash;
  std::string func_output;
  std::vector<std::uint8_t> func_result;
};

class Handshake {
public:
  HandshakeState State() const noexcept { return m_state; }
  bool IsComplete() const noexcept {
    return m_state == HandshakeState::Session;
  }

  marshal::ValuePtr BuildVersionExchangeClient(
      const VersionExchange &version) const;
  marshal::ValuePtr BuildVkCommand(const std::string &vip_key) const;
  marshal::ValuePtr BuildCryptoRequest(const CryptoRequest &request) const;
  marshal::ValuePtr BuildLoginChallenge(const LoginChallenge &challenge) const;
  marshal::ValuePtr BuildHandshakeResult(const HandshakeResult &result) const;

  void OnVkSent();
  void OnCryptoRequestSent();
  void OnLoginChallengeSent();
  void OnHandshakeResultSent();
  void OnVersionExchangeServer(const marshal::Value &value);
  void OnOkCc(const marshal::Value &value);
  void OnPasswordVersion(const marshal::Value &value);
  void OnCryptoServerHandshake(const marshal::Value &value);
  void OnCryptoHandshakeAck(const marshal::Value &value);

private:
  static void RequireState(HandshakeState expected, HandshakeState actual);
  static const marshal::Value &RequireTuple(const marshal::Value &value,
                                            std::size_t min_size);
  static const marshal::Value &RequireDict(const marshal::Value &value);
  static std::string RequireString(const marshal::Value &value);
  static std::int64_t RequireInt(const marshal::Value &value);

  HandshakeState m_state = HandshakeState::WaitVersion;
};

} // namespace ithax::network
