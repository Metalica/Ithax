#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <taskflow/algorithm/for_each.hpp>
#include <taskflow/taskflow.hpp>

#include "thread_budget.h"

namespace ithax::jobs {

constexpr std::size_t MAX_PARALLEL_FOR_ITEMS = 1'000'000U;
constexpr std::uint32_t MAX_JOB_WORKERS = 64U;

enum class RuntimeState {
  Running,
  StopRequested,
  Draining,
  Stopped,
};

class JobRuntimeError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class JobExecutionError : public JobRuntimeError {
public:
  using JobRuntimeError::JobRuntimeError;
};

class TaskHandle {
public:
  TaskHandle(const TaskHandle &) = delete;
  TaskHandle &operator=(const TaskHandle &) = delete;
  TaskHandle(TaskHandle &&) noexcept = default;
  TaskHandle &operator=(TaskHandle &&) noexcept = default;

  bool Cancel();
  void Wait();
  bool Valid() const noexcept;

private:
  friend class JobRuntime;

  explicit TaskHandle(tf::Future<void> &&future) noexcept;

  tf::Future<void> m_future;
};

class JobRuntime {
public:
  JobRuntime(ithax::ThreadBudget &budget, std::uint32_t worker_count);
  ~JobRuntime() noexcept;

  JobRuntime(const JobRuntime &) = delete;
  JobRuntime &operator=(const JobRuntime &) = delete;
  JobRuntime(JobRuntime &&) = delete;
  JobRuntime &operator=(JobRuntime &&) = delete;

  TaskHandle Submit(tf::Taskflow taskflow);
  // The caller keeps the graph alive and unchanged until its handle is waited.
  TaskHandle SubmitReusable(tf::Taskflow &taskflow);

  template <typename Callable>
  TaskHandle ParallelFor(
      std::size_t first, std::size_t last, Callable callable) {
    static_assert(
        std::is_invocable_v<Callable &, std::size_t>,
        "ParallelFor callable must accept a size_t index");
    if (last < first) {
      throw JobRuntimeError("parallel-for range is reversed");
    }
    if (last - first > MAX_PARALLEL_FOR_ITEMS) {
      throw JobRuntimeError("parallel-for range exceeds its bound");
    }

    tf::Taskflow taskflow;
    taskflow.for_each_index(
        first, last, std::size_t{1U}, std::move(callable));
    return Submit(std::move(taskflow));
  }

  void RequestStop();
  void Drain();

  RuntimeState State() const;
  std::uint32_t WorkerCount() const noexcept;

private:
  static std::uint32_t PrepareWorkerCount(
      ithax::ThreadBudget &budget, std::uint32_t requested);
  void ShutdownNoexcept() noexcept;

  std::uint32_t m_worker_count;
  tf::Executor m_executor;
  mutable std::mutex m_state_mutex;
  RuntimeState m_state = RuntimeState::Running;
};

} // namespace ithax::jobs
