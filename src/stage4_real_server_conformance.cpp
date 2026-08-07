#include "network/client.h"
#include "network/marshal/marshal.h"
#include "network/packet.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ithax::network::Client;
using ithax::network::Packet;
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

void TestGetInitVals(const std::string &host, std::uint16_t port) {
  Client client(host, port);
  client.SetReconnectPolicy(0U, 0U);
  client.Connect();
  Require(client.IsConnected(), "client did not connect");
  Require(client.Generation() > 0U, "client generation was not assigned");

  bool got_response = false;
  bool response_valid = false;
  client.SetCallRspHandler(
      [&got_response, &response_valid](const Packet &packet,
                                       const ValuePtr &result) {
        got_response = true;
        if (packet.type != ithax::network::MessageType::CallRsp) {
          return;
        }
        if (result && result->IsTuple() && result->TupleValue().size() >= 2U) {
          response_valid = true;
        }
      });

  client.SendCall("machoNet", "GetInitVals", {});
  constexpr std::uint32_t kMaxReceives = 8U;
  std::uint32_t receives = 0U;
  while (!got_response && receives < kMaxReceives) {
    const ValuePtr value = client.Receive();
    Require(value != nullptr, "no response was received");
    ++receives;
  }
  Require(got_response, "call response handler was not invoked");
  Require(response_valid, "GetInitVals response shape is invalid");

  client.Disconnect();
  std::cout << "{\"event\":\"stage4_real_server_getinitvals\","
            << "\"status\":\"pass\",\"generation\":" << client.Generation()
            << "}\n";
}

void TestPing(const std::string &host, std::uint16_t port) {
  Client client(host, port);
  client.SetReconnectPolicy(0U, 0U);
  client.Connect();
  client.SendPing();
  const ValuePtr value = client.Receive();
  Require(value != nullptr, "ping response was not received");
  client.Disconnect();
  std::cout << "{\"event\":\"stage4_real_server_ping\",\"status\":\"pass\""
            << "}\n";
}

} // namespace

int main(int argc, char *argv[]) {
  try {
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::uint16_t port =
        argc > 2 ? static_cast<std::uint16_t>(std::stoul(argv[2]))
                 : 26000U;
    TestGetInitVals(host, port);
    TestPing(host, port);
    std::cout << "{\"event\":\"stage4_real_server_suite\",\"status\":\"pass\""
              << "}\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "stage4 real server suite failed: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
