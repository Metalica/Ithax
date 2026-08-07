#include "threading/frame_packet_channel.h"
#include "threading/frame_slot.h"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {

constexpr std::uint64_t TSAN_TICK_COUNT = 10'000U;
constexpr std::size_t SLOT_COUNT = 2U;
constexpr std::size_t CHANNEL_CAPACITY = 2U;
constexpr std::size_t PAYLOAD_BYTES = sizeof(std::uint64_t);

class TSanCoreError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct SharedError {
  std::mutex mutex;
  std::exception_ptr error;
};

void RecordError(SharedError &state) noexcept {
  std::lock_guard lock(state.mutex);
  if (!state.error) {
    state.error = std::current_exception();
  }
}

void RethrowError(SharedError &state) {
  std::lock_guard lock(state.mutex);
  if (state.error) {
    std::rethrow_exception(state.error);
  }
}

void RunSpscStress() {
  ithax::threading::FrameSlotPool slots(SLOT_COUNT, PAYLOAD_BYTES);
  ithax::threading::FramePacketChannel channel(CHANNEL_CAPACITY);
  SharedError errors;
  std::uint64_t consumed = 0U;

  std::thread producer([&]() {
    try {
      for (std::uint64_t tick = 1U; tick <= TSAN_TICK_COUNT; ++tick) {
        auto writer = slots.AcquireWrite();
        if (!writer.has_value()) {
          throw TSanCoreError("TSan producer could not acquire a slot");
        }
        std::array<std::byte, PAYLOAD_BYTES> payload{};
        std::memcpy(payload.data(), &tick, sizeof(tick));
        std::memcpy(writer->Payload().data(), payload.data(), payload.size());
        writer->SetPayloadSize(payload.size());
        const auto token = writer->Token();
        channel.Publish({tick, tick ^ token.generation, token});
        writer->Publish();
      }
      slots.Close();
      channel.Close();
    } catch (...) {
      RecordError(errors);
      slots.Close();
      channel.Close();
    }
  });

  std::thread consumer([&]() {
    try {
      std::uint64_t expected_tick = 1U;
      for (;;) {
        const auto packet = channel.Consume();
        if (!packet.has_value()) {
          break;
        }
        auto reader = slots.AcquireRead(packet->slot.value());
        if (!reader.has_value()) {
          throw TSanCoreError("TSan consumer could not acquire a slot");
        }
        if (reader->PayloadSize() != PAYLOAD_BYTES ||
            packet->generation != expected_tick) {
          throw TSanCoreError("TSan frame metadata was not ordered");
        }
        std::uint64_t observed_tick = 0U;
        std::memcpy(&observed_tick, reader->Payload().data(),
                    sizeof(observed_tick));
        if (observed_tick != expected_tick ||
            packet->world_hash !=
                (expected_tick ^ reader->Token().generation)) {
          throw TSanCoreError("TSan frame payload was corrupted");
        }
        reader->Release();
        ++expected_tick;
      }
      if (expected_tick != TSAN_TICK_COUNT + 1U) {
        throw TSanCoreError("TSan consumer did not receive every frame");
      }
      consumed = expected_tick - 1U;
    } catch (...) {
      RecordError(errors);
      slots.Close();
      channel.Close();
    }
  });

  producer.join();
  consumer.join();
  RethrowError(errors);
  if (!channel.IsClosed() || channel.Size() != 0U || !slots.IsQuiescent() ||
      consumed != TSAN_TICK_COUNT) {
    throw TSanCoreError("TSan SPSC shutdown invariant failed");
  }
}

}  // namespace

int main() {
  try {
    RunSpscStress();
    std::cout << "{\"event\":\"stage3_tsan_summary\","
              << "\"status\":\"pass\",\"ticks\":"
              << TSAN_TICK_COUNT << "}\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Stage 3 TSan core test failed: " << error.what() << '\n';
    return 1;
  }
}
