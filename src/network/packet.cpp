#include "network/packet.h"

#include <utility>

namespace ithax::network {

namespace {

constexpr std::int64_t kMinPacketTupleSize = 7;
constexpr std::int64_t kMaxPacketTupleSize = 14;
constexpr const char *kAddressTypeName = "macho.MachoAddress";
constexpr const char *kModernAddressTypeName =
    "carbon.common.script.net.machoNetPacket.MachoAddress";

const marshal::Value &RequireTuple(const marshal::Value &value,
                                   std::size_t min_size) {
  if (!value.IsTuple() || value.TupleValue().size() < min_size) {
    throw PacketFormatError("packet body is not a tuple");
  }
  return value;
}

std::int64_t RequireInt(const marshal::Value &value) {
  if (!value.IsInt()) {
    throw PacketFormatError("expected an int");
  }
  return value.IntValue();
}

std::string RequireString(const marshal::Value &value) {
  if (!value.IsString()) {
    throw PacketFormatError("expected a string");
  }
  return value.StringValue();
}

} // namespace

const char *MessageTypeName(MessageType type) noexcept {
  switch (type) {
    case MessageType::AuthenticationReq:
      return "AUTHENTICATION_REQ";
    case MessageType::AuthenticationRsp:
      return "AUTHENTICATION_RSP";
    case MessageType::IdentificationReq:
      return "IDENTIFICATION_REQ";
    case MessageType::IdentificationRsp:
      return "IDENTIFICATION_RSP";
    case MessageType::CallReq:
      return "CALL_REQ";
    case MessageType::CallRsp:
      return "CALL_RSP";
    case MessageType::TransportClosed:
      return "TRANSPORTCLOSED";
    case MessageType::ResolveReq:
      return "RESOLVE_REQ";
    case MessageType::ResolveRsp:
      return "RESOLVE_RSP";
    case MessageType::Notification:
      return "NOTIFICATION";
    case MessageType::ErrorResponse:
      return "ERRORRESPONSE";
    case MessageType::SessionChangeNotification:
      return "SESSIONCHANGENOTIFICATION";
    case MessageType::SessionInitialStateNotification:
      return "SESSIONINITIALSTATENOTIFICATION";
    case MessageType::PingReq:
      return "PING_REQ";
    case MessageType::PingRsp:
      return "PING_RSP";
  }
  return "UNKNOWN";
}

marshal::ValuePtr Address::Encode() const {
  std::vector<marshal::ValuePtr> items;
  items.push_back(marshal::Value::Int(static_cast<std::int64_t>(type)));
  switch (type) {
    case AddressType::Any:
      items.push_back(service.empty() ? marshal::Value::None()
                                      : marshal::Value::String(service));
      items.push_back(object_id == 0 ? marshal::Value::None()
                                     : marshal::Value::Int(object_id));
      break;
    case AddressType::Node:
      items.push_back(marshal::Value::Int(object_id));
      items.push_back(service.empty() ? marshal::Value::None()
                                      : marshal::Value::String(service));
      items.push_back(call_id == 0 ? marshal::Value::None()
                                   : marshal::Value::Int(call_id));
      break;
    case AddressType::Client:
      items.push_back(marshal::Value::Int(object_id));
      items.push_back(call_id == 0 ? marshal::Value::None()
                                   : marshal::Value::Int(call_id));
      items.push_back(service.empty() ? marshal::Value::None()
                                      : marshal::Value::String(service));
      break;
    case AddressType::Broadcast:
      items.push_back(service.empty() ? marshal::Value::None()
                                      : marshal::Value::String(service));
      items.push_back(marshal::Value::List({}));
      items.push_back(marshal::Value::String(broadcast_idtype));
      break;
    case AddressType::Invalid:
      items.push_back(marshal::Value::None());
      break;
  }
  return marshal::Value::Object(kAddressTypeName,
                                marshal::Value::Tuple(std::move(items)));
}

Address Address::Decode(const marshal::Value &value) {
  if (!value.IsObject() ||
      (value.ObjectType() != kAddressTypeName &&
       value.ObjectType() != kModernAddressTypeName)) {
    throw PacketFormatError("address is not a macho address object");
  }
  const auto &args = value.ObjectArgs();
  if (!args->IsTuple() || args->TupleValue().size() < 3U) {
    throw PacketFormatError("address tuple is too short");
  }
  const auto &items = args->TupleValue();
  Address address;
  address.type = static_cast<AddressType>(RequireInt(*items[0]));
  switch (address.type) {
    case AddressType::Any: {
      if (items.size() != 3U) {
        throw PacketFormatError("any address tuple has the wrong size");
      }
      if (!items[1]->IsNone()) {
        address.service = RequireString(*items[1]);
      }
      if (!items[2]->IsNone()) {
        address.object_id = RequireInt(*items[2]);
      }
      break;
    }
    case AddressType::Node: {
      if (items.size() != 4U) {
        throw PacketFormatError("node address tuple has the wrong size");
      }
      address.object_id = RequireInt(*items[1]);
      if (!items[2]->IsNone()) {
        address.service = RequireString(*items[2]);
      }
      if (!items[3]->IsNone()) {
        address.call_id = RequireInt(*items[3]);
      }
      break;
    }
    case AddressType::Client: {
      if (items.size() != 4U) {
        throw PacketFormatError("client address tuple has the wrong size");
      }
      address.object_id = RequireInt(*items[1]);
      if (!items[2]->IsNone()) {
        address.call_id = RequireInt(*items[2]);
      }
      if (!items[3]->IsNone()) {
        address.service = RequireString(*items[3]);
      }
      break;
    }
    case AddressType::Broadcast: {
      if (items.size() != 4U) {
        throw PacketFormatError("broadcast address tuple has the wrong size");
      }
      if (!items[1]->IsNone()) {
        address.service = RequireString(*items[1]);
      }
      address.broadcast_idtype = RequireString(*items[3]);
      break;
    }
    case AddressType::Invalid:
      throw PacketFormatError("address type is invalid");
  }
  return address;
}

marshal::ValuePtr Packet::Encode() const {
  std::vector<marshal::ValuePtr> items;
  items.push_back(marshal::Value::Int(static_cast<std::int64_t>(type)));
  items.push_back(source.Encode());
  items.push_back(dest.Encode());
  items.push_back(user_id == 0 ? marshal::Value::None()
                               : marshal::Value::Int(user_id));
  items.push_back(payload ? payload : marshal::Value::Tuple({}));
  items.push_back(named_payload ? named_payload : marshal::Value::None());
  items.push_back(marshal::Value::None());
  return marshal::Value::Object(type_string,
                                marshal::Value::Tuple(std::move(items)));
}

Packet Packet::Decode(const marshal::Value &value, std::uint64_t generation) {
  if (!value.IsObject()) {
    throw PacketFormatError("packet is not an object");
  }
  const auto &tuple = RequireTuple(*value.ObjectArgs(), kMinPacketTupleSize);
  if (tuple.TupleValue().size() > kMaxPacketTupleSize) {
    throw PacketFormatError("packet tuple is too large");
  }
  const auto &items = tuple.TupleValue();

  Packet packet;
  packet.type_string = value.ObjectType();
  packet.type = static_cast<MessageType>(RequireInt(*items[0]));
  packet.source = Address::Decode(*items[1]);
  packet.dest = Address::Decode(*items[2]);
  if (!items[3]->IsNone()) {
    packet.user_id = RequireInt(*items[3]);
  }
  packet.payload = items[4];
  if (!items[5]->IsNone()) {
    packet.named_payload = items[5];
  }
  packet.generation = generation;
  return packet;
}

CallRequest CallRequest::Decode(const Packet &packet) {
  if (packet.type != MessageType::CallReq) {
    throw PacketFormatError("packet is not a call request");
  }
  if (!packet.payload->IsTuple() || packet.payload->TupleValue().empty()) {
    throw PacketFormatError("call request payload is empty");
  }
  const auto &wrapper = packet.payload->TupleValue()[0];
  if (!wrapper->IsTuple() || wrapper->TupleValue().size() < 2U) {
    throw PacketFormatError("call request wrapper is malformed");
  }
  const auto &wrapper_items = wrapper->TupleValue();
  marshal::ValuePtr decoded_body;
  for (const auto &item : wrapper_items) {
    if (item->IsSubStream()) {
      decoded_body = marshal::Decode(item->SubStreamData(), true);
      break;
    }
  }
  if (!decoded_body) {
    throw PacketFormatError("call request has no substream");
  }
  const marshal::Value &body = *decoded_body;
  if (!body.IsTuple() || body.TupleValue().size() < 2U) {
    throw PacketFormatError("call request body is malformed");
  }
  const auto &body_items = body.TupleValue();
  CallRequest call;
  if (body_items[0]->IsString()) {
    call.service = body_items[0]->StringValue();
  } else if (body_items[0]->IsInt()) {
    call.service = std::to_string(body_items[0]->IntValue());
  }
  call.method = RequireString(*body_items[1]);
  if (body_items.size() > 2U && body_items[2]->IsTuple()) {
    call.args = body_items[2]->TupleValue();
  }
  if (body_items.size() > 3U && !body_items[3]->IsNone()) {
    call.kwargs = body_items[3];
  }
  call.call_id = packet.source.call_id;
  return call;
}

} // namespace ithax::network
