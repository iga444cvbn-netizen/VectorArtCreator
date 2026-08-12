#include "tests/support/async_evaluation_gate.h"

#include <QThreadPool>
#include <QtConcurrentRun>

namespace vt::test {

AsyncEvaluationGate::AsyncEvaluationGate()
{
    QThreadPool* pool = QThreadPool::globalInstance();
    m_previousMaxThreadCount = pool->maxThreadCount();
    pool->setMaxThreadCount(1);
    m_blocker = QtConcurrent::run(pool, [this] {
        m_started.release();
        m_release.acquire();
    });
}

AsyncEvaluationGate::~AsyncEvaluationGate()
{
    release();
    m_blocker.waitForFinished();
    QThreadPool::globalInstance()->setMaxThreadCount(m_previousMaxThreadCount);
}

bool AsyncEvaluationGate::waitUntilHolding(int timeoutMs)
{
    if (m_startedObserved) return true;
    m_startedObserved = m_started.tryAcquire(1, timeoutMs);
    return m_startedObserved;
}

void AsyncEvaluationGate::release()
{
    if (m_released) return;
    m_released = true;
    m_release.release();
}

} // namespace vt::test
