#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <vector>

#include "jobs/job_runtime.h"

namespace ithax::threading {
class FramePacketChannel;
}

namespace ithax::runtime {

constexpr std::size_t MAX_PENDING_RUNTIME_TASKS = 4'096U;

using TaskId = std::uint64_t;

enum class LifecycleState {
  Starting,
  Running,
  StopRequested,
  Draining,
  Stopped,
  Failed,
};

enum class FailureSource {
  Job,
  Owner,
  FrameChannel,
};

struct RuntimeFailure {
  FailureSource source;
  std::exception_ptr error;
};

class RuntimeSupervisorError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

class RuntimeSupervisor {
public:
  RuntimeSupervisor(ithax::ThreadBudget &budget, std::uint32_t worker_count);
  ~RuntimeSupervisor() noexcept;

  RuntimeSupervisor(const RuntimeSupervisor &) = delete;
  RuntimeSupervisor &operator=(const RuntimeSupervisor &) = delete;
  RuntimeSupervisor(RuntimeSupervisor &&) = delete;
  RuntimeSupervisor &operator=(RuntimeSupervisor &&) = delete;

  void Start();
  TaskId Submit(tf::Taskflow taskflow);
  // The caller keeps the graph alive and unchanged until its task is waited.
  TaskId SubmitReusable(tf::Taskflow &taskflow);
  void Wait(TaskId task_id);

  void ReportFailure(FailureSource source, std::exception_ptr error);
  void RequestStop();
  void RequestStop(ithax::threading::FramePacketChannel &channel);
  void Drain();

  LifecycleState State() const;
  std::stop_token StopToken() const noexcept;
  std::optional<RuntimeFailure> Failure() const;
  void RethrowFailure() const;
  std::size_t PendingTaskCount() const;

private:
  struct PendingTask {
    TaskId id;
    ithax::jobs::TaskHandle handle;
  };

  void ConsumePendingTasks(std::vector<PendingTask> &tasks);
  void RecordFailure(FailureSource source, std::exception_ptr error);
  void ReleaseConsumer();

  ithax::jobs::JobRuntime m_jobs;
  std::stop_source m_stop_source;
  mutable std::mutex m_submission_mutex;
  std::mutex m_drain_mutex;
  LifecycleState m_state = LifecycleState::Starting;
  std::optional<RuntimeFailure> m_failure;
  std::vector<PendingTask> m_pending_tasks;
  TaskId m_next_task_id = 1U;
  std::size_t m_active_consumers = 0U;
  std::condition_variable m_consumers_idle;
};

} // namespace ithax::runtime
