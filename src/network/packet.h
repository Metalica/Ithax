#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "network/marshal/marshal.h"

namespace ithax::network {

enum class MessageType : std::int64_t {
  AuthenticationReq = 0,
  AuthenticationRsp = 1,
  IdentificationReq = 2,
  IdentificationRsp = 3,
  CallReq = 6,
  CallRsp = 7,
  TransportClosed = 8,
  ResolveReq = 10,
  ResolveRsp = 11,
  Notification = 12,
  ErrorResponse = 15,
  SessionChangeNotification = 16,
  SessionInitialStateNotification = 18,
  PingReq = 20,
  PingRsp = 21,
};

const char *MessageTypeName(MessageType type) noexcept;

enum class AddressType : std::int64_t {
  Node = 1,
  Client = 2,
  Broadcast = 4,
  Any = 8,
  Invalid = 0,
};

struct Address {
  AddressType type = AddressType::Invalid;
  std::int64_t object_id = 0;
  std::int64_t call_id = 0;
  std::string service;
  std::string broadcast_idtype;

  marshal::ValuePtr Encode() const;
  static Address Decode(const marshal::Value &value);
};

class PacketError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class PacketFormatError : public PacketError {
public:
  using PacketError::PacketError;
};

struct Packet {
  std::string type_string;
  MessageType type = MessageType::CallReq;
  Address source;
  Address dest;
  std::int64_t user_id = 0;
  marshal::ValuePtr payload;
  marshal::ValuePtr named_payload;
  std::uint64_t generation = 0U;

  marshal::ValuePtr Encode() const;
  static Packet Decode(const marshal::Value &value, std::uint64_t generation);
};

struct CallRequest {
  std::string service;
  std::string method;
  std::vector<marshal::ValuePtr> args;
  marshal::ValuePtr kwargs;
  std::int64_t call_id = 0;

  static CallRequest Decode(const Packet &packet);
};

} // namespace ithax::network
