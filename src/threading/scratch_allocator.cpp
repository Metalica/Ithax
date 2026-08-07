#include "threading/scratch_allocator.h"

#include <new>

namespace ithax::threading {

void ScratchAllocator::StorageDeleter::operator()(
    std::byte *storage) const noexcept {
  if (storage != nullptr) {
    ::operator delete(storage,
                      std::align_val_t{SCRATCH_ALLOCATOR_BASE_ALIGNMENT});
  }
}

ScratchAllocator::ScratchAllocator(const std::size_t capacity)
    : m_storage(static_cast<std::byte *>(
          ::operator new(ValidateCapacity(capacity),
                         std::align_val_t{SCRATCH_ALLOCATOR_BASE_ALIGNMENT}))),
      m_capacity(capacity), m_owner_thread(std::this_thread::get_id()) {}

ScratchAllocator::~ScratchAllocator() noexcept = default;

std::span<std::byte> ScratchAllocator::Allocate(const std::size_t size,
                                                const std::size_t alignment) {
  AssertOwner();
  const auto valid_alignment = ValidateAlignment(alignment);
  if (size == 0U) {
    return {};
  }

  const auto padding = ComputePadding(m_offset, valid_alignment);
  if (padding > m_capacity - m_offset ||
      size > m_capacity - m_offset - padding) {
    throw ScratchAllocatorOverflowError(
        "scratch allocation exceeds its fixed capacity");
  }

  const auto start = m_offset + padding;
  m_offset = start + size;
  return {m_storage.get() + start, size};
}

void ScratchAllocator::Reset() {
  AssertOwner();
  m_offset = 0U;
}

bool ScratchAllocator::IsOwnerThread() const noexcept {
  return m_owner_thread == std::this_thread::get_id();
}

std::size_t ScratchAllocator::Capacity() const noexcept { return m_capacity; }

std::size_t ScratchAllocator::Used() const {
  AssertOwner();
  return m_offset;
}

std::size_t ScratchAllocator::Remaining() const {
  AssertOwner();
  return m_capacity - m_offset;
}

std::size_t ScratchAllocator::ValidateCapacity(const std::size_t capacity) {
  if (capacity == 0U || capacity > MAX_SCRATCH_ALLOCATOR_CAPACITY) {
    throw ScratchAllocatorCapacityError(
        "scratch allocator capacity is outside its bound");
  }
  return capacity;
}

std::size_t ScratchAllocator::ValidateAlignment(const std::size_t alignment) {
  if (alignment == 0U || alignment > SCRATCH_ALLOCATOR_BASE_ALIGNMENT ||
      (alignment & (alignment - 1U)) != 0U) {
    throw ScratchAllocatorAlignmentError(
        "scratch allocator alignment is invalid");
  }
  return alignment;
}

std::size_t
ScratchAllocator::ComputePadding(const std::size_t offset,
                                 const std::size_t alignment) noexcept {
  const auto remainder = offset % alignment;
  return remainder == 0U ? 0U : alignment - remainder;
}

void ScratchAllocator::AssertOwner() const {
  if (!IsOwnerThread()) {
    throw ScratchAllocatorOwnershipError(
        "scratch allocator accessed by a non-owner thread");
  }
}

} // namespace ithax::threading
