#include "network/dispatcher.h"

#include <utility>

namespace ithax::network {

void Dispatcher::Register(const std::string &service,
                          const std::string &method, Handler handler) {
  m_handlers[service][method] = std::move(handler);
}

void Dispatcher::RegisterService(
    const std::string &service,
    const std::map<std::string, Handler> &methods) {
  for (const auto &[method, handler] : methods) {
    Register(service, method, handler);
  }
}

marshal::ValuePtr Dispatcher::Dispatch(const Packet &packet) const {
  if (packet.type != MessageType::CallReq) {
    throw DispatcherError("dispatcher only handles call requests");
  }
  const CallRequest call = CallRequest::Decode(packet);
  const auto service_it = m_handlers.find(call.service);
  if (service_it == m_handlers.end()) {
    throw DispatcherUnknownMethodError("unknown service: " + call.service);
  }
  const auto method_it = service_it->second.find(call.method);
  if (method_it == service_it->second.end()) {
    throw DispatcherUnknownMethodError("unknown method: " + call.service +
                                       "." + call.method);
  }
  return method_it->second(call, packet);
}

bool Dispatcher::HasHandler(const std::string &service,
                            const std::string &method) const noexcept {
  const auto service_it = m_handlers.find(service);
  if (service_it == m_handlers.end()) {
    return false;
  }
  return service_it->second.find(method) != service_it->second.end();
}

} // namespace ithax::network
