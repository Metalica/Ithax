#include "network/connection.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>
#include <memory>

#pragma comment(lib, "ws2_32.lib")

namespace ithax::network {

namespace {

constexpr std::size_t kRecvChunkBytes = 64U * 1024U;

class WsaGuard {
public:
  WsaGuard() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      throw ConnectionError("WSAStartup failed");
    }
  }
  ~WsaGuard() noexcept { WSACleanup(); }
};

bool IsLoopbackAddress(const std::string &host) {
  if (host == "127.0.0.1" || host == "localhost" || host == "::1") {
    return true;
  }
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *result = nullptr;
  if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0) {
    return false;
  }
  bool is_loopback = false;
  for (addrinfo *entry = result; entry != nullptr; entry = entry->ai_next) {
    if (entry->ai_family == AF_INET) {
      const auto *address =
          reinterpret_cast<const sockaddr_in *>(entry->ai_addr);
      const std::uint32_t addr = ntohl(address->sin_addr.s_addr);
      if ((addr >> 24U) == 127U) {
        is_loopback = true;
        break;
      }
    } else if (entry->ai_family == AF_INET6) {
      const auto *address =
          reinterpret_cast<const sockaddr_in6 *>(entry->ai_addr);
      const auto *bytes = address->sin6_addr.s6_addr;
      if (bytes[0] == 0U && bytes[1] == 0U && bytes[2] == 0U &&
          bytes[3] == 0U && bytes[4] == 0U && bytes[5] == 0U &&
          bytes[6] == 0U && bytes[7] == 0U && bytes[8] == 0U &&
          bytes[9] == 0U && bytes[10] == 0U && bytes[11] == 0U &&
          bytes[12] == 0U && bytes[13] == 0U && bytes[14] == 0U &&
          bytes[15] == 1U) {
        is_loopback = true;
        break;
      }
    }
  }
  freeaddrinfo(result);
  return is_loopback;
}

} // namespace

Connection::Connection() {
  static WsaGuard guard;
  (void)guard;
}

Connection::~Connection() noexcept { Disconnect(); }

void Connection::Connect(const std::string &host, std::uint16_t port) {
  ValidateLoopbackHost(host);
  if (m_connected) {
    throw ConnectionError("connection is already established");
  }

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *result = nullptr;
  const std::string port_text = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &result) != 0) {
    throw ConnectionError("address resolution failed");
  }

  SOCKET socket_handle = INVALID_SOCKET;
  for (addrinfo *entry = result; entry != nullptr; entry = entry->ai_next) {
    socket_handle = socket(entry->ai_family, entry->ai_socktype,
                           entry->ai_protocol);
    if (socket_handle == INVALID_SOCKET) {
      continue;
    }
    if (connect(socket_handle, entry->ai_addr,
                static_cast<int>(entry->ai_addrlen)) == 0) {
      break;
    }
    closesocket(socket_handle);
    socket_handle = INVALID_SOCKET;
  }
  freeaddrinfo(result);

  if (socket_handle == INVALID_SOCKET) {
    throw ConnectionError("connection attempt failed");
  }

  const DWORD receive_timeout = RECEIVE_TIMEOUT_MS;
  setsockopt(socket_handle, SOL_SOCKET, SO_RCVTIMEO,
             reinterpret_cast<const char *>(&receive_timeout),
             sizeof(receive_timeout));

  m_socket = reinterpret_cast<void *>(socket_handle);
  m_connected = true;
  ++m_generation;
  m_decoder = FrameDecoder();
  m_crypto.reset();
}

void Connection::Disconnect() noexcept {
  if (m_socket != nullptr) {
    closesocket(reinterpret_cast<SOCKET>(m_socket));
    m_socket = nullptr;
  }
  m_connected = false;
  m_crypto.reset();
}

bool Connection::IsConnected() const noexcept { return m_connected; }

std::uint64_t Connection::Generation() const noexcept { return m_generation; }

void Connection::SendMarshal(const marshal::Value &value) {
  if (!m_connected || m_socket == nullptr) {
    throw ConnectionClosedError("connection is not established");
  }
  std::vector<std::uint8_t> payload = marshal::Encode(value);
  if (m_crypto) {
    payload = m_crypto->Encrypt(payload);
  }
  const std::vector<std::uint8_t> frame = EncodeFrame(payload);
  const SOCKET socket_handle = reinterpret_cast<SOCKET>(m_socket);
  std::size_t sent = 0U;
  while (sent < frame.size()) {
    const int written = send(
        socket_handle,
        reinterpret_cast<const char *>(frame.data() + sent),
        static_cast<int>(frame.size() - sent), 0);
    if (written <= 0) {
      throw ConnectionClosedError("send failed");
    }
    sent += static_cast<std::size_t>(written);
  }
}

marshal::ValuePtr Connection::ReceiveMarshal() {
  if (!m_connected || m_socket == nullptr) {
    throw ConnectionClosedError("connection is not established");
  }
  const SOCKET socket_handle = reinterpret_cast<SOCKET>(m_socket);
  for (;;) {
    auto frame = m_decoder.TryPopFrame();
    if (frame.has_value()) {
      std::vector<std::uint8_t> payload = std::move(frame.value());
      if (m_crypto) {
        payload = m_crypto->Decrypt(payload);
      }
      return marshal::Decode(payload, true);
    }
    std::vector<std::uint8_t> chunk(kRecvChunkBytes);
    const int received =
        recv(socket_handle, reinterpret_cast<char *>(chunk.data()),
             static_cast<int>(chunk.size()), 0);
    if (received == 0) {
      throw ConnectionClosedError("peer closed the connection");
    }
    if (received < 0) {
      const int error = WSAGetLastError();
      if (error == WSAETIMEDOUT) {
        throw ConnectionTimeoutError("receive timed out");
      }
      throw ConnectionClosedError("receive failed");
    }
    chunk.resize(static_cast<std::size_t>(received));
    m_decoder.Push(chunk);
  }
}

void Connection::SetSessionCrypto(
    std::unique_ptr<SessionCrypto> crypto) noexcept {
  m_crypto = std::move(crypto);
}

bool Connection::HasSessionCrypto() const noexcept {
  return m_crypto != nullptr;
}

void Connection::ValidateLoopbackHost(const std::string &host) {
  if (!IsLoopbackAddress(host)) {
    throw ConnectionLoopbackError(
        "the client network path is loopback-only by policy");
  }
}

} // namespace ithax::network
