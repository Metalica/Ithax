#include "network/dispatcher.h"
#include "network/marshal/marshal.h"
#include "network/packet.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ithax::network::Address;
using ithax::network::AddressType;
using ithax::network::CallRequest;
using ithax::network::Dispatcher;
using ithax::network::DispatcherUnknownMethodError;
using ithax::network::MessageType;
using ithax::network::Packet;
using ithax::network::PacketError;
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

void TestAddressRoundTrip() {
  Address any;
  any.type = AddressType::Any;
  any.service = "machoNet";
  any.object_id = 0;
  const Address any_decoded = Address::Decode(*RoundTrip(*any.Encode()));
  Require(any_decoded.type == AddressType::Any &&
              any_decoded.service == "machoNet",
          "any address round trip failed");

  Address node;
  node.type = AddressType::Node;
  node.object_id = 0xFFAA;
  node.service = "beyonce";
  node.call_id = 7;
  const Address node_decoded = Address::Decode(*RoundTrip(*node.Encode()));
  Require(node_decoded.type == AddressType::Node &&
              node_decoded.object_id == 0xFFAA &&
              node_decoded.service == "beyonce" &&
              node_decoded.call_id == 7,
          "node address round trip failed");

  Address client;
  client.type = AddressType::Client;
  client.object_id = 42;
  client.call_id = 9;
  client.service = "charMgr";
  const Address client_decoded =
      Address::Decode(*RoundTrip(*client.Encode()));
  Require(client_decoded.type == AddressType::Client &&
              client_decoded.object_id == 42 &&
              client_decoded.call_id == 9 &&
              client_decoded.service == "charMgr",
          "client address round trip failed");

  Address broadcast;
  broadcast.type = AddressType::Broadcast;
  broadcast.service = "LSC";
  broadcast.broadcast_idtype = "channelID";
  const Address broadcast_decoded =
      Address::Decode(*RoundTrip(*broadcast.Encode()));
  Require(broadcast_decoded.type == AddressType::Broadcast &&
              broadcast_decoded.service == "LSC" &&
              broadcast_decoded.broadcast_idtype == "channelID",
          "broadcast address round trip failed");
  std::cout << "{\"event\":\"stage4_packet_addresses\",\"status\":\"pass\""
            << "}\n";
}

void TestPacketRoundTrip() {
  Packet packet;
  packet.type_string = "macho.CallReq";
  packet.type = MessageType::CallReq;
  packet.source.type = AddressType::Any;
  packet.source.service = "machoNet";
  packet.dest.type = AddressType::Any;
  packet.dest.service = "machoNet";
  packet.user_id = 0;
  packet.payload = Value::Tuple({});
  packet.named_payload = Value::None();

  const ValuePtr encoded = packet.Encode();
  const Packet decoded = Packet::Decode(*RoundTrip(*encoded), 3U);
  Require(decoded.type == MessageType::CallReq,
          "packet type did not round trip");
  Require(decoded.type_string == "macho.CallReq",
          "packet type string did not round trip");
  Require(decoded.generation == 3U, "packet generation did not round trip");
  Require(decoded.source.service == "machoNet",
          "packet source did not round trip");
  std::cout << "{\"event\":\"stage4_packet_roundtrip\",\"status\":\"pass\""
            << "}\n";
}

void TestCallRequestDecode() {
  std::vector<ValuePtr> call_body;
  call_body.push_back(Value::String("machoNet"));
  call_body.push_back(Value::String("GetInitVals"));
  call_body.push_back(Value::Tuple({}));
  call_body.push_back(Value::None());

  std::vector<ValuePtr> wrapper;
  wrapper.push_back(Value::Int(0));
  wrapper.push_back(Value::SubStream(
      Encode(*Value::Tuple(std::move(call_body)))));

  std::vector<ValuePtr> payload;
  payload.push_back(Value::Tuple(std::move(wrapper)));

  Packet packet;
  packet.type_string = "macho.CallReq";
  packet.type = MessageType::CallReq;
  packet.source.type = AddressType::Any;
  packet.source.service = "machoNet";
  packet.source.call_id = 5;
  packet.dest.type = AddressType::Any;
  packet.dest.service = "machoNet";
  packet.payload = Value::Tuple(std::move(payload));

  const CallRequest call = CallRequest::Decode(packet);
  Require(call.service == "machoNet", "call service is wrong");
  Require(call.method == "GetInitVals", "call method is wrong");
  Require(call.args.empty(), "call args should be empty");
  Require(call.call_id == 5, "call id is wrong");
  std::cout << "{\"event\":\"stage4_call_request_decode\",\"status\":\"pass\""
            << "}\n";
}

void TestDispatcher() {
  Dispatcher dispatcher;
  dispatcher.Register("machoNet", "GetInitVals",
                      [](const CallRequest &, const Packet &) {
                        return Value::Dict({});
                      });
  dispatcher.Register("ping", "Ping",
                      [](const CallRequest &, const Packet &) {
                        return Value::None();
                      });
  Require(dispatcher.HasHandler("machoNet", "GetInitVals"),
          "registered handler was not found");
  Require(!dispatcher.HasHandler("machoNet", "Nope"),
          "unregistered handler was found");

  std::vector<ValuePtr> call_body;
  call_body.push_back(Value::String("machoNet"));
  call_body.push_back(Value::String("GetInitVals"));
  call_body.push_back(Value::Tuple({}));
  call_body.push_back(Value::None());
  std::vector<ValuePtr> wrapper;
  wrapper.push_back(Value::Int(0));
  wrapper.push_back(Value::SubStream(
      Encode(*Value::Tuple(std::move(call_body)))));
  std::vector<ValuePtr> payload;
  payload.push_back(Value::Tuple(std::move(wrapper)));

  Packet packet;
  packet.type_string = "macho.CallReq";
  packet.type = MessageType::CallReq;
  packet.source.type = AddressType::Any;
  packet.source.service = "machoNet";
  packet.dest.type = AddressType::Any;
  packet.dest.service = "machoNet";
  packet.payload = Value::Tuple(std::move(payload));

  const ValuePtr result = dispatcher.Dispatch(packet);
  Require(result->IsDict(), "dispatch result is wrong");

  bool rejected = false;
  try {
    std::vector<ValuePtr> bad_body;
    bad_body.push_back(Value::String("machoNet"));
    bad_body.push_back(Value::String("Nope"));
    bad_body.push_back(Value::Tuple({}));
    bad_body.push_back(Value::None());
    std::vector<ValuePtr> bad_wrapper;
    bad_wrapper.push_back(Value::Int(0));
    bad_wrapper.push_back(Value::SubStream(
        Encode(*Value::Tuple(std::move(bad_body)))));
    std::vector<ValuePtr> bad_payload;
    bad_payload.push_back(Value::Tuple(std::move(bad_wrapper)));
    Packet bad_packet = packet;
    bad_packet.payload = Value::Tuple(std::move(bad_payload));
    dispatcher.Dispatch(bad_packet);
  } catch (const DispatcherUnknownMethodError &) {
    rejected = true;
  }
  Require(rejected, "unknown method was dispatched");
  std::cout << "{\"event\":\"stage4_dispatcher\",\"status\":\"pass\"}\n";
}

void TestMalformedPackets() {
  struct Case {
    const char *name;
    ValuePtr value;
  };
  const std::vector<Case> cases = {
      {"not_object", Value::Int(1)},
      {"short_tuple",
       Value::Object("macho.CallReq", Value::Tuple({Value::Int(6)}))},
      {"bad_address",
       Value::Object("macho.CallReq",
                     Value::Tuple({Value::Int(6), Value::Int(1),
                                   Value::Int(1), Value::None(),
                                   Value::Tuple({}), Value::None(),
                                   Value::None()}))},
  };
  for (const auto &test : cases) {
    bool rejected = false;
    try {
      Packet::Decode(*test.value, 1U);
    } catch (const PacketError &) {
      rejected = true;
    }
    Require(rejected, "malformed packet was accepted");
  }
  std::cout << "{\"event\":\"stage4_packet_malformed\",\"status\":\"pass\","
            << "\"cases\":" << cases.size() << "}\n";
}

} // namespace

int main() {
  try {
    TestAddressRoundTrip();
    TestPacketRoundTrip();
    TestCallRequestDecode();
    TestDispatcher();
    TestMalformedPackets();
    std::cout << "{\"event\":\"stage4_packet_suite\",\"status\":\"pass\"}\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "stage4 packet suite failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
