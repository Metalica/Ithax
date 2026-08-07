#include "network/handshake.h"

#include <utility>

namespace ithax::network {

namespace {

constexpr std::int64_t kMachoVersion = 496;
constexpr std::int64_t kEveBirthday = 170'472;
constexpr std::int64_t kBuildVersion = 3'396'210;
constexpr double kClientVersion = 24.01;
constexpr const char *kProjectVersion = "V24.01@ccp";
constexpr const char *kOkCc = "OK CC";

} // namespace

void Handshake::RequireState(HandshakeState expected,
                             HandshakeState actual) {
  if (actual != expected) {
    throw HandshakeStateError("handshake message arrived out of order");
  }
}

const marshal::Value &Handshake::RequireTuple(const marshal::Value &value,
                                              std::size_t min_size) {
  if (!value.IsTuple() || value.TupleValue().size() < min_size) {
    throw HandshakeProtocolError("expected a tuple");
  }
  return value;
}

const marshal::Value &Handshake::RequireDict(const marshal::Value &value) {
  if (!value.IsDict()) {
    throw HandshakeProtocolError("expected a dict");
  }
  return value;
}

std::string Handshake::RequireString(const marshal::Value &value) {
  if (!value.IsString()) {
    throw HandshakeProtocolError("expected a string");
  }
  return value.StringValue();
}

std::int64_t Handshake::RequireInt(const marshal::Value &value) {
  if (!value.IsInt()) {
    throw HandshakeProtocolError("expected an int");
  }
  return value.IntValue();
}

marshal::ValuePtr Handshake::BuildVersionExchangeClient(
    const VersionExchange &version) const {
  std::vector<marshal::ValuePtr> items;
  items.push_back(marshal::Value::Int(version.birthday));
  items.push_back(marshal::Value::Int(version.macho_version));
  items.push_back(marshal::Value::Int(version.user_count));
  items.push_back(marshal::Value::Real(version.version_number));
  items.push_back(marshal::Value::Int(version.build_version));
  items.push_back(marshal::Value::String(version.project_version));
  return marshal::Value::Tuple(std::move(items));
}

marshal::ValuePtr Handshake::BuildVkCommand(const std::string &vip_key) const {
  std::vector<marshal::ValuePtr> items;
  items.push_back(marshal::Value::None());
  items.push_back(marshal::Value::String("VK"));
  items.push_back(marshal::Value::String(vip_key));
  return marshal::Value::Tuple(std::move(items));
}

marshal::ValuePtr Handshake::BuildCryptoRequest(
    const CryptoRequest &request) const {
  std::vector<marshal::DictEntry> params;
  if (request.has_session_key) {
    params.push_back(
        {marshal::Value::String("crypting_sessionkey"),
         marshal::Value::Buffer(std::vector<std::uint8_t>(
             request.session_key.begin(), request.session_key.end()))});
  }
  if (request.has_session_iv) {
    params.push_back(
        {marshal::Value::String("crypting_sessioniv"),
         marshal::Value::Buffer(std::vector<std::uint8_t>(
             request.session_iv.begin(), request.session_iv.end()))});
  }
  std::vector<marshal::ValuePtr> items;
  items.push_back(marshal::Value::String(request.key_version));
  items.push_back(marshal::Value::Dict(std::move(params)));
  return marshal::Value::Tuple(std::move(items));
}

marshal::ValuePtr Handshake::BuildLoginChallenge(
    const LoginChallenge &challenge) const {
  std::vector<marshal::DictEntry> login;
  login.push_back(
      {marshal::Value::String("user_name"),
       marshal::Value::WString(challenge.user_name)});
  login.push_back(
      {marshal::Value::String("user_password_hash"),
       marshal::Value::String(challenge.user_password_hash)});
  login.push_back(
      {marshal::Value::String("user_languageid"),
       marshal::Value::WString(challenge.user_language_id)});
  login.push_back(
      {marshal::Value::String("user_affiliateid"),
       marshal::Value::Int(challenge.user_affiliate_id)});
  login.push_back({marshal::Value::String("macho_version"),
                   marshal::Value::Int(kMachoVersion)});
  login.push_back({marshal::Value::String("boot_version"),
                   marshal::Value::Real(kClientVersion)});
  login.push_back({marshal::Value::String("boot_build"),
                   marshal::Value::Int(kBuildVersion)});
  login.push_back({marshal::Value::String("boot_codename"),
                   marshal::Value::String("Ithax")});
  login.push_back({marshal::Value::String("boot_region"),
                   marshal::Value::String("ccp")});

  std::vector<marshal::ValuePtr> items;
  items.push_back(marshal::Value::String(""));
  items.push_back(marshal::Value::Dict(std::move(login)));
  return marshal::Value::Tuple(std::move(items));
}

marshal::ValuePtr Handshake::BuildHandshakeResult(
    const HandshakeResult &result) const {
  std::vector<marshal::ValuePtr> items;
  items.push_back(marshal::Value::String(result.challenge_response_hash));
  items.push_back(marshal::Value::String(result.func_output));
  items.push_back(marshal::Value::Buffer(result.func_result));
  return marshal::Value::Tuple(std::move(items));
}

void Handshake::OnVkSent() {
  RequireState(HandshakeState::WaitCommand, m_state);
  m_state = HandshakeState::WaitCrypto;
}

void Handshake::OnCryptoRequestSent() {
  RequireState(HandshakeState::WaitCrypto, m_state);
}

void Handshake::OnLoginChallengeSent() {
  RequireState(HandshakeState::WaitAuth, m_state);
}

void Handshake::OnHandshakeResultSent() {
  RequireState(HandshakeState::WaitFuncResult, m_state);
}

void Handshake::OnVersionExchangeServer(const marshal::Value &value) {
  RequireState(HandshakeState::WaitVersion, m_state);
  RequireTuple(value, 7U);
  m_state = HandshakeState::WaitCommand;
}

void Handshake::OnOkCc(const marshal::Value &value) {
  RequireState(HandshakeState::WaitCrypto, m_state);
  if (RequireString(value) != kOkCc) {
    throw HandshakeProtocolError("crypto request was not acknowledged");
  }
  m_state = HandshakeState::WaitAuth;
}

void Handshake::OnPasswordVersion(const marshal::Value &value) {
  RequireState(HandshakeState::WaitAuth, m_state);
  if (RequireInt(value) != 2) {
    throw HandshakeProtocolError("unexpected password version");
  }
}

void Handshake::OnCryptoServerHandshake(const marshal::Value &value) {
  RequireState(HandshakeState::WaitAuth, m_state);
  RequireTuple(value, 4U);
  m_state = HandshakeState::WaitFuncResult;
}

void Handshake::OnCryptoHandshakeAck(const marshal::Value &value) {
  RequireState(HandshakeState::WaitFuncResult, m_state);
  RequireDict(value);
  m_state = HandshakeState::Session;
}

} // namespace ithax::network
