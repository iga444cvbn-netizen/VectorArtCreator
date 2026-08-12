#include "core/evaluation/work_control.h"

#include <atomic>
#include <limits>
#include <mutex>
#include <utility>

namespace vt {

struct WorkControl::State {
    explicit State(qint64 maximum)
        : maximumUnits(qMax<qint64>(1, maximum))
    {
    }

    const qint64 maximumUnits;
    std::atomic<qint64> consumed{0};
    std::atomic<WorkControlStatus> status{WorkControlStatus::Running};
    std::mutex callbackMutex;
    std::function<void(qint64)> checkpointCallback;
};

WorkControl::WorkControl()
    : m_state(std::make_shared<State>(std::numeric_limits<qint64>::max()))
{
}

WorkControl::WorkControl(std::shared_ptr<State> state)
    : m_state(std::move(state))
{
}

WorkControl WorkControl::unlimited()
{
    return WorkControl(std::make_shared<State>(std::numeric_limits<qint64>::max()));
}

WorkControl WorkControl::withBudget(qint64 maximumUnits)
{
    return WorkControl(std::make_shared<State>(maximumUnits));
}

bool WorkControl::consume(qint64 units) const
{
    if (!m_state || units <= 0) {
        return isRunning();
    }
    qint64 previous = m_state->consumed.load(std::memory_order_acquire);
    for (;;) {
        if (!isRunning()) return false;
        if (previous >= m_state->maximumUnits
            || units > m_state->maximumUnits - previous) {
            WorkControlStatus expected = WorkControlStatus::Running;
            static_cast<void>(m_state->status.compare_exchange_strong(
                expected, WorkControlStatus::BudgetExceeded, std::memory_order_acq_rel));
            return false;
        }
        if (m_state->consumed.compare_exchange_weak(
                previous, previous + units, std::memory_order_acq_rel)) {
            break;
        }
    }

    std::function<void(qint64)> callback;
    {
        const std::lock_guard lock(m_state->callbackMutex);
        callback = m_state->checkpointCallback;
    }
    const qint64 consumed = previous + units;
    if (callback) {
        callback(consumed);
    }
    return isRunning();
}

void WorkControl::cancel() const
{
    if (!m_state) {
        return;
    }
    WorkControlStatus expected = WorkControlStatus::Running;
    static_cast<void>(m_state->status.compare_exchange_strong(
        expected, WorkControlStatus::Cancelled, std::memory_order_acq_rel));
}

WorkControlStatus WorkControl::status() const
{
    return m_state
        ? m_state->status.load(std::memory_order_acquire)
        : WorkControlStatus::Cancelled;
}

bool WorkControl::isRunning() const
{
    return status() == WorkControlStatus::Running;
}

qint64 WorkControl::unitsConsumed() const
{
    return m_state ? m_state->consumed.load(std::memory_order_acquire) : 0;
}

qint64 WorkControl::maximumUnits() const
{
    return m_state ? m_state->maximumUnits : 0;
}

QString WorkControl::interruptionMessage() const
{
    switch (status()) {
    case WorkControlStatus::Running:
        return {};
    case WorkControlStatus::Cancelled:
        return QStringLiteral("The operation was cancelled by a newer request.");
    case WorkControlStatus::BudgetExceeded:
        return QStringLiteral("The operation exceeded the bounded work budget.");
    }
    return QStringLiteral("Evaluation stopped.");
}

void WorkControl::setCheckpointCallback(std::function<void(qint64)> callback) const
{
    if (!m_state) {
        return;
    }
    const std::lock_guard lock(m_state->callbackMutex);
    m_state->checkpointCallback = std::move(callback);
}

} // namespace vt
