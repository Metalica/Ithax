#include "ecs/ecs_world.h"

#include <algorithm>
#include <limits>

namespace {

constexpr std::int64_t DEFAULT_VELOCITY = 1;
constexpr std::uint64_t HASH_OFFSET = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t HASH_PRIME = 1'099'511'628'211ULL;

std::size_t PartitionStart(
    const std::size_t size, const std::size_t index, const std::size_t count) {
  const auto base = size / count;
  const auto remainder = size % count;
  return index * base + std::min(index, remainder);
}

std::size_t PartitionEnd(
    const std::size_t size, const std::size_t index, const std::size_t count) {
  const auto start = PartitionStart(size, index, count);
  const auto base = size / count;
  const auto remainder = size % count;
  return start + base + (index < remainder ? 1U : 0U);
}

std::int64_t CheckedAdd(
    const std::int64_t value, const std::int64_t delta) {
  const auto maximum = std::numeric_limits<std::int64_t>::max();
  const auto minimum = std::numeric_limits<std::int64_t>::min();
  if ((delta > 0 && value > maximum - delta) ||
      (delta < 0 && value < minimum - delta)) {
    throw ithax::ecs::EcsError("component update overflowed");
  }
  return value + delta;
}

std::uint64_t MixHash(
    const std::uint64_t hash, const std::uint64_t value) noexcept {
  return (hash ^ value) * HASH_PRIME;
}

} // namespace

namespace ithax::ecs {

EcsWorld::EcsWorld() : m_owner_thread(std::this_thread::get_id()) {}

void EcsWorld::AssertOwner() const {
  if (std::this_thread::get_id() != m_owner_thread) {
    throw EcsOwnershipError("ECS access was attempted off the owner thread");
  }
}

void EcsWorld::CreateEntities(const std::size_t count) {
  AssertOwner();
  const auto current_count = m_entity_count;
  if (count > MAX_WORLD_ENTITIES - current_count) {
    throw EcsError("entity count exceeds its bound");
  }

  m_entity_order.reserve(current_count + count);
  for (std::size_t index = 0U; index < count; ++index) {
    const auto entity = m_registry.create();
    const auto value = static_cast<std::int64_t>(current_count + index);
    m_registry.emplace<TransformComponent>(entity, TransformComponent{value});
    m_registry.emplace<VelocityComponent>(
        entity, VelocityComponent{DEFAULT_VELOCITY});
    m_entity_order.push_back(entity);
  }
  m_entity_count += count;
}

WorldSnapshot EcsWorld::Snapshot() const {
  AssertOwner();
  WorldSnapshot snapshot;
  CopySnapshot(snapshot);
  return snapshot;
}

void EcsWorld::CopySnapshot(WorldSnapshot &snapshot) const {
  AssertOwner();
  snapshot.states.clear();
  snapshot.states.reserve(m_entity_order.size());
  for (const auto entity : m_entity_order) {
    snapshot.states.push_back({
        entity,
        m_registry.get<TransformComponent>(entity),
        m_registry.get<VelocityComponent>(entity),
    });
  }
}

EcsJournal BuildTransformJournal(
    const WorldSnapshot &snapshot,
    const std::size_t worker_index,
    const std::size_t worker_count) {
  EcsJournal journal;
  BuildTransformJournalInto(
      snapshot, worker_index, worker_count, journal);
  return journal;
}

void BuildTransformJournalInto(
    const WorldSnapshot &snapshot,
    const std::size_t worker_index,
    const std::size_t worker_count,
    EcsJournal &journal) {
  if (worker_count == 0U || worker_count > MAX_WORLD_WORKERS ||
      worker_index >= worker_count) {
    throw EcsError("journal worker index is outside its bound");
  }

  const auto begin = PartitionStart(
      snapshot.states.size(), worker_index, worker_count);
  const auto end = PartitionEnd(
      snapshot.states.size(), worker_index, worker_count);
  journal.worker_index = worker_index;
  journal.transforms.clear();
  journal.transforms.reserve(end - begin);
  for (std::size_t index = begin; index < end; ++index) {
    const auto &state = snapshot.states[index];
    journal.transforms.push_back({
        state.entity,
        CheckedAdd(state.transform.value, state.velocity.delta),
        index - begin,
    });
  }
}

void EcsWorld::MergeJournals(std::vector<EcsJournal> &journals) {
  AssertOwner();
  if (journals.empty() || journals.size() > MAX_WORLD_WORKERS) {
    throw EcsError("journal count is outside its bound");
  }

  std::sort(
      journals.begin(), journals.end(),
      [](const EcsJournal &left, const EcsJournal &right) {
        return left.worker_index < right.worker_index;
      });

  std::size_t total_updates = 0U;
  for (const auto &journal : journals) {
    if (journal.worker_index >= journals.size()) {
      throw EcsError("journal worker indexes are not contiguous");
    }
    if (journal.transforms.size() > MAX_JOURNAL_UPDATES - total_updates) {
      throw EcsError("journal update count exceeds its bound");
    }
    total_updates += journal.transforms.size();
  }

  if (total_updates != m_entity_order.size()) {
    throw EcsError("journals did not cover the complete world snapshot");
  }

  std::size_t update_offset = 0U;
  for (std::size_t journal_index = 0U;
       journal_index < journals.size();
       ++journal_index) {
    const auto &journal = journals[journal_index];
    if (journal.worker_index != journal_index) {
      throw EcsError("journal worker indexes are not contiguous");
    }
    for (std::size_t update_index = 0U;
         update_index < journal.transforms.size();
         ++update_index) {
      const auto &update = journal.transforms[update_index];
      if (update.sequence != update_index ||
          !m_registry.valid(update.entity) ||
          !m_registry.all_of<TransformComponent>(update.entity) ||
          update_offset >= m_entity_order.size() ||
          update.entity != m_entity_order[update_offset]) {
        throw EcsError("journal update does not match stable world state");
      }
      m_registry.get<TransformComponent>(update.entity).value = update.value;
      ++update_offset;
    }
  }
}

std::size_t EcsWorld::EntityCount() const {
  AssertOwner();
  return m_entity_count;
}

std::uint64_t EcsWorld::StateHash() const {
  const auto snapshot = Snapshot();
  auto hash = HASH_OFFSET;
  for (const auto &state : snapshot.states) {
    hash = MixHash(hash, entt::to_integral(state.entity));
    hash = MixHash(hash, static_cast<std::uint64_t>(state.transform.value));
    hash = MixHash(hash, static_cast<std::uint64_t>(state.velocity.delta));
  }
  return hash;
}

} // namespace ithax::ecs
