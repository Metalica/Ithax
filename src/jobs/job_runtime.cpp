#include "jobs/job_runtime.h"

#include <exception>
#include <utility>

namespace ithax::jobs {

TaskHandle::TaskHandle(tf::Future<void> &&future) noexcept
    : m_future(std::move(future)) {}

bool TaskHandle::Cancel() {
  if (!m_future.valid()) {
    return false;
  }
  return m_future.cancel();
}

void TaskHandle::Wait() {
  if (!m_future.valid()) {
    throw JobRuntimeError("job handle has no unconsumed result");
  }

  try {
    m_future.get();
  } catch (...) {
    std::throw_with_nested(JobExecutionError("job execution failed"));
  }
}

bool TaskHandle::Valid() const noexcept { return m_future.valid(); }

std::uint32_t JobRuntime::PrepareWorkerCount(
    ithax::ThreadBudget &budget, const std::uint32_t requested) {
  if (requested == 0U || requested > MAX_JOB_WORKERS) {
    throw JobRuntimeError("job worker count is outside its bound");
  }

  budget.Capture(
      "job-runtime-preflight", "runtime-owner", "taskflow", requested,
      ithax::ReservationKind::Hard);
  if (!budget.CanReserveWorkers(requested)) {
    throw JobRuntimeError("job workers exceed measured thread headroom");
  }
  return requested;
}

JobRuntime::JobRuntime(
    ithax::ThreadBudget &budget, const std::uint32_t worker_count)
    : m_worker_count(PrepareWorkerCount(budget, worker_count)),
      m_executor(m_worker_count) {}

JobRuntime::~JobRuntime() noexcept { ShutdownNoexcept(); }

TaskHandle JobRuntime::Submit(tf::Taskflow taskflow) {
  std::lock_guard lock(m_state_mutex);
  if (m_state != RuntimeState::Running) {
    throw JobRuntimeError("job submission rejected after stop request");
  }

  try {
    return TaskHandle(m_executor.run(std::move(taskflow)));
  } catch (...) {
    std::throw_with_nested(JobRuntimeError("job submission failed"));
  }
}

TaskHandle JobRuntime::SubmitReusable(tf::Taskflow &taskflow) {
  std::lock_guard lock(m_state_mutex);
  if (m_state != RuntimeState::Running) {
    throw JobRuntimeError("job submission rejected after stop request");
  }

  try {
    return TaskHandle(m_executor.run(taskflow));
  } catch (...) {
    std::throw_with_nested(JobRuntimeError("job submission failed"));
  }
}

void JobRuntime::RequestStop() {
  std::lock_guard lock(m_state_mutex);
  if (m_state == RuntimeState::Running) {
    m_state = RuntimeState::StopRequested;
  }
}

void JobRuntime::Drain() {
  {
    std::lock_guard lock(m_state_mutex);
    if (m_state == RuntimeState::Stopped) {
      return;
    }
    if (m_state == RuntimeState::Running) {
      m_state = RuntimeState::StopRequested;
    }
    m_state = RuntimeState::Draining;
  }

  m_executor.wait_for_all();

  std::lock_guard lock(m_state_mutex);
  m_state = RuntimeState::Stopped;
}

RuntimeState JobRuntime::State() const {
  std::lock_guard lock(m_state_mutex);
  return m_state;
}

std::uint32_t JobRuntime::WorkerCount() const noexcept {
  return m_worker_count;
}

void JobRuntime::ShutdownNoexcept() noexcept {
  try {
    Drain();
  } catch (...) {
    std::terminate();
  }
}

} // namespace ithax::jobs
