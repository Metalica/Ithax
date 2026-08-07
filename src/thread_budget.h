#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ithax {

constexpr std::uint32_t DEFAULT_HARD_RESERVATIONS = 1U;
constexpr std::uint32_t DEFAULT_SOFT_RESERVATIONS = 0U;
constexpr std::uint32_t DEFAULT_HEADROOM = 1U;
constexpr std::uint32_t MAX_CPU_SET_IDS = 1024U;
constexpr std::uint32_t MAX_LABEL_LENGTH = 64U;

enum class ReservationKind {
  Hard,
  Soft,
  Unknown,
};

struct ThreadBudgetPolicy {
  std::uint32_t hard_reservations = DEFAULT_HARD_RESERVATIONS;
  std::uint32_t soft_reservations = DEFAULT_SOFT_RESERVATIONS;
  std::uint32_t headroom = DEFAULT_HEADROOM;
};

struct ThreadBudgetSnapshot {
  std::string phase;
  std::string owner;
  std::string subsystem;
  std::uint32_t configured_threads = 0U;
  std::uint32_t observed_threads = 0U;
  std::uint32_t peak_observed_threads = 0U;
  std::uint32_t process_cpu_set_count = 0U;
  std::vector<std::uint32_t> cpu_set_ids;
  std::uint64_t process_affinity_mask = 0U;
  std::string cpu_set_source;
  std::string timestamp_utc;
  ReservationKind reservation = ReservationKind::Unknown;
};

class ThreadBudgetError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class ThreadBudget {
public:
  explicit ThreadBudget(ThreadBudgetPolicy policy);

  ThreadBudgetSnapshot Capture(std::string phase, std::string owner,
                               std::string subsystem,
                               std::uint32_t configured_threads,
                               ReservationKind reservation);

  std::uint32_t AvailableWorkerCount() const noexcept;
  bool CanReserveWorkers(std::uint32_t requested) const noexcept;

  const ThreadBudgetPolicy &Policy() const noexcept;
  std::uint32_t ProcessCpuSetCount() const noexcept;
  std::uint32_t PeakObservedThreads() const noexcept;
  const std::vector<ThreadBudgetSnapshot> &Snapshots() const noexcept;

private:
  static void ValidateLabel(const std::string &label);

  ThreadBudgetPolicy m_policy;
  std::uint32_t m_processCpuSetCount = 0U;
  std::uint32_t m_peakObservedThreads = 0U;
  std::vector<ThreadBudgetSnapshot> m_snapshots;
};

const char *ReservationKindName(ReservationKind kind) noexcept;

} // namespace ithax
