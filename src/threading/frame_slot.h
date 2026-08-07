#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace ithax::threading {

constexpr std::size_t MAX_FRAME_SLOT_COUNT = 64U;
constexpr std::size_t MAX_FRAME_SLOT_PAYLOAD_BYTES = 16U * 1024U * 1024U;
constexpr std::size_t MAX_FRAME_SLOT_STORAGE_BYTES = 64U * 1024U * 1024U;

struct FrameSlotToken {
  std::uint32_t slot_id = 0U;
  std::uint64_t generation = 0U;
  std::uint64_t pool_id = 0U;

  friend bool operator==(const FrameSlotToken &,
                         const FrameSlotToken &) = default;
};

enum class FrameSlotState {
  Free,
  Writing,
  Published,
  Reading,
};

class FrameSlotError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class FrameSlotCapacityError : public FrameSlotError {
public:
  using FrameSlotError::FrameSlotError;
};

class FrameSlotGenerationError : public FrameSlotError {
public:
  using FrameSlotError::FrameSlotError;
};

class FrameSlotOwnershipError : public FrameSlotError {
public:
  using FrameSlotError::FrameSlotError;
};

class FrameSlotStateError : public FrameSlotError {
public:
  using FrameSlotError::FrameSlotError;
};

class FrameSlotPool;

class FrameWriteLease {
public:
  FrameWriteLease() noexcept = default;
  ~FrameWriteLease() noexcept;

  FrameWriteLease(const FrameWriteLease &) = delete;
  FrameWriteLease &operator=(const FrameWriteLease &) = delete;
  FrameWriteLease(FrameWriteLease &&other) noexcept;
  FrameWriteLease &operator=(FrameWriteLease &&other) noexcept;

  FrameSlotToken Token() const noexcept;
  std::span<std::byte> Payload();
  std::size_t PayloadSize() const;
  void SetPayloadSize(std::size_t size);
  void Publish();
  void Cancel() noexcept;

private:
  friend class FrameSlotPool;

  FrameWriteLease(FrameSlotPool *pool, FrameSlotToken token) noexcept;

  FrameSlotPool *m_pool = nullptr;
  FrameSlotToken m_token;
  bool m_active = false;
};

class FrameReadLease {
public:
  FrameReadLease() noexcept = default;
  ~FrameReadLease() noexcept;

  FrameReadLease(const FrameReadLease &) = delete;
  FrameReadLease &operator=(const FrameReadLease &) = delete;
  FrameReadLease(FrameReadLease &&other) noexcept;
  FrameReadLease &operator=(FrameReadLease &&other) noexcept;

  FrameSlotToken Token() const noexcept;
  std::span<const std::byte> Payload() const;
  std::size_t PayloadSize() const;
  void Release() noexcept;

private:
  friend class FrameSlotPool;

  FrameReadLease(FrameSlotPool *pool, FrameSlotToken token) noexcept;

  FrameSlotPool *m_pool = nullptr;
  FrameSlotToken m_token;
  bool m_active = false;
};

class FrameSlotPool {
public:
  FrameSlotPool(std::size_t slot_count, std::size_t payload_capacity);
  ~FrameSlotPool() noexcept;

  FrameSlotPool(const FrameSlotPool &) = delete;
  FrameSlotPool &operator=(const FrameSlotPool &) = delete;
  FrameSlotPool(FrameSlotPool &&) = delete;
  FrameSlotPool &operator=(FrameSlotPool &&) = delete;

  std::optional<FrameWriteLease> TryAcquireWrite();
  std::optional<FrameWriteLease> AcquireWrite();
  std::optional<FrameReadLease> TryAcquireRead(FrameSlotToken token);
  std::optional<FrameReadLease> AcquireRead(FrameSlotToken token);

  void Close() noexcept;
  void DiscardPublished(FrameSlotToken token);
  bool IsClosed() const;
  bool IsQuiescent() const;
  std::size_t SlotCount() const noexcept;
  std::size_t PayloadCapacity() const noexcept;

private:
  struct SlotStorage {
    FrameSlotState state = FrameSlotState::Free;
    std::uint64_t generation = 0U;
    std::size_t payload_size = 0U;
    bool was_cancelled = false;
    std::vector<std::byte> payload;
  };

  friend class FrameReadLease;
  friend class FrameWriteLease;

  std::optional<FrameReadLease> AcquireReadImpl(FrameSlotToken token,
                                                bool blocking);
  std::size_t FindFreeSlotLocked() const noexcept;
  void RegisterProducerLocked();
  void RegisterConsumerLocked();
  void AssertProducerLocked() const;
  void AssertConsumerLocked() const;
  SlotStorage &SlotForTokenLocked(FrameSlotToken token);
  const SlotStorage &SlotForTokenLocked(FrameSlotToken token) const;
  void EndOperationLocked() noexcept;
  std::span<std::byte> WritePayload(FrameSlotToken token);
  std::size_t WritePayloadSize(FrameSlotToken token) const;
  void SetWritePayloadSize(FrameSlotToken token, std::size_t size);
  void PublishWrite(FrameSlotToken token);
  void CancelWrite(FrameSlotToken token);
  std::span<const std::byte> ReadPayload(FrameSlotToken token) const;
  std::size_t ReadPayloadSize(FrameSlotToken token) const;
  void ReleaseRead(FrameSlotToken token);

  mutable std::mutex m_mutex;
  std::condition_variable m_state_changed;
  std::vector<SlotStorage> m_slots;
  std::size_t m_payload_capacity = 0U;
  std::uint64_t m_pool_id = 0U;
  bool m_closed = false;
  bool m_has_producer = false;
  bool m_has_consumer = false;
  std::thread::id m_producer_thread;
  std::thread::id m_consumer_thread;
  std::size_t m_active_operations = 0U;
  std::condition_variable m_operations_idle;
};

} // namespace ithax::threading
