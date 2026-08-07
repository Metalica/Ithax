#include "runtime/runtime_supervisor.h"

#include <algorithm>
#include <limits>
#include <utility>

#include "threading/frame_packet_channel.h"

namespace ithax::runtime {

RuntimeSupervisor::RuntimeSupervisor(ithax::ThreadBudget &budget,
                                     const std::uint32_t worker_count)
    : m_jobs(budget, worker_count) {
  m_pending_tasks.reserve(MAX_PENDING_RUNTIME_TASKS);
}

RuntimeSupervisor::~RuntimeSupervisor() noexcept {
  try {
    Drain();
  } catch (...) {
    std::terminate();
  }
}

void RuntimeSupervisor::Start() {
  std::lock_guard lock(m_submission_mutex);
  if (m_state != LifecycleState::Starting) {
    throw RuntimeSupervisorError("runtime supervisor was already started");
  }
  if (m_failure.has_value()) {
    throw RuntimeSupervisorError(
        "runtime supervisor cannot start after a failure");
  }
  m_state = LifecycleState::Running;
}

TaskId RuntimeSupervisor::Submit(tf::Taskflow taskflow) {
  std::lock_guard submission_lock(m_submission_mutex);
  if (m_state != LifecycleState::Running) {
    throw RuntimeSupervisorError(
        "runtime submission rejected outside the running state");
  }
  if (m_pending_tasks.size() >= MAX_PENDING_RUNTIME_TASKS) {
    throw RuntimeSupervisorError("runtime pending-task bound was exceeded");
  }
  if (m_next_task_id == std::numeric_limits<TaskId>::max()) {
    throw RuntimeSupervisorError("runtime task identifier exhausted");
  }

  auto handle = m_jobs.Submit(std::move(taskflow));
  const TaskId task_id = m_next_task_id++;
  m_pending_tasks.push_back({task_id, std::move(handle)});
  return task_id;
}

TaskId RuntimeSupervisor::SubmitReusable(tf::Taskflow &taskflow) {
  std::lock_guard submission_lock(m_submission_mutex);
  if (m_state != LifecycleState::Running) {
    throw RuntimeSupervisorError(
        "runtime submission rejected outside the running state");
  }
  if (m_pending_tasks.size() >= MAX_PENDING_RUNTIME_TASKS) {
    throw RuntimeSupervisorError("runtime pending-task bound was exceeded");
  }
  if (m_next_task_id == std::numeric_limits<TaskId>::max()) {
    throw RuntimeSupervisorError("runtime task identifier exhausted");
  }

  auto handle = m_jobs.SubmitReusable(taskflow);
  const TaskId task_id = m_next_task_id++;
  m_pending_tasks.push_back({task_id, std::move(handle)});
  return task_id;
}

void RuntimeSupervisor::Wait(const TaskId task_id) {
  if (task_id == 0U) {
    throw RuntimeSupervisorError("runtime task identifier is invalid");
  }

  std::optional<PendingTask> task;
  {
    std::lock_guard submission_lock(m_submission_mutex);
    const auto task_iterator =
        std::find_if(m_pending_tasks.begin(), m_pending_tasks.end(),
                     [task_id](const PendingTask &pending) {
                       return pending.id == task_id;
                     });
    if (task_iterator == m_pending_tasks.end()) {
      throw RuntimeSupervisorError("runtime task identifier was not pending");
    }
    task.emplace(std::move(*task_iterator));
    m_pending_tasks.erase(task_iterator);
    ++m_active_consumers;
  }

  try {
    task->handle.Wait();
  } catch (...) {
    const auto error = std::current_exception();
    try {
      RecordFailure(FailureSource::Job, error);
      RequestStop();
    } catch (...) {
      ReleaseConsumer();
      throw;
    }
    ReleaseConsumer();
    std::rethrow_exception(error);
  }
  ReleaseConsumer();
}

void RuntimeSupervisor::ReportFailure(const FailureSource source,
                                      const std::exception_ptr error) {
  if (!error) {
    throw RuntimeSupervisorError("runtime failure has no exception");
  }

  RecordFailure(source, error);
  RequestStop();
}

void RuntimeSupervisor::RequestStop() {
  m_stop_source.request_stop();
  std::lock_guard lock(m_submission_mutex);
  if (m_state == LifecycleState::Starting ||
      m_state == LifecycleState::Running) {
    m_state = LifecycleState::StopRequested;
  }
  if (m_state == LifecycleState::Stopped) {
    return;
  }
  m_jobs.RequestStop();
}

void RuntimeSupervisor::RequestStop(
    ithax::threading::FramePacketChannel &channel) {
  RequestStop();
  channel.Close();
}

void RuntimeSupervisor::Drain() {
  std::lock_guard drain_lock(m_drain_mutex);
  m_stop_source.request_stop();
  std::vector<PendingTask> tasks;
  {
    std::lock_guard submission_lock(m_submission_mutex);
    if (m_state == LifecycleState::Stopped) {
      return;
    }
    if (m_state == LifecycleState::Starting ||
        m_state == LifecycleState::Running ||
        m_state == LifecycleState::StopRequested) {
      m_state = LifecycleState::Draining;
    }
    m_jobs.RequestStop();
    tasks.swap(m_pending_tasks);
  }

  ConsumePendingTasks(tasks);
  try {
    m_jobs.Drain();
  } catch (...) {
    const auto error = std::current_exception();
    RecordFailure(FailureSource::Job, error);
    RequestStop();
  }

  std::unique_lock submission_lock(m_submission_mutex);
  m_consumers_idle.wait(submission_lock,
                        [this]() { return m_active_consumers == 0U; });
  m_state =
      m_failure.has_value() ? LifecycleState::Failed : LifecycleState::Stopped;
}

LifecycleState RuntimeSupervisor::State() const {
  std::lock_guard lock(m_submission_mutex);
  return m_state;
}

std::stop_token RuntimeSupervisor::StopToken() const noexcept {
  return m_stop_source.get_token();
}

std::optional<RuntimeFailure> RuntimeSupervisor::Failure() const {
  std::lock_guard lock(m_submission_mutex);
  return m_failure;
}

void RuntimeSupervisor::RethrowFailure() const {
  const auto failure = Failure();
  if (failure.has_value()) {
    std::rethrow_exception(failure->error);
  }
}

std::size_t RuntimeSupervisor::PendingTaskCount() const {
  std::lock_guard lock(m_submission_mutex);
  return m_pending_tasks.size();
}

void RuntimeSupervisor::ConsumePendingTasks(std::vector<PendingTask> &tasks) {
  for (auto &pending : tasks) {
    try {
      pending.handle.Wait();
    } catch (...) {
      const auto error = std::current_exception();
      RecordFailure(FailureSource::Job, error);
      RequestStop();
    }
  }
  m_pending_tasks.clear();
}

void RuntimeSupervisor::RecordFailure(const FailureSource source,
                                      const std::exception_ptr error) {
  if (!error) {
    throw RuntimeSupervisorError("runtime failure has no exception");
  }
  std::lock_guard lock(m_submission_mutex);
  if (m_state == LifecycleState::Stopped) {
    throw RuntimeSupervisorError("runtime failure was reported after shutdown");
  }
  if (!m_failure.has_value()) {
    m_failure = RuntimeFailure{source, error};
  }
  if (m_state == LifecycleState::Starting ||
      m_state == LifecycleState::Running) {
    m_state = LifecycleState::StopRequested;
  }
}

void RuntimeSupervisor::ReleaseConsumer() {
  std::lock_guard lock(m_submission_mutex);
  if (m_active_consumers == 0U) {
    throw RuntimeSupervisorError("runtime consumer count underflowed");
  }
  --m_active_consumers;
  if (m_active_consumers == 0U) {
    m_consumers_idle.notify_all();
  }
}

} // namespace ithax::runtime
