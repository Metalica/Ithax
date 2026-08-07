#include "network/handshake.h"
#include "network/marshal/marshal.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using ithax::network::Handshake;
using ithax::network::HandshakeError;
using ithax::network::HandshakeState;
using ithax::network::VersionExchange;
using ithax::network::marshal::Decode;
using ithax::network::marshal::Encode;
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

ValuePtr RoundTrip(const Value &value) {
  return Decode(Encode(value), true);
}

void TestFullFlow() {
  Handshake handshake;
  Require(handshake.State() == HandshakeState::WaitVersion,
          "handshake did not start in WAIT_VERSION");

  VersionExchange version;
  version.birthday = 170'472;
  version.macho_version = 496;
  version.user_count = 0;
  version.version_number = 24.01;
  version.build_version = 3'396'210;
  version.project_version = "V24.01@ccp";
  const ValuePtr version_packet = handshake.BuildVersionExchangeClient(version);
  const auto version_decoded = RoundTrip(*version_packet);
  Require(version_decoded->IsTuple() &&
              version_decoded->TupleValue().size() == 6U,
          "version exchange client is not a 6-tuple");
  Require(version_decoded->TupleValue()[0]->IntValue() == 170'472,
          "version exchange birthday is wrong");

  const ValuePtr server_version = Value::Tuple(
      {Value::Int(170'472), Value::Int(496), Value::Int(0),
       Value::Real(24.01), Value::Int(3'396'210),
       Value::String("V24.01@ccp"), Value::None()});
  handshake.OnVersionExchangeServer(*server_version);
  Require(handshake.State() == HandshakeState::WaitCommand,
          "handshake did not advance to WAIT_COMMAND");

  const ValuePtr vk = handshake.BuildVkCommand("test-vip-key");
  const auto vk_decoded = RoundTrip(*vk);
  Require(vk_decoded->IsTuple() && vk_decoded->TupleValue().size() == 3U,
          "VK command is not a 3-tuple");
  Require(vk_decoded->TupleValue()[1]->StringValue() == "VK",
          "VK command marker is wrong");
  handshake.OnVkSent();
  Require(handshake.State() == HandshakeState::WaitCrypto,
          "handshake did not advance to WAIT_CRYPTO");

  ithax::network::CryptoRequest request;
  request.key_version = "placebo";
  request.has_session_key = true;
  request.session_key.fill(0x11U);
  request.has_session_iv = true;
  request.session_iv.fill(0x22U);
  const ValuePtr crypto_request = handshake.BuildCryptoRequest(request);
  const auto crypto_decoded = RoundTrip(*crypto_request);
  Require(crypto_decoded->IsTuple() &&
              crypto_decoded->TupleValue().size() == 2U,
          "crypto request is not a 2-tuple");
  Require(crypto_decoded->TupleValue()[0]->StringValue() == "placebo",
          "crypto request key version is wrong");
  handshake.OnCryptoRequestSent();
  Require(handshake.State() == HandshakeState::WaitCrypto,
          "handshake left WAIT_CRYPTO before OK CC");

  handshake.OnOkCc(*Value::String("OK CC"));
  Require(handshake.State() == HandshakeState::WaitAuth,
          "handshake did not advance to WAIT_AUTH");

  ithax::network::LoginChallenge challenge;
  challenge.user_name = "testuser";
  challenge.user_password_hash = "deadbeef";
  challenge.user_language_id = "EN";
  challenge.user_affiliate_id = 0;
  const ValuePtr login = handshake.BuildLoginChallenge(challenge);
  const auto login_decoded = RoundTrip(*login);
  Require(login_decoded->IsTuple() && login_decoded->TupleValue().size() == 2U,
          "login challenge is not a 2-tuple");
  const auto &login_dict = login_decoded->TupleValue()[1];
  Require(login_dict->IsDict(), "login challenge dict is missing");
  handshake.OnLoginChallengeSent();

  handshake.OnPasswordVersion(*Value::Int(2));
  const ValuePtr server_handshake = Value::Tuple(
      {Value::String(""), Value::Tuple({Value::Buffer({0x74U}), Value::Bool(false)}),
       Value::Dict({}), Value::Dict({})});
  handshake.OnCryptoServerHandshake(*server_handshake);
  Require(handshake.State() == HandshakeState::WaitFuncResult,
          "handshake did not advance to WAIT_FUNC_RESULT");

  ithax::network::HandshakeResult result;
  result.challenge_response_hash = "55087";
  result.func_output = "";
  result.func_result = {0x74U, 0x04U, 0x00U, 0x00U, 0x00U, 0x4EU, 0x6FU,
                        0x6EU, 0x65U};
  const ValuePtr handshake_result = handshake.BuildHandshakeResult(result);
  const auto result_decoded = RoundTrip(*handshake_result);
  Require(result_decoded->IsTuple() &&
              result_decoded->TupleValue().size() == 3U,
          "handshake result is not a 3-tuple");
  handshake.OnHandshakeResultSent();

  const ValuePtr ack = Value::Dict(
      {{Value::String("live_updates"), Value::List({})},
       {Value::String("session_init"), Value::Dict({})},
       {Value::String("sessionID"), Value::Int(1)}});
  handshake.OnCryptoHandshakeAck(*ack);
  Require(handshake.IsComplete(), "handshake did not reach SESSION");
  std::cout << "{\"event\":\"stage4_handshake_full_flow\",\"status\":\"pass\""
            << "}\n";
}

void TestOutOfOrderRejected() {
  Handshake handshake;
  bool rejected = false;
  try {
    handshake.OnOkCc(*Value::String("OK CC"));
  } catch (const HandshakeError &) {
    rejected = true;
  }
  Require(rejected, "out-of-order handshake message was accepted");
  std::cout << "{\"event\":\"stage4_handshake_out_of_order\",\"status\":\"pass\""
            << "}\n";
}

void TestBadOkCcRejected() {
  Handshake handshake;
  const ValuePtr server_version = Value::Tuple(
      {Value::Int(170'472), Value::Int(496), Value::Int(0),
       Value::Real(24.01), Value::Int(3'396'210),
       Value::String("V24.01@ccp"), Value::None()});
  handshake.OnVersionExchangeServer(*server_version);
  bool rejected = false;
  try {
    handshake.OnOkCc(*Value::String("NOPE"));
  } catch (const HandshakeError &) {
    rejected = true;
  }
  Require(rejected, "a wrong OK CC reply was accepted");
  std::cout << "{\"event\":\"stage4_handshake_bad_okcc\",\"status\":\"pass\""
            << "}\n";
}

void TestMalformedServerMessages() {
  Handshake handshake;
  bool rejected = false;
  try {
    handshake.OnVersionExchangeServer(*Value::Int(1));
  } catch (const HandshakeError &) {
    rejected = true;
  }
  Require(rejected, "a non-tuple version exchange was accepted");
  std::cout << "{\"event\":\"stage4_handshake_malformed\",\"status\":\"pass\""
            << "}\n";
}

} // namespace

int main() {
  try {
    TestFullFlow();
    TestOutOfOrderRejected();
    TestBadOkCcRejected();
    TestMalformedServerMessages();
    std::cout << "{\"event\":\"stage4_handshake_suite\",\"status\":\"pass\"}\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "stage4 handshake suite failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
