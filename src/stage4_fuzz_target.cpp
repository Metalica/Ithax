#include "network/marshal/marshal.h"
#include "network/packet.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using ithax::network::marshal::Decode;
using ithax::network::marshal::Encode;
using ithax::network::marshal::MarshalError;
using ithax::network::Packet;
using ithax::network::PacketError;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data,
                                      std::size_t size) {
  const std::vector<std::uint8_t> stream(data, data + size);
  try {
    const auto value = Decode(stream, true);
    if (value) {
      const std::vector<std::uint8_t> reencoded = Encode(*value);
      (void)reencoded;
      try {
        const Packet packet = Packet::Decode(*value, 1U);
        (void)packet;
      } catch (const PacketError &) {
      }
    }
  } catch (const MarshalError &) {
  }
  return 0;
}
