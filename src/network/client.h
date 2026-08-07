#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "network/connection.h"
#include "network/dispatcher.h"
#include "network/handshake.h"
#include "network/marshal/marshal.h"
#include "network/packet.h"
#include "network/session_crypto.h"

namespace ithax::network {

constexpr std::uint32_t DEFAULT_HEARTBEAT_INTERVAL_MS = 30'000U;
constexpr std::uint32_t DEFAULT_RECONNECT_DELAY_MS = 1'000U;
constexpr std::uint32_t MAX_RECONNECT_ATTEMPTS = 3U;

class ClientError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class ClientReconnectError : public ClientError {
public:
  using ClientError::ClientError;
};

class Client {
public:
  using CallRspHandler = std::function<void(
      const Packet &, const marshal::ValuePtr &)>;

  Client(std::string host, std::uint16_t port);
  ~Client() noexcept;

  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;
  Client(Client &&) = delete;
  Client &operator=(Client &&) = delete;

  void Connect();
  void Disconnect() noexcept;
  bool IsConnected() const noexcept;
  std::uint64_t Generation() const noexcept;

  void SetHeartbeatInterval(std::uint32_t interval_ms) noexcept;
  void SetReconnectPolicy(std::uint32_t max_attempts,
                          std::uint32_t delay_ms) noexcept;
  void SetCallRspHandler(CallRspHandler handler) noexcept;

  void SendCall(const std::string &service, const std::string &method,
                std::vector<marshal::ValuePtr> args,
                marshal::ValuePtr kwargs = nullptr);
  void SendPing();
  void SendNotification(const std::string &type,
                        std::vector<marshal::ValuePtr> args);

  marshal::ValuePtr Receive();
  void PumpOnce();

  Dispatcher &GetDispatcher() noexcept { return m_dispatcher; }
  const Dispatcher &GetDispatcher() const noexcept { return m_dispatcher; }

private:
  void EstablishSession();
  void SendPacket(const Packet &packet);
  void HandleIncoming(const marshal::Value &value);
  void HandlePingReq(const Packet &packet);
  void HandleCallRsp(const Packet &packet);
  void HandleNotification(const Packet &packet);
  void HandleErrorResponse(const Packet &packet);
  void HandleTransportClosed(const Packet &packet);
  void HandleSessionChange(const Packet &packet);
  void HandleUnknown(const Packet &packet);

  std::string m_host;
  std::uint16_t m_port;
  std::unique_ptr<Connection> m_connection;
  Handshake m_handshake;
  Dispatcher m_dispatcher;
  std::uint32_t m_heartbeat_interval_ms = DEFAULT_HEARTBEAT_INTERVAL_MS;
  std::uint32_t m_reconnect_attempts = MAX_RECONNECT_ATTEMPTS;
  std::uint32_t m_reconnect_delay_ms = DEFAULT_RECONNECT_DELAY_MS;
  std::chrono::steady_clock::time_point m_last_heartbeat;
  CallRspHandler m_call_rsp_handler;
  bool m_connected = false;
};

} // namespace ithax::network
