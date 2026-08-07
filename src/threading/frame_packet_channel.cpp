#include "threading/frame_packet_channel.h"

namespace {

std::size_t ValidateCapacity(const std::size_t capacity) {
  if (capacity == 0U ||
      capacity > ithax::threading::MAX_FRAME_PACKET_CAPACITY) {
    throw ithax::threading::FramePacketChannelError(
        "frame channel capacity is outside its bound");
  }
  return capacity;
}

} // namespace

namespace ithax::threading {

FramePacketChannel::FramePacketChannel(const std::size_t capacity)
    : m_buffer(ValidateCapacity(capacity)) {}

FramePacketChannel::~FramePacketChannel() noexcept { Close(); }

void FramePacketChannel::RegisterProducer() {
  const auto current_thread = std::this_thread::get_id();
  if (!m_has_producer) {
    m_producer_thread = current_thread;
    m_has_producer = true;
    return;
  }
  if (m_producer_thread != current_thread) {
    throw FramePacketChannelError("frame channel has multiple producers");
  }
}

void FramePacketChannel::RegisterConsumer() {
  const auto current_thread = std::this_thread::get_id();
  if (!m_has_consumer) {
    m_consumer_thread = current_thread;
    m_has_consumer = true;
    return;
  }
  if (m_consumer_thread != current_thread) {
    throw FramePacketChannelError("frame channel has multiple consumers");
  }
}

bool FramePacketChannel::TryPublish(FramePacket packet) {
  std::unique_lock lock(m_mutex);
  RegisterProducer();
  if (m_closed || m_size == m_buffer.size()) {
    return false;
  }

  m_buffer[m_tail] = packet;
  m_tail = (m_tail + 1U) % m_buffer.size();
  ++m_size;
  lock.unlock();
  m_not_empty.notify_one();
  return true;
}

void FramePacketChannel::Publish(FramePacket packet) {
  std::unique_lock lock(m_mutex);
  RegisterProducer();
  m_not_full.wait(lock, [this]() {
    return m_closed || m_size < m_buffer.size();
  });
  if (m_closed) {
    throw FramePacketChannelError("publish rejected after channel close");
  }

  m_buffer[m_tail] = packet;
  m_tail = (m_tail + 1U) % m_buffer.size();
  ++m_size;
  lock.unlock();
  m_not_empty.notify_one();
}

std::optional<FramePacket> FramePacketChannel::TryConsume() {
  std::unique_lock lock(m_mutex);
  RegisterConsumer();
  if (m_size == 0U) {
    return std::nullopt;
  }

  FramePacket packet = m_buffer[m_head];
  m_head = (m_head + 1U) % m_buffer.size();
  --m_size;
  lock.unlock();
  m_not_full.notify_one();
  return packet;
}

std::optional<FramePacket> FramePacketChannel::Consume() {
  std::unique_lock lock(m_mutex);
  RegisterConsumer();
  m_not_empty.wait(lock, [this]() {
    return m_closed || m_size > 0U;
  });
  if (m_size == 0U) {
    return std::nullopt;
  }

  FramePacket packet = m_buffer[m_head];
  m_head = (m_head + 1U) % m_buffer.size();
  --m_size;
  lock.unlock();
  m_not_full.notify_one();
  return packet;
}

void FramePacketChannel::Close() noexcept {
  {
    std::lock_guard lock(m_mutex);
    m_closed = true;
  }
  m_not_empty.notify_all();
  m_not_full.notify_all();
}

bool FramePacketChannel::IsClosed() const {
  std::lock_guard lock(m_mutex);
  return m_closed;
}

std::size_t FramePacketChannel::Capacity() const noexcept {
  return m_buffer.size();
}

std::size_t FramePacketChannel::Size() const {
  std::lock_guard lock(m_mutex);
  return m_size;
}

} // namespace ithax::threading
