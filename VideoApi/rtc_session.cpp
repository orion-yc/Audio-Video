#include "rtc_session.h"

RtcSession::RtcSession(QObject *parent)
    : QObject(parent)
{
}

RtcSession::~RtcSession()
{
    stop();
}

bool RtcSession::isAvailable()
{
#if defined(AUDIO_VIDEO_LIVEKIT_SDK) && defined(AUDIO_VIDEO_LIVEKIT_NATIVE_ADAPTER)
    return true;
#else
    return false;
#endif
}

bool RtcSession::start(const Config &config)
{
    if (!isAvailable()) {
        m_state = State::Failed;
        Q_EMIT SIG_stateChanged(m_state,
                                QStringLiteral("LiveKit C++ SDK is not linked"));
        return false;
    }

    if (!config.gatewayUrl.isValid() || config.roomId.isEmpty() ||
            config.participantIdentity.isEmpty() || config.liveKitToken.isEmpty()) {
        m_state = State::Failed;
        Q_EMIT SIG_stateChanged(m_state, QStringLiteral("Invalid RTC configuration"));
        return false;
    }

    // The SDK-specific adapter is deliberately isolated behind this boundary.
    // A library search path alone is not enough to enable media; the adapter
    // must explicitly provide the ABI bridge and be compiled in.
    m_state = State::Connecting;
    Q_EMIT SIG_stateChanged(m_state, QStringLiteral("Starting LiveKit session"));
    return false;
}

void RtcSession::stop()
{
    if (m_state != State::Disconnected) {
        m_state = State::Disconnected;
        Q_EMIT SIG_stateChanged(m_state, QStringLiteral("RTC session stopped"));
    }
}

RtcSession::State RtcSession::state() const
{
    return m_state;
}
