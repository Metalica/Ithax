#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>

namespace ithax::threading {

constexpr std::size_t MAX_SCRATCH_ALLOCATOR_CAPACITY = 16U * 1024U * 1024U;
constexpr std::size_t SCRATCH_ALLOCATOR_BASE_ALIGNMENT =
    alignof(std::max_align_t);

class ScratchAllocatorError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class ScratchAllocatorCapacityError : public ScratchAllocatorError {
public:
  using ScratchAllocatorError::ScratchAllocatorError;
};

class ScratchAllocatorAlignmentError : public ScratchAllocatorError {
public:
  using ScratchAllocatorError::ScratchAllocatorError;
};

class ScratchAllocatorOverflowError : public ScratchAllocatorError {
public:
  using ScratchAllocatorError::ScratchAllocatorError;
};

class ScratchAllocatorOwnershipError : public ScratchAllocatorError {
public:
  using ScratchAllocatorError::ScratchAllocatorError;
};

class ScratchAllocator {
public:
  explicit ScratchAllocator(std::size_t capacity);
  ~ScratchAllocator() noexcept;

  ScratchAllocator(const ScratchAllocator &) = delete;
  ScratchAllocator &operator=(const ScratchAllocator &) = delete;
  ScratchAllocator(ScratchAllocator &&) = delete;
  ScratchAllocator &operator=(ScratchAllocator &&) = delete;

  // Returned spans remain valid until Reset() or destruction.
  std::span<std::byte>
  Allocate(std::size_t size,
           std::size_t alignment = SCRATCH_ALLOCATOR_BASE_ALIGNMENT);
  void Reset();

  bool IsOwnerThread() const noexcept;
  std::size_t Capacity() const noexcept;
  std::size_t Used() const;
  std::size_t Remaining() const;

private:
  struct StorageDeleter {
    void operator()(std::byte *storage) const noexcept;
  };

  static std::size_t ValidateCapacity(std::size_t capacity);
  static std::size_t ValidateAlignment(std::size_t alignment);
  static std::size_t ComputePadding(std::size_t offset,
                                    std::size_t alignment) noexcept;
  void AssertOwner() const;

  std::unique_ptr<std::byte, StorageDeleter> m_storage;
  std::size_t m_capacity;
  std::size_t m_offset = 0U;
  std::thread::id m_owner_thread;
};

} // namespace ithax::threading
