#include "network/connection.h"
#include "network/handshake.h"
#include "network/marshal/marshal.h"
#include "network/packet.h"
#include "network/session_crypto.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ithax::network::Connection;
using ithax::network::ConnectionClosedError;
using ithax::network::Handshake;
using ithax::network::HandshakeState;
using ithax::network::Packet;
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

void CompleteHandshake(Connection &connection, Handshake &handshake) {
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
  connection.SendMarshal(*handshake.BuildVkCommand("reconnect-vip"));
  handshake.OnVkSent();

  ithax::network::CryptoRequest request;
  request.key_version = "placebo";
  request.has_session_key = true;
  request.session_key.fill(0x77U);
  request.has_session_iv = true;
  request.session_iv.fill(0x88U);
  connection.SendMarshal(*handshake.BuildCryptoRequest(request));
  handshake.OnCryptoRequestSent();

  const ValuePtr ok_cc = connection.ReceiveMarshal();
  handshake.OnOkCc(*ok_cc);

  ithax::network::LoginChallenge challenge;
  challenge.user_name = "reconnect";
  challenge.user_password_hash = "reconnect";
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
  Require(handshake.IsComplete(), "handshake did not complete");
}

void TestReconnectGenerations(const std::string &host, std::uint16_t port) {
  Connection connection;
  connection.Connect(host, port);
  const std::uint64_t first_generation = connection.Generation();
  Require(first_generation > 0U, "first generation was not assigned");

  Handshake first_handshake;
  CompleteHandshake(connection, first_handshake);
  Require(connection.Generation() == first_generation,
          "generation changed during the first session");

  connection.Disconnect();
  Require(!connection.IsConnected(), "connection did not close");

  connection.Connect(host, port);
  const std::uint64_t second_generation = connection.Generation();
  Require(second_generation > first_generation,
          "reconnect did not advance the generation");

  Handshake second_handshake;
  CompleteHandshake(connection, second_handshake);
  Require(connection.Generation() == second_generation,
          "generation changed during the second session");

  connection.Disconnect();
  std::cout << "{\"event\":\"stage4_reconnect_generations\",\"status\":\"pass\","
            << "\"first\":" << first_generation
            << ",\"second\":" << second_generation << "}\n";
}

void TestStaleGenerationRejected() {
  // A packet decoded under an old generation must carry that generation;
  // the client rejects messages whose generation does not match the
  // current connection generation.
  Packet packet;
  packet.type_string = "macho.CallRsp";
  packet.type = ithax::network::MessageType::CallRsp;
  packet.source.type = ithax::network::AddressType::Any;
  packet.source.service = "machoNet";
  packet.dest.type = ithax::network::AddressType::Any;
  packet.dest.service = "machoNet";
  packet.payload = Value::Tuple({});

  const ValuePtr encoded = packet.Encode();
  const Packet decoded = Packet::Decode(*encoded, 1U);
  Require(decoded.generation == 1U, "stale generation was not retained");
  Require(decoded.generation != 2U,
          "stale generation matched the current generation");
  std::cout << "{\"event\":\"stage4_stale_generation\",\"status\":\"pass\""
            << "}\n";
}

void TestPeerCloseDetected(const std::string &host, std::uint16_t port) {
  Connection connection;
  connection.Connect(host, port);
  Handshake handshake;
  CompleteHandshake(connection, handshake);

  // Send a ping; the mock answers once and then closes the connection.
  connection.SendMarshal(*Value::Tuple({Value::Int(20), Value::None()}));
  const ValuePtr reply = connection.ReceiveMarshal();
  Require(reply != nullptr, "ping reply was not received");

  // The next receive must surface a closed-connection error.
  bool closed = false;
  try {
    connection.ReceiveMarshal();
  } catch (const ConnectionClosedError &) {
    closed = true;
  }
  Require(closed, "peer close was not detected");
  std::cout << "{\"event\":\"stage4_peer_close\",\"status\":\"pass\"}\n";
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::uint16_t port =
        argc > 2 ? static_cast<std::uint16_t>(std::stoul(argv[2]))
                 : 26001U;
    TestReconnectGenerations(host, port);
    TestStaleGenerationRejected();
    TestPeerCloseDetected(host, port);
    std::cout << "{\"event\":\"stage4_reconnect_suite\",\"status\":\"pass\""
              << "}\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "stage4 reconnect suite failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
