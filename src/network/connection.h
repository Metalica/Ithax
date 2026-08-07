#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "network/framing.h"
#include "network/marshal/marshal.h"
#include "network/session_crypto.h"

namespace ithax::network {

constexpr std::uint16_t DEFAULT_SERVER_PORT = 26000U;
constexpr std::uint32_t CONNECT_TIMEOUT_MS = 5'000U;
constexpr std::uint32_t RECEIVE_TIMEOUT_MS = 30'000U;

class ConnectionError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class ConnectionLoopbackError : public ConnectionError {
public:
  using ConnectionError::ConnectionError;
};

class ConnectionClosedError : public ConnectionError {
public:
  using ConnectionError::ConnectionError;
};

class ConnectionTimeoutError : public ConnectionError {
public:
  using ConnectionError::ConnectionError;
};

class Connection {
public:
  Connection();
  ~Connection() noexcept;

  Connection(const Connection &) = delete;
  Connection &operator=(const Connection &) = delete;
  Connection(Connection &&) = delete;
  Connection &operator=(Connection &&) = delete;

  void Connect(const std::string &host, std::uint16_t port);
  void Disconnect() noexcept;
  bool IsConnected() const noexcept;
  std::uint64_t Generation() const noexcept;

  void SendMarshal(const marshal::Value &value);
  marshal::ValuePtr ReceiveMarshal();

  void SetSessionCrypto(std::unique_ptr<SessionCrypto> crypto) noexcept;
  bool HasSessionCrypto() const noexcept;

private:
  static void ValidateLoopbackHost(const std::string &host);

  std::uint64_t m_generation = 0U;
  bool m_connected = false;
  std::unique_ptr<SessionCrypto> m_crypto;
  FrameDecoder m_decoder;
  void *m_socket = nullptr;
};

} // namespace ithax::network
