#include "network/connection.h"
#include "network/handshake.h"
#include "network/marshal/marshal.h"
#include "network/session_crypto.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ithax::network::Connection;
using ithax::network::ConnectionError;
using ithax::network::ConnectionLoopbackError;
using ithax::network::Handshake;
using ithax::network::HandshakeState;
using ithax::network::SessionCrypto;
using ithax::network::VersionExchange;
using ithax::network::marshal::Value;
using ithax::network::marshal::ValuePtr;

class TestError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

void Require(bool condition, const char *message) {
  if (!condition) {
    throw TestError(message);
  }
}

void TestLoopbackPolicy() {
  Connection connection;
  bool rejected = false;
  try {
    connection.Connect("8.8.8.8", 26000);
  } catch (const ConnectionLoopbackError &) {
    rejected = true;
  }
  Require(rejected, "non-loopback connect was accepted");
  std::cout << "{\"event\":\"stage4_loopback_policy\",\"status\":\"pass\"}\n";
}

void TestFullHandshake(const std::string &host, std::uint16_t port) {
  Connection connection;
  connection.Connect(host, port);
  Require(connection.IsConnected(), "connection did not establish");
  const std::uint64_t generation = connection.Generation();
  Require(generation > 0U, "connection generation was not assigned");

  Handshake handshake;
  Require(handshake.State() == HandshakeState::WaitVersion,
          "handshake did not start in WAIT_VERSION");

  const ValuePtr server_version = connection.ReceiveMarshal();
  handshake.OnVersionExchangeServer(*server_version);
  Require(handshake.State() == HandshakeState::WaitCommand,
          "handshake did not advance after the version exchange");

  VersionExchange version;
  version.birthday = 170'472;
  version.macho_version = 496;
  version.user_count = 0;
  version.version_number = 24.01;
  version.build_version = 3'396'210;
  version.project_version = "V24.01@ccp";
  connection.SendMarshal(*handshake.BuildVersionExchangeClient(version));

  connection.SendMarshal(*handshake.BuildVkCommand("stage4-vip-key"));
  handshake.OnVkSent();

  ithax::network::CryptoRequest request;
  request.key_version = "placebo";
  request.has_session_key = true;
  request.session_key.fill(0x11U);
  request.has_session_iv = true;
  request.session_iv.fill(0x22U);
  connection.SendMarshal(*handshake.BuildCryptoRequest(request));
  handshake.OnCryptoRequestSent();

  const ValuePtr ok_cc = connection.ReceiveMarshal();
  handshake.OnOkCc(*ok_cc);
  Require(handshake.State() == HandshakeState::WaitAuth,
          "handshake did not advance after OK CC");

  ithax::network::LoginChallenge challenge;
  challenge.user_name = "stage4user";
  challenge.user_password_hash = "deadbeef";
  challenge.user_language_id = "EN";
  challenge.user_affiliate_id = 0;
  connection.SendMarshal(*handshake.BuildLoginChallenge(challenge));
  handshake.OnLoginChallengeSent();

  const ValuePtr password_version = connection.ReceiveMarshal();
  handshake.OnPasswordVersion(*password_version);

  const ValuePtr server_handshake = connection.ReceiveMarshal();
  handshake.OnCryptoServerHandshake(*server_handshake);
  Require(handshake.State() == HandshakeState::WaitFuncResult,
          "handshake did not advance after the server handshake");

  ithax::network::HandshakeResult result;
  result.challenge_response_hash = "55087";
  result.func_output = "";
  result.func_result = {0x74U, 0x04U, 0x00U, 0x00U, 0x00U, 0x4EU, 0x6FU,
                        0x6EU, 0x65U};
  connection.SendMarshal(*handshake.BuildHandshakeResult(result));
  handshake.OnHandshakeResultSent();

  const ValuePtr ack = connection.ReceiveMarshal();
  handshake.OnCryptoHandshakeAck(*ack);
  Require(handshake.IsComplete(), "handshake did not reach SESSION");
  Require(connection.Generation() == generation,
          "connection generation changed during the handshake");

  connection.Disconnect();
  Require(!connection.IsConnected(), "connection did not close");
  std::cout << "{\"event\":\"stage4_network_handshake\",\"status\":\"pass\","
            << "\"generation\":" << generation << "}\n";
}

void TestEncryptedSession(const std::string &host, std::uint16_t port) {
  Connection connection;
  connection.Connect(host, port);
  Handshake handshake;

  const ValuePtr server_version = connection.ReceiveMarshal();
  handshake.OnVersionExchangeServer(*server_version);

  VersionExchange version;
  version.birthday = 170'472;
  version.macho_version = 496;
  version.user_count = 0;
  version.version_number = 24.01;
  version.build_version = 3'396'210;
  version.project_version = "V24.01@ccp";
  connection.SendMarshal(*handshake.BuildVersionExchangeClient(version));
  connection.SendMarshal(*handshake.BuildVkCommand("stage4-vip-key"));
  handshake.OnVkSent();

  ithax::network::CryptoRequest request;
  request.key_version = "placebo";
  request.has_session_key = true;
  request.session_key.fill(0x33U);
  request.has_session_iv = true;
  request.session_iv.fill(0x44U);
  connection.SendMarshal(*handshake.BuildCryptoRequest(request));
  handshake.OnCryptoRequestSent();

  const ValuePtr ok_cc = connection.ReceiveMarshal();
  handshake.OnOkCc(*ok_cc);

  ithax::network::LoginChallenge challenge;
  challenge.user_name = "stage4user";
  challenge.user_password_hash = "deadbeef";
  challenge.user_language_id = "EN";
  connection.SendMarshal(*handshake.BuildLoginChallenge(challenge));
  handshake.OnLoginChallengeSent();

  const ValuePtr password_version = connection.ReceiveMarshal();
  handshake.OnPasswordVersion(*password_version);
  const ValuePtr server_handshake = connection.ReceiveMarshal();
  handshake.OnCryptoServerHandshake(*server_handshake);

  ithax::network::HandshakeResult result;
  result.challenge_response_hash = "55087";
  result.func_output = "";
  result.func_result = {0x74U, 0x04U, 0x00U, 0x00U, 0x00U, 0x4EU, 0x6FU,
                        0x6EU, 0x65U};
  connection.SendMarshal(*handshake.BuildHandshakeResult(result));
  handshake.OnHandshakeResultSent();

  const ValuePtr ack = connection.ReceiveMarshal();
  handshake.OnCryptoHandshakeAck(*ack);
  Require(handshake.IsComplete(), "encrypted handshake did not complete");

  connection.SetSessionCrypto(std::make_unique<SessionCrypto>(
      request.session_key, request.session_iv));
  Require(connection.HasSessionCrypto(), "session crypto was not installed");

  const ValuePtr ping = Value::Tuple({Value::Int(20), Value::None()});
  connection.SendMarshal(*ping);
  connection.Disconnect();
  std::cout << "{\"event\":\"stage4_encrypted_session\",\"status\":\"pass\""
            << "}\n";
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    const std::string host =
        argc > 1 ? argv[1] : "127.0.0.1";
    const std::uint16_t port =
        argc > 2 ? static_cast<std::uint16_t>(std::stoul(argv[2]))
                 : 26001U;
    TestLoopbackPolicy();
    TestFullHandshake(host, port);
    TestEncryptedSession(host, port);
    std::cout << "{\"event\":\"stage4_network_suite\",\"status\":\"pass\"}\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "stage4 network suite failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
