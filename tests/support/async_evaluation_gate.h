#pragma once

#include <QFuture>
#include <QSemaphore>

namespace vt::test {

// Deterministically occupies the global QtConcurrent pool so controller
// evaluations queue behind an explicit barrier. Tests can author A and B
// snapshots without relying on shaping speed or sleeps, then release workers
// and assert that only B is published.
class AsyncEvaluationGate final {
public:
    AsyncEvaluationGate();
    ~AsyncEvaluationGate();

    AsyncEvaluationGate(const AsyncEvaluationGate&) = delete;
    AsyncEvaluationGate& operator=(const AsyncEvaluationGate&) = delete;

    [[nodiscard]] bool waitUntilHolding(int timeoutMs = 5000);
    void release();

private:
    int m_previousMaxThreadCount = 1;
    QSemaphore m_started;
    QSemaphore m_release;
    QFuture<void> m_blocker;
    bool m_startedObserved = false;
    bool m_released = false;
};

} // namespace vt::test
