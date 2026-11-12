#include "track_publication_state.h"

bool TrackPublicationState::beginAttempt(qint64 nowMs) {
    if (m_ready || m_inFlight || exhausted() || nowMs < m_nextAttemptMs) return false;
    ++m_attempts;
    m_inFlight = true;
    return true;
}
void TrackPublicationState::succeeded() {
    m_ready = true;
    m_inFlight = false;
    m_attempts = 0;
    m_nextAttemptMs = 0;
}
void TrackPublicationState::failed(qint64 nowMs) {
    m_ready = false;
    m_inFlight = false;
    m_nextAttemptMs = nowMs + 1500;
}
void TrackPublicationState::reset() {
    m_ready = false;
    m_inFlight = false;
    m_attempts = 0;
    m_nextAttemptMs = 0;
}
