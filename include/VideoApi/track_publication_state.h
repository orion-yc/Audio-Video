#pragma once
#include <QtGlobal>

// Construct/capture a source only after this publication attempt is eligible/successful.
class TrackPublicationState {
public:
    bool beginAttempt(qint64 nowMs);
    void succeeded();
    void failed(qint64 nowMs);
    void reset();
    bool ready() const { return m_ready; }
    bool exhausted() const { return m_attempts >= 3; }
    int attempts() const { return m_attempts; }
private:
    bool m_ready = false;
    bool m_inFlight = false;
    int m_attempts = 0;
    qint64 m_nextAttemptMs = 0;
};
