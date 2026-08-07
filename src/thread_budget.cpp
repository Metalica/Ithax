#include "thread_budget.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstddef>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on
#elif defined(__linux__)
#include <sched.h>
#include <unistd.h>
#endif

namespace {

constexpr std::uint32_t MAX_SNAPSHOT_COUNT = 4096U;

struct PlatformSnapshot {
  std::uint32_t process_threads = 0U;
  std::uint32_t process_cpu_set_count = 0U;
  std::vector<std::uint32_t> cpu_set_ids;
  std::uint64_t process_affinity_mask = 0U;
  std::string cpu_set_source;
};

std::string ErrorMessage(const char *operation, unsigned long code) {
  return std::string(operation) + " failed with error " + std::to_string(code);
}

std::string TimestampUtc() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds);
  const std::time_t time = std::chrono::system_clock::to_time_t(seconds);
  std::tm calendar{};
#if defined(_WIN32)
  if (gmtime_s(&calendar, &time) != 0) {
    throw ithax::ThreadBudgetError("gmtime_s failed");
  }
#else
  if (gmtime_r(&time, &calendar) == nullptr) {
    throw ithax::ThreadBudgetError("gmtime_r failed");
  }
#endif

  std::ostringstream result;
  result << std::put_time(&calendar, "%Y-%m-%dT%H:%M:%S") << '.'
         << std::setfill('0') << std::setw(3) << milliseconds.count() << 'Z';
  return result.str();
}

#if defined(_WIN32)

class ScopedHandle {
public:
  explicit ScopedHandle(HANDLE handle) noexcept : m_handle(handle) {}

  ~ScopedHandle() noexcept {
    if (m_handle != INVALID_HANDLE_VALUE && m_handle != nullptr) {
      CloseHandle(m_handle);
    }
  }

  ScopedHandle(const ScopedHandle &) = delete;
  ScopedHandle &operator=(const ScopedHandle &) = delete;

  HANDLE Get() const noexcept { return m_handle; }

private:
  HANDLE m_handle;
};

std::uint32_t CountProcessThreads() {
  const DWORD processId = GetCurrentProcessId();
  ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
  if (snapshot.Get() == INVALID_HANDLE_VALUE) {
    throw ithax::ThreadBudgetError(
        ErrorMessage("CreateToolhelp32Snapshot", GetLastError()));
  }

  THREADENTRY32 entry{};
  entry.dwSize = sizeof(entry);
  if (Thread32First(snapshot.Get(), &entry) == FALSE) {
    const DWORD error = GetLastError();
    if (error == ERROR_NO_MORE_FILES) {
      return 0U;
    }
    throw ithax::ThreadBudgetError(ErrorMessage("Thread32First", error));
  }

  std::uint32_t count = 0U;
  do {
    if (entry.th32OwnerProcessID == processId) {
      if (count == std::numeric_limits<std::uint32_t>::max()) {
        throw ithax::ThreadBudgetError(
            "process thread count exceeded its measurement bound");
      }
      ++count;
    }
  } while (Thread32Next(snapshot.Get(), &entry) != FALSE);

  const DWORD error = GetLastError();
  if (error != ERROR_NO_MORE_FILES) {
    throw ithax::ThreadBudgetError(ErrorMessage("Thread32Next", error));
  }
  return count;
}

PlatformSnapshot CapturePlatformSnapshot() {
  PlatformSnapshot snapshot;
  snapshot.process_threads = CountProcessThreads();

  ULONG required = 0U;
  GetProcessDefaultCpuSets(GetCurrentProcess(), nullptr, 0U, &required);
  if (required > ithax::MAX_CPU_SET_IDS) {
    throw ithax::ThreadBudgetError(
        "process CPU-set count exceeded its measurement bound");
  }
  if (required > 0U) {
    std::vector<ULONG> cpu_set_ids(required);
    if (GetProcessDefaultCpuSets(GetCurrentProcess(), cpu_set_ids.data(),
                                 required, &required) == FALSE) {
      throw ithax::ThreadBudgetError(
          ErrorMessage("GetProcessDefaultCpuSets", GetLastError()));
    }
    snapshot.process_cpu_set_count = static_cast<std::uint32_t>(required);
    snapshot.cpu_set_ids.reserve(required);
    for (const ULONG cpu_set_id : cpu_set_ids) {
      snapshot.cpu_set_ids.push_back(static_cast<std::uint32_t>(cpu_set_id));
    }
    snapshot.cpu_set_source = "process_default_cpu_sets";
    return snapshot;
  }

  DWORD_PTR processMask = 0U;
  DWORD_PTR systemMask = 0U;
  if (GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask) ==
      FALSE) {
    throw ithax::ThreadBudgetError(
        ErrorMessage("GetProcessAffinityMask", GetLastError()));
  }
  snapshot.process_affinity_mask = static_cast<std::uint64_t>(processMask);
  for (std::size_t bit = 0U; bit < sizeof(DWORD_PTR) * CHAR_BIT; ++bit) {
    const DWORD_PTR value = DWORD_PTR(1) << bit;
    if ((processMask & value) != 0U) {
      snapshot.cpu_set_ids.push_back(static_cast<std::uint32_t>(bit));
    }
  }
  snapshot.process_cpu_set_count =
      static_cast<std::uint32_t>(snapshot.cpu_set_ids.size());
  snapshot.cpu_set_source = "process_affinity_mask";
  return snapshot;
}

#elif defined(__linux__)

std::uint32_t CountProcessThreads() {
  std::error_code error;
  std::uint32_t count = 0U;
  for (const auto &entry :
       std::filesystem::directory_iterator("/proc/self/task", error)) {
    static_cast<void>(entry);
    if (count == std::numeric_limits<std::uint32_t>::max()) {
      throw ithax::ThreadBudgetError(
          "process thread count exceeded its measurement bound");
    }
    ++count;
  }
  if (error) {
    throw ithax::ThreadBudgetError("reading /proc/self/task failed: " +
                                   error.message());
  }
  return count;
}

PlatformSnapshot CapturePlatformSnapshot() {
  PlatformSnapshot snapshot;
  snapshot.process_threads = CountProcessThreads();
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (sched_getaffinity(0, sizeof(mask), &mask) != 0) {
    throw ithax::ThreadBudgetError(ErrorMessage("sched_getaffinity", errno));
  }
  for (std::uint32_t cpu = 0U; cpu < CPU_SETSIZE; ++cpu) {
    if (CPU_ISSET(cpu, &mask)) {
      if (snapshot.cpu_set_ids.size() >= ithax::MAX_CPU_SET_IDS) {
        throw ithax::ThreadBudgetError(
            "process CPU-set count exceeded its measurement bound");
      }
      snapshot.cpu_set_ids.push_back(cpu);
    }
  }
  snapshot.process_cpu_set_count =
      static_cast<std::uint32_t>(snapshot.cpu_set_ids.size());
  snapshot.cpu_set_source = "sched_getaffinity";
  return snapshot;
}

#else

PlatformSnapshot CapturePlatformSnapshot() {
  throw ithax::ThreadBudgetError(
      "process CPU-set measurement is not implemented on this platform");
}

#endif

} // namespace

namespace ithax {

const char *ReservationKindName(const ReservationKind kind) noexcept {
  switch (kind) {
  case ReservationKind::Hard:
    return "hard";
  case ReservationKind::Soft:
    return "soft";
  case ReservationKind::Unknown:
    return "unknown";
  }
  return "unknown";
}

ThreadBudget::ThreadBudget(ThreadBudgetPolicy policy) : m_policy(policy) {}

std::uint32_t ThreadBudget::QueryProcessCpuSetCount() {
  const PlatformSnapshot platform = CapturePlatformSnapshot();
  if (platform.process_cpu_set_count == 0U) {
    throw ThreadBudgetError("platform returned an empty CPU set");
  }
  return platform.process_cpu_set_count;
}

void ThreadBudget::ValidateLabel(const std::string &label) {
  if (label.empty() || label.size() > MAX_LABEL_LENGTH) {
    throw ThreadBudgetError("thread budget label is outside its bound");
  }
  for (const unsigned char character : label) {
    if (character < 0x20U || character > 0x7eU) {
      throw ThreadBudgetError("thread budget label contains a control byte");
    }
  }
}

ThreadBudgetSnapshot ThreadBudget::Capture(
    std::string phase, std::string owner, std::string subsystem,
    const std::uint32_t configured_threads, const ReservationKind reservation) {
  if (m_snapshots.size() >= MAX_SNAPSHOT_COUNT) {
    throw ThreadBudgetError("thread budget snapshot limit was exceeded");
  }
  ValidateLabel(phase);
  ValidateLabel(owner);
  ValidateLabel(subsystem);

  const PlatformSnapshot platform = CapturePlatformSnapshot();
  if (platform.process_threads == 0U || platform.process_cpu_set_count == 0U) {
    throw ThreadBudgetError("platform returned an empty thread budget");
  }

  ThreadBudgetSnapshot snapshot;
  snapshot.phase = std::move(phase);
  snapshot.owner = std::move(owner);
  snapshot.subsystem = std::move(subsystem);
  snapshot.configured_threads = configured_threads;
  snapshot.observed_threads = platform.process_threads;
  snapshot.peak_observed_threads =
      std::max(m_peakObservedThreads, platform.process_threads);
  snapshot.process_cpu_set_count = platform.process_cpu_set_count;
  snapshot.cpu_set_ids = platform.cpu_set_ids;
  snapshot.process_affinity_mask = platform.process_affinity_mask;
  snapshot.cpu_set_source = platform.cpu_set_source;
  snapshot.timestamp_utc = TimestampUtc();
  snapshot.reservation = reservation;

  m_processCpuSetCount = platform.process_cpu_set_count;
  m_peakObservedThreads = snapshot.peak_observed_threads;
  m_snapshots.push_back(snapshot);
  return snapshot;
}

std::uint32_t ThreadBudget::AvailableWorkerCount() const noexcept {
  const std::uint64_t reserved =
      static_cast<std::uint64_t>(m_policy.hard_reservations) +
      static_cast<std::uint64_t>(m_policy.soft_reservations) +
      static_cast<std::uint64_t>(m_policy.headroom);
  if (reserved >= m_processCpuSetCount) {
    return 0U;
  }
  return m_processCpuSetCount - static_cast<std::uint32_t>(reserved);
}

bool ThreadBudget::CanReserveWorkers(
    const std::uint32_t requested) const noexcept {
  return requested > 0U && requested <= AvailableWorkerCount();
}

const ThreadBudgetPolicy &ThreadBudget::Policy() const noexcept {
  return m_policy;
}

std::uint32_t ThreadBudget::ProcessCpuSetCount() const noexcept {
  return m_processCpuSetCount;
}

std::uint32_t ThreadBudget::PeakObservedThreads() const noexcept {
  return m_peakObservedThreads;
}

const std::vector<ThreadBudgetSnapshot> &
ThreadBudget::Snapshots() const noexcept {
  return m_snapshots;
}

} // namespace ithax
