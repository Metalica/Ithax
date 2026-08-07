#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "network/marshal/marshal.h"
#include "network/packet.h"

namespace ithax::network {

class DispatcherError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class DispatcherUnknownMethodError : public DispatcherError {
public:
  using DispatcherError::DispatcherError;
};

class Dispatcher {
public:
  using Handler = std::function<marshal::ValuePtr(
      const CallRequest &, const Packet &)>;

  void Register(const std::string &service, const std::string &method,
                Handler handler);
  void RegisterService(const std::string &service,
                       const std::map<std::string, Handler> &methods);

  marshal::ValuePtr Dispatch(const Packet &packet) const;

  bool HasHandler(const std::string &service,
                  const std::string &method) const noexcept;

private:
  std::map<std::string, std::map<std::string, Handler>> m_handlers;
};

} // namespace ithax::network
