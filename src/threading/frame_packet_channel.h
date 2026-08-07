#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

#include "threading/frame_slot.h"

namespace ithax::threading {

constexpr std::size_t MAX_FRAME_PACKET_CAPACITY = 1024U;

struct FramePacket {
  std::uint64_t generation = 0U;
  std::uint64_t world_hash = 0U;
  std::optional<FrameSlotToken> slot;
};

class FramePacketChannelError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class FramePacketChannel {
public:
  explicit FramePacketChannel(std::size_t capacity);
  ~FramePacketChannel() noexcept;

  FramePacketChannel(const FramePacketChannel &) = delete;
  FramePacketChannel &operator=(const FramePacketChannel &) = delete;
  FramePacketChannel(FramePacketChannel &&) = delete;
  FramePacketChannel &operator=(FramePacketChannel &&) = delete;

  bool TryPublish(FramePacket packet);
  void Publish(FramePacket packet);
  std::optional<FramePacket> TryConsume();
  std::optional<FramePacket> Consume();

  void Close() noexcept;
  bool IsClosed() const;
  std::size_t Capacity() const noexcept;
  std::size_t Size() const;

private:
  void RegisterProducer();
  void RegisterConsumer();

  mutable std::mutex m_mutex;
  std::condition_variable m_not_empty;
  std::condition_variable m_not_full;
  std::vector<FramePacket> m_buffer;
  std::size_t m_head = 0U;
  std::size_t m_tail = 0U;
  std::size_t m_size = 0U;
  bool m_closed = false;
  bool m_has_producer = false;
  bool m_has_consumer = false;
  std::thread::id m_producer_thread;
  std::thread::id m_consumer_thread;
};

} // namespace ithax::threading
