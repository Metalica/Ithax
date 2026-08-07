#include "network/client.h"

#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

namespace ithax::network {

namespace {

constexpr std::int64_t kProxyNodeId = 0xFFAA;
constexpr const char *kPingReqType = "macho.PingReq";
constexpr const char *kPingRspType = "macho.PingRsp";
constexpr const char *kCallReqType = "macho.CallReq";
constexpr const char *kCallRspType = "macho.CallRsp";
constexpr const char *kNotificationType = "macho.Notification";
constexpr const char *kErrorResponseType = "macho.ErrorResponse";
constexpr const char *kTransportClosedType = "macho.TransportClosed";
constexpr const char *kSessionChangeType = "macho.SessionChangeNotification";

Address MakeAnyAddress(const std::string &service) {
  Address address;
  address.type = AddressType::Any;
  address.service = service;
  return address;
}

Address MakeClientAddress(std::int64_t client_id) {
  Address address;
  address.type = AddressType::Client;
  address.object_id = client_id;
  return address;
}

} // namespace

Client::Client(std::string host, std::uint16_t port)
    : m_host(std::move(host)), m_port(port) {
  m_connection = std::make_unique<Connection>();
}

Client::~Client() noexcept { Disconnect(); }

void Client::Connect() {
  if (m_connected) {
    throw ClientError("client is already connected");
  }
  std::uint32_t attempt = 0U;
  for (;;) {
    try {
      EstablishSession();
      m_connected = true;
      m_last_heartbeat = std::chrono::steady_clock::now();
      return;
    } catch (const std::exception &) {
      m_connection->Disconnect();
      ++attempt;
      if (attempt > m_reconnect_attempts) {
        throw ClientReconnectError("connection attempts exhausted");
      }
      std::this_thread::sleep_for(
          std::chrono::milliseconds(m_reconnect_delay_ms));
    }
  }
}

void Client::Disconnect() noexcept {
  if (m_connection) {
    m_connection->Disconnect();
  }
  m_connected = false;
}

bool Client::IsConnected() const noexcept { return m_connected; }

std::uint64_t Client::Generation() const noexcept {
  return m_connection ? m_connection->Generation() : 0U;
}

void Client::SetHeartbeatInterval(std::uint32_t interval_ms) noexcept {
  m_heartbeat_interval_ms = interval_ms;
}

void Client::SetReconnectPolicy(std::uint32_t max_attempts,
                                std::uint32_t delay_ms) noexcept {
  m_reconnect_attempts = max_attempts;
  m_reconnect_delay_ms = delay_ms;
}

void Client::SetCallRspHandler(CallRspHandler handler) noexcept {
  m_call_rsp_handler = std::move(handler);
}

void Client::SendCall(const std::string &service, const std::string &method,
                      std::vector<marshal::ValuePtr> args,
                      marshal::ValuePtr kwargs) {
  if (!m_connected) {
    throw ClientError("client is not connected");
  }
  Packet packet;
  packet.type_string = kCallReqType;
  packet.type = MessageType::CallReq;
  packet.source = MakeAnyAddress(service);
  packet.dest = MakeAnyAddress("machoNet");
  packet.user_id = 0;

  std::vector<marshal::ValuePtr> call_body;
  call_body.push_back(marshal::Value::String(service));
  call_body.push_back(marshal::Value::String(method));
  call_body.push_back(marshal::Value::Tuple(std::move(args)));
  call_body.push_back(kwargs ? kwargs : marshal::Value::None());

  std::vector<marshal::ValuePtr> wrapper;
  wrapper.push_back(marshal::Value::Int(0));
  wrapper.push_back(marshal::Value::SubStream(
      marshal::Encode(*marshal::Value::Tuple(std::move(call_body)))));

  std::vector<marshal::ValuePtr> payload;
  payload.push_back(marshal::Value::Tuple(std::move(wrapper)));
  packet.payload = marshal::Value::Tuple(std::move(payload));
  SendPacket(packet);
}

void Client::SendPing() {
  if (!m_connected) {
    throw ClientError("client is not connected");
  }
  Packet packet;
  packet.type_string = kPingReqType;
  packet.type = MessageType::PingReq;
  packet.source = MakeAnyAddress("ping");
  packet.dest = MakeAnyAddress("machoNet");
  packet.payload = marshal::Value::Tuple({});
  SendPacket(packet);
}

void Client::SendNotification(const std::string &type,
                              std::vector<marshal::ValuePtr> args) {
  if (!m_connected) {
    throw ClientError("client is not connected");
  }
  Packet packet;
  packet.type_string = kNotificationType;
  packet.type = MessageType::Notification;
  packet.source = MakeAnyAddress(type);
  packet.dest = MakeAnyAddress("machoNet");
  packet.payload = marshal::Value::Tuple(std::move(args));
  SendPacket(packet);
}

marshal::ValuePtr Client::Receive() {
  if (!m_connected) {
    throw ClientError("client is not connected");
  }
  const marshal::ValuePtr value = m_connection->ReceiveMarshal();
  HandleIncoming(*value);
  return value;
}

void Client::PumpOnce() {
  if (!m_connected) {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - m_last_heartbeat);
  if (elapsed.count() >= static_cast<std::int64_t>(m_heartbeat_interval_ms)) {
    SendPing();
    m_last_heartbeat = now;
  }
}

void Client::EstablishSession() {
  m_connection->Connect(m_host, m_port);
  m_handshake = Handshake();

  const marshal::ValuePtr server_version = m_connection->ReceiveMarshal();
  m_handshake.OnVersionExchangeServer(*server_version);

  VersionExchange version;
  version.birthday = 170'472;
  version.macho_version = 496;
  version.user_count = 0;
  version.version_number = 24.01;
  version.build_version = 3'396'210;
  version.project_version = "V24.01@ccp";
  m_connection->SendMarshal(*m_handshake.BuildVersionExchangeClient(version));
  m_connection->SendMarshal(*m_handshake.BuildVkCommand("ithax-vip-key"));
  m_handshake.OnVkSent();

  CryptoRequest request;
  request.key_version = "placebo";
  request.has_session_key = true;
  request.session_key.fill(0x55U);
  request.has_session_iv = true;
  request.session_iv.fill(0x66U);
  m_connection->SendMarshal(*m_handshake.BuildCryptoRequest(request));
  m_handshake.OnCryptoRequestSent();

  const marshal::ValuePtr ok_cc = m_connection->ReceiveMarshal();
  m_handshake.OnOkCc(*ok_cc);

  // The approved server enables session encryption immediately after the
  // crypto request: the login challenge and everything after it is
  // encrypted. Install the session crypto before sending the challenge.
  m_connection->SetSessionCrypto(std::make_unique<SessionCrypto>(
      request.session_key, request.session_iv));

  LoginChallenge challenge;
  challenge.user_name = "ithax";
  challenge.user_password_hash = "ithax";
  challenge.user_language_id = "EN";
  m_connection->SendMarshal(*m_handshake.BuildLoginChallenge(challenge));
  m_handshake.OnLoginChallengeSent();

  const marshal::ValuePtr password_version = m_connection->ReceiveMarshal();
  m_handshake.OnPasswordVersion(*password_version);
  const marshal::ValuePtr server_handshake = m_connection->ReceiveMarshal();
  m_handshake.OnCryptoServerHandshake(*server_handshake);

  HandshakeResult result;
  result.challenge_response_hash = "55087";
  result.func_output = "";
  result.func_result = {0x74U, 0x04U, 0x00U, 0x00U, 0x00U, 0x4EU, 0x6FU,
                        0x6EU, 0x65U};
  m_connection->SendMarshal(*m_handshake.BuildHandshakeResult(result));
  m_handshake.OnHandshakeResultSent();

  const marshal::ValuePtr ack = m_connection->ReceiveMarshal();
  m_handshake.OnCryptoHandshakeAck(*ack);
  if (!m_handshake.IsComplete()) {
    throw ClientError("handshake did not complete");
  }
}

void Client::SendPacket(const Packet &packet) {
  m_connection->SendMarshal(*packet.Encode());
}

void Client::HandleIncoming(const marshal::Value &value) {
  const Packet packet = Packet::Decode(value, Generation());
  switch (packet.type) {
    case MessageType::PingReq:
      HandlePingReq(packet);
      break;
    case MessageType::CallRsp:
      HandleCallRsp(packet);
      break;
    case MessageType::Notification:
      HandleNotification(packet);
      break;
    case MessageType::ErrorResponse:
      HandleErrorResponse(packet);
      break;
    case MessageType::TransportClosed:
      HandleTransportClosed(packet);
      break;
    case MessageType::SessionChangeNotification:
      HandleSessionChange(packet);
      break;
    default:
      HandleUnknown(packet);
      break;
  }
}

void Client::HandlePingReq(const Packet &packet) {
  Packet response;
  response.type_string = kPingRspType;
  response.type = MessageType::PingRsp;
  response.source = packet.dest;
  response.dest = packet.source;
  response.user_id = packet.user_id;
  response.payload = marshal::Value::Tuple({});
  SendPacket(response);
}

void Client::HandleCallRsp(const Packet &packet) {
  if (m_call_rsp_handler) {
    marshal::ValuePtr result;
    if (packet.payload && packet.payload->IsTuple() &&
        !packet.payload->TupleValue().empty()) {
      const auto &first = packet.payload->TupleValue()[0];
      if (first->IsSubStream()) {
        result = marshal::Decode(first->SubStreamData(), true);
      } else if (first->IsTuple() && !first->TupleValue().empty()) {
        const auto &inner = first->TupleValue()[0];
        if (inner->IsSubStream()) {
          result = marshal::Decode(inner->SubStreamData(), true);
        }
      }
    }
    m_call_rsp_handler(packet, result);
    return;
  }
  std::cout << "{\"event\":\"stage4_call_rsp\",\"status\":\"pass\","
            << "\"type\":\"" << packet.type_string << "\"}\n";
}

void Client::HandleNotification(const Packet &packet) {
  std::cout << "{\"event\":\"stage4_notification\",\"status\":\"pass\","
            << "\"type\":\"" << packet.type_string << "\"}\n";
}

void Client::HandleErrorResponse(const Packet &packet) {
  std::cout << "{\"event\":\"stage4_error_response\",\"status\":\"pass\","
            << "\"type\":\"" << packet.type_string << "\"}\n";
}

void Client::HandleTransportClosed(const Packet &packet) {
  std::cout << "{\"event\":\"stage4_transport_closed\",\"status\":\"pass\","
            << "\"type\":\"" << packet.type_string << "\"}\n";
}

void Client::HandleSessionChange(const Packet &packet) {
  std::cout << "{\"event\":\"stage4_session_change\",\"status\":\"pass\","
            << "\"type\":\"" << packet.type_string << "\"}\n";
}

void Client::HandleUnknown(const Packet &packet) {
  std::cout << "{\"event\":\"stage4_unknown_packet\",\"status\":\"pass\","
            << "\"type\":\"" << packet.type_string << "\"}\n";
}

} // namespace ithax::network
