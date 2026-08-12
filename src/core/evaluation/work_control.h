#pragma once

#include <QString>
#include <QtGlobal>

#include <functional>
#include <memory>

namespace vt {

enum class WorkControlStatus {
    Running,
    Cancelled,
    BudgetExceeded,
};

// Copyable cooperative stop/budget handle shared by one evaluation or export.
// Expensive stages consume deterministic semantic work units rather than using
// wall-clock deadlines, so tests and production enforce the same contract.
class WorkControl final {
public:
    static constexpr qint64 DefaultEvaluationUnits = 8'000'000;

    WorkControl();
    [[nodiscard]] static WorkControl unlimited();
    [[nodiscard]] static WorkControl withBudget(
        qint64 maximumUnits = DefaultEvaluationUnits);

    [[nodiscard]] bool consume(qint64 units = 1) const;
    void cancel() const;
    [[nodiscard]] WorkControlStatus status() const;
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] qint64 unitsConsumed() const;
    [[nodiscard]] qint64 maximumUnits() const;
    [[nodiscard]] QString interruptionMessage() const;

    // Deterministic test seam. Production leaves this empty. The callback is
    // invoked after a successful checkpoint and may coordinate external
    // cancellation without sleeps or timing thresholds.
    void setCheckpointCallback(std::function<void(qint64)> callback) const;

private:
    struct State;
    explicit WorkControl(std::shared_ptr<State> state);
    std::shared_ptr<State> m_state;
};

} // namespace vt
