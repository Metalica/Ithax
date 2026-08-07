#include "threading/frame_slot.h"

#include <atomic>
#include <exception>
#include <limits>

namespace ithax::threading {

namespace {

void TerminateOnLeaseError() noexcept { std::terminate(); }

std::uint64_t AllocatePoolId() {
  static std::atomic<std::uint64_t> next_id = 1U;
  auto current_id = next_id.load(std::memory_order_relaxed);
  for (;;) {
    if (current_id == 0U ||
        current_id == std::numeric_limits<std::uint64_t>::max()) {
      throw FrameSlotGenerationError("frame slot pool ID was exhausted");
    }
    if (next_id.compare_exchange_weak(current_id, current_id + 1U,
                                      std::memory_order_relaxed,
                                      std::memory_order_relaxed)) {
      return current_id;
    }
  }
}

} // namespace

FrameWriteLease::FrameWriteLease(FrameSlotPool *pool,
                                 const FrameSlotToken token) noexcept
    : m_pool(pool), m_token(token), m_active(true) {}

FrameWriteLease::~FrameWriteLease() noexcept { Cancel(); }

FrameWriteLease::FrameWriteLease(FrameWriteLease &&other) noexcept
    : m_pool(other.m_pool), m_token(other.m_token), m_active(other.m_active) {
  other.m_pool = nullptr;
  other.m_active = false;
}

FrameWriteLease &FrameWriteLease::operator=(FrameWriteLease &&other) noexcept {
  if (this != &other) {
    Cancel();
    m_pool = other.m_pool;
    m_token = other.m_token;
    m_active = other.m_active;
    other.m_pool = nullptr;
    other.m_active = false;
  }
  return *this;
}

FrameSlotToken FrameWriteLease::Token() const noexcept { return m_token; }

std::span<std::byte> FrameWriteLease::Payload() {
  if (!m_active || m_pool == nullptr) {
    throw FrameSlotStateError("write lease is not active");
  }
  return m_pool->WritePayload(m_token);
}

std::size_t FrameWriteLease::PayloadSize() const {
  if (!m_active || m_pool == nullptr) {
    throw FrameSlotStateError("write lease is not active");
  }
  return m_pool->WritePayloadSize(m_token);
}

void FrameWriteLease::SetPayloadSize(const std::size_t size) {
  if (!m_active || m_pool == nullptr) {
    throw FrameSlotStateError("write lease is not active");
  }
  m_pool->SetWritePayloadSize(m_token, size);
}

void FrameWriteLease::Publish() {
  if (!m_active || m_pool == nullptr) {
    throw FrameSlotStateError("write lease is not active");
  }
  m_pool->PublishWrite(m_token);
  m_active = false;
}

void FrameWriteLease::Cancel() noexcept {
  if (!m_active || m_pool == nullptr) {
    return;
  }
  try {
    m_pool->CancelWrite(m_token);
    m_active = false;
  } catch (...) {
    TerminateOnLeaseError();
  }
}

FrameReadLease::FrameReadLease(FrameSlotPool *pool,
                               const FrameSlotToken token) noexcept
    : m_pool(pool), m_token(token), m_active(true) {}

FrameReadLease::~FrameReadLease() noexcept { Release(); }

FrameReadLease::FrameReadLease(FrameReadLease &&other) noexcept
    : m_pool(other.m_pool), m_token(other.m_token), m_active(other.m_active) {
  other.m_pool = nullptr;
  other.m_active = false;
}

FrameReadLease &FrameReadLease::operator=(FrameReadLease &&other) noexcept {
  if (this != &other) {
    Release();
    m_pool = other.m_pool;
    m_token = other.m_token;
    m_active = other.m_active;
    other.m_pool = nullptr;
    other.m_active = false;
  }
  return *this;
}

FrameSlotToken FrameReadLease::Token() const noexcept { return m_token; }

std::span<const std::byte> FrameReadLease::Payload() const {
  if (!m_active || m_pool == nullptr) {
    throw FrameSlotStateError("read lease is not active");
  }
  return m_pool->ReadPayload(m_token);
}

std::size_t FrameReadLease::PayloadSize() const {
  if (!m_active || m_pool == nullptr) {
    throw FrameSlotStateError("read lease is not active");
  }
  return m_pool->ReadPayloadSize(m_token);
}

void FrameReadLease::Release() noexcept {
  if (!m_active || m_pool == nullptr) {
    return;
  }
  try {
    m_pool->ReleaseRead(m_token);
    m_active = false;
  } catch (...) {
    TerminateOnLeaseError();
  }
}

FrameSlotPool::FrameSlotPool(const std::size_t slot_count,
                             const std::size_t payload_capacity) {
  if (slot_count == 0U || slot_count > MAX_FRAME_SLOT_COUNT ||
      payload_capacity == 0U ||
      payload_capacity > MAX_FRAME_SLOT_PAYLOAD_BYTES) {
    throw FrameSlotCapacityError("frame slot dimensions are outside bounds");
  }
  if (slot_count > MAX_FRAME_SLOT_STORAGE_BYTES / payload_capacity) {
    throw FrameSlotCapacityError("frame slot storage exceeds its bound");
  }

  m_pool_id = AllocatePoolId();
  m_payload_capacity = payload_capacity;
  m_slots.resize(slot_count);
  for (auto &slot : m_slots) {
    slot.payload.resize(payload_capacity);
  }
}

FrameSlotPool::~FrameSlotPool() noexcept {
  Close();
  std::unique_lock lock(m_mutex);
  m_operations_idle.wait(lock, [this]() { return m_active_operations == 0U; });
  const bool quiescent = [&]() {
    for (const auto &slot : m_slots) {
      if (slot.state != FrameSlotState::Free) {
        return false;
      }
    }
    return true;
  }();
  if (!quiescent) {
    TerminateOnLeaseError();
  }
}

std::optional<FrameWriteLease> FrameSlotPool::TryAcquireWrite() {
  std::lock_guard lock(m_mutex);
  RegisterProducerLocked();
  if (m_closed) {
    return std::nullopt;
  }
  const auto slot_id = FindFreeSlotLocked();
  if (slot_id == m_slots.size()) {
    return std::nullopt;
  }
  auto &slot = m_slots[slot_id];
  if (slot.generation == std::numeric_limits<std::uint64_t>::max()) {
    throw FrameSlotGenerationError("frame slot generation was exhausted");
  }
  ++slot.generation;
  slot.payload_size = 0U;
  slot.was_cancelled = false;
  slot.state = FrameSlotState::Writing;
  return FrameWriteLease(
      this, {static_cast<std::uint32_t>(slot_id), slot.generation, m_pool_id});
}

std::optional<FrameWriteLease> FrameSlotPool::AcquireWrite() {
  std::unique_lock lock(m_mutex);
  ++m_active_operations;
  bool operation_active = true;
  const auto finish = [&]() {
    if (operation_active) {
      EndOperationLocked();
      operation_active = false;
    }
  };
  try {
    RegisterProducerLocked();
    m_state_changed.wait(lock, [this]() {
      return m_closed || FindFreeSlotLocked() != m_slots.size();
    });
    if (m_closed) {
      finish();
      return std::nullopt;
    }
    const auto slot_id = FindFreeSlotLocked();
    auto &slot = m_slots[slot_id];
    if (slot.generation == std::numeric_limits<std::uint64_t>::max()) {
      throw FrameSlotGenerationError("frame slot generation was exhausted");
    }
    ++slot.generation;
    slot.payload_size = 0U;
    slot.was_cancelled = false;
    slot.state = FrameSlotState::Writing;
    const FrameSlotToken token{static_cast<std::uint32_t>(slot_id),
                               slot.generation, m_pool_id};
    finish();
    return FrameWriteLease(this, token);
  } catch (...) {
    finish();
    throw;
  }
}

std::optional<FrameReadLease>
FrameSlotPool::TryAcquireRead(const FrameSlotToken token) {
  return AcquireReadImpl(token, false);
}

std::optional<FrameReadLease>
FrameSlotPool::AcquireRead(const FrameSlotToken token) {
  return AcquireReadImpl(token, true);
}

void FrameSlotPool::Close() noexcept {
  {
    std::lock_guard lock(m_mutex);
    m_closed = true;
  }
  m_state_changed.notify_all();
}

void FrameSlotPool::DiscardPublished(const FrameSlotToken token) {
  std::lock_guard lock(m_mutex);
  auto &slot = SlotForTokenLocked(token);
  RegisterConsumerLocked();
  if (!m_closed || slot.state != FrameSlotState::Published) {
    throw FrameSlotStateError("published frame slot cannot be discarded");
  }
  slot.payload_size = 0U;
  slot.was_cancelled = false;
  slot.state = FrameSlotState::Free;
  m_state_changed.notify_all();
}

bool FrameSlotPool::IsClosed() const {
  std::lock_guard lock(m_mutex);
  return m_closed;
}

bool FrameSlotPool::IsQuiescent() const {
  std::lock_guard lock(m_mutex);
  if (m_active_operations != 0U) {
    return false;
  }
  for (const auto &slot : m_slots) {
    if (slot.state != FrameSlotState::Free) {
      return false;
    }
  }
  return true;
}

std::size_t FrameSlotPool::SlotCount() const noexcept { return m_slots.size(); }

std::size_t FrameSlotPool::PayloadCapacity() const noexcept {
  return m_payload_capacity;
}

std::optional<FrameReadLease>
FrameSlotPool::AcquireReadImpl(const FrameSlotToken token,
                               const bool blocking) {
  std::unique_lock lock(m_mutex);
  ++m_active_operations;
  bool operation_active = true;
  const auto finish = [&]() {
    if (operation_active) {
      EndOperationLocked();
      operation_active = false;
    }
  };
  try {
    static_cast<void>(SlotForTokenLocked(token));
    RegisterConsumerLocked();
    for (;;) {
      auto &slot = SlotForTokenLocked(token);
      if (slot.state == FrameSlotState::Published) {
        slot.state = FrameSlotState::Reading;
        finish();
        return FrameReadLease(this, token);
      }
      if (slot.state == FrameSlotState::Writing) {
        if (m_closed || !blocking) {
          finish();
          return std::nullopt;
        }
        m_state_changed.wait(lock);
        continue;
      }
      if (slot.was_cancelled || m_closed) {
        finish();
        return std::nullopt;
      }
      throw FrameSlotStateError("frame slot is not published");
    }
  } catch (...) {
    finish();
    throw;
  }
}

std::size_t FrameSlotPool::FindFreeSlotLocked() const noexcept {
  for (std::size_t index = 0U; index < m_slots.size(); ++index) {
    if (m_slots[index].state == FrameSlotState::Free) {
      return index;
    }
  }
  return m_slots.size();
}

void FrameSlotPool::EndOperationLocked() noexcept {
  if (m_active_operations == 0U) {
    TerminateOnLeaseError();
  }
  --m_active_operations;
  if (m_active_operations == 0U) {
    m_operations_idle.notify_all();
  }
}

void FrameSlotPool::RegisterProducerLocked() {
  const auto current_thread = std::this_thread::get_id();
  if (!m_has_producer) {
    m_has_producer = true;
    m_producer_thread = current_thread;
    return;
  }
  if (m_producer_thread != current_thread) {
    throw FrameSlotOwnershipError("frame slot pool has multiple producers");
  }
}

void FrameSlotPool::RegisterConsumerLocked() {
  const auto current_thread = std::this_thread::get_id();
  if (!m_has_consumer) {
    m_has_consumer = true;
    m_consumer_thread = current_thread;
    return;
  }
  if (m_consumer_thread != current_thread) {
    throw FrameSlotOwnershipError("frame slot pool has multiple consumers");
  }
}

void FrameSlotPool::AssertProducerLocked() const {
  if (!m_has_producer || m_producer_thread != std::this_thread::get_id()) {
    throw FrameSlotOwnershipError("frame slot write owner mismatch");
  }
}

void FrameSlotPool::AssertConsumerLocked() const {
  if (!m_has_consumer || m_consumer_thread != std::this_thread::get_id()) {
    throw FrameSlotOwnershipError("frame slot read owner mismatch");
  }
}

FrameSlotPool::SlotStorage &
FrameSlotPool::SlotForTokenLocked(const FrameSlotToken token) {
  if (token.pool_id != m_pool_id || token.generation == 0U ||
      token.slot_id >= m_slots.size()) {
    throw FrameSlotGenerationError("frame slot token is invalid");
  }
  auto &slot = m_slots[token.slot_id];
  if (slot.generation != token.generation) {
    throw FrameSlotGenerationError("frame slot token is stale");
  }
  return slot;
}

const FrameSlotPool::SlotStorage &
FrameSlotPool::SlotForTokenLocked(const FrameSlotToken token) const {
  if (token.pool_id != m_pool_id || token.generation == 0U ||
      token.slot_id >= m_slots.size()) {
    throw FrameSlotGenerationError("frame slot token is invalid");
  }
  const auto &slot = m_slots[token.slot_id];
  if (slot.generation != token.generation) {
    throw FrameSlotGenerationError("frame slot token is stale");
  }
  return slot;
}

std::span<std::byte> FrameSlotPool::WritePayload(const FrameSlotToken token) {
  std::lock_guard lock(m_mutex);
  AssertProducerLocked();
  auto &slot = SlotForTokenLocked(token);
  if (slot.state != FrameSlotState::Writing) {
    throw FrameSlotStateError("frame slot is not writable");
  }
  return {slot.payload.data(), slot.payload.size()};
}

std::size_t FrameSlotPool::WritePayloadSize(const FrameSlotToken token) const {
  std::lock_guard lock(m_mutex);
  AssertProducerLocked();
  const auto &slot = SlotForTokenLocked(token);
  if (slot.state != FrameSlotState::Writing) {
    throw FrameSlotStateError("frame slot is not writable");
  }
  return slot.payload_size;
}

void FrameSlotPool::SetWritePayloadSize(const FrameSlotToken token,
                                        const std::size_t size) {
  std::lock_guard lock(m_mutex);
  AssertProducerLocked();
  auto &slot = SlotForTokenLocked(token);
  if (slot.state != FrameSlotState::Writing) {
    throw FrameSlotStateError("frame slot is not writable");
  }
  if (size > m_payload_capacity) {
    throw FrameSlotCapacityError("frame slot payload exceeds its capacity");
  }
  slot.payload_size = size;
}

void FrameSlotPool::PublishWrite(const FrameSlotToken token) {
  std::lock_guard lock(m_mutex);
  AssertProducerLocked();
  auto &slot = SlotForTokenLocked(token);
  if (slot.state != FrameSlotState::Writing) {
    throw FrameSlotStateError("frame slot is not writable");
  }
  if (m_closed) {
    throw FrameSlotStateError("closed frame slot cannot be published");
  }
  slot.state = FrameSlotState::Published;
  m_state_changed.notify_all();
}

void FrameSlotPool::CancelWrite(const FrameSlotToken token) {
  std::lock_guard lock(m_mutex);
  AssertProducerLocked();
  auto &slot = SlotForTokenLocked(token);
  if (slot.state != FrameSlotState::Writing) {
    throw FrameSlotStateError("frame slot is not cancellable");
  }
  slot.payload_size = 0U;
  slot.was_cancelled = true;
  slot.state = FrameSlotState::Free;
  m_state_changed.notify_all();
}

std::span<const std::byte>
FrameSlotPool::ReadPayload(const FrameSlotToken token) const {
  std::lock_guard lock(m_mutex);
  AssertConsumerLocked();
  const auto &slot = SlotForTokenLocked(token);
  if (slot.state != FrameSlotState::Reading) {
    throw FrameSlotStateError("frame slot is not readable");
  }
  return {slot.payload.data(), slot.payload_size};
}

std::size_t FrameSlotPool::ReadPayloadSize(const FrameSlotToken token) const {
  std::lock_guard lock(m_mutex);
  AssertConsumerLocked();
  const auto &slot = SlotForTokenLocked(token);
  if (slot.state != FrameSlotState::Reading) {
    throw FrameSlotStateError("frame slot is not readable");
  }
  return slot.payload_size;
}

void FrameSlotPool::ReleaseRead(const FrameSlotToken token) {
  std::lock_guard lock(m_mutex);
  AssertConsumerLocked();
  auto &slot = SlotForTokenLocked(token);
  if (slot.state != FrameSlotState::Reading) {
    throw FrameSlotStateError("frame slot is not releasable");
  }
  slot.payload_size = 0U;
  slot.was_cancelled = false;
  slot.state = FrameSlotState::Free;
  m_state_changed.notify_all();
}

} // namespace ithax::threading
