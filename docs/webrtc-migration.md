# HTTP / LiveKit integration

The client default is `transport=webrtc`; explicit `transport=tcp` preserves the old transport.
The selected mode is loaded before any control connection is opened.

## Runtime configuration

```ini
[Net]
transport=webrtc
gateway=https://video.onnotify.top
livekit=wss://video.onnotify.top
```

The complete `room-xxxxxxxx` ID is shown in the title and can be selected/copied. The numeric
`m_roomid` is only an active-room marker in WebRTC mode; it is never sent to the HTTP API.
The gateway and media adapter retain the actual room string.

`GatewayClient` implements account registration/login, JWT authorization, profile updates,
JPEG avatars, room creation/join/leave, and authorized WebSocket room chat. Requests have
bounded deadlines (8 seconds per login attempt, 4 seconds for health checks, 15 seconds
for other requests), reject redirects, and cancel stale authentication requests. A transient
login failure is retried once within the existing 20-second UI deadline. Tokens
and passwords are not persisted. A session-expiration response returns the user to login.
JWTs are never truncated into the old 33-byte TCP token field.

`RtcSession` uses the official native C++ SDK 1.13.0 (pinned hash in the installer). SDK calls,
publication and capture run on a worker thread. Existing H.264 access units are published
through EncodedVideoSource; no second H.264 encoder is introduced. Video feedback requests
keyframes and provides encoder bitrate targets. The source is republished when the chosen
resolution/frame rate/bitrate/source changes. Outbound encoded video is bounded to one
pending capture, and incoming video uses a latest-frame mailbox per participant.

Microphone and WASAPI loopback are mixed as 48 kHz mono PCM. Twenty-millisecond mixer
frames are split into two ten-millisecond submissions to the low-latency native source.
LiveKit uses Opus for transport. Playback uses a bounded 48 kHz output buffer. Camera,
desktop, selected system-output device, local previews and the existing chat UI are reused.
LiveKit handles transient connection recovery and refreshes its room token; a final
unrecoverable media disconnect returns to the lobby instead of leaving a false connected UI.

## Server dependency

The gateway's HTTP logger must preserve `http.Hijacker` for WebSocket upgrades. The server
change in Audio-Video_Server-go fixes this and rechecks membership when sending/receiving
chat, omits sender echoes, and subscribes before returning the WebSocket handshake.
A normal API health check alone does not verify WebSocket or media support.

The existing HTTP gateway stores membership in Redis. A graceful leave removes it; a
client crash can leave membership until the configured room TTL. Native LiveKit presence
is used for the visible participant roster. Immediate Redis cleanup after process crashes
would require server-side LiveKit lifecycle reconciliation/webhooks.

## Validation

Native Linux build of the shared HTTP/RTC modules, C++ syntax checking of all changed UI,
control and audio sources, and two real SDK sessions against the deployed HTTPS/WSS domain.
The smoke test covers registration/login, profile, JPEG upload/fetch, bidirectional Unicode
chat, and a late subscriber decoding H.264 video and receiving Opus audio. It uses a small
synthetic IDR fixture, not real Windows desktop capture or a 4K performance benchmark.

Windows/MSVC packaging, Windows microphone/camera/WASAPI devices, real two-machine
network interruption and 4K/60 fps/72 Mbps are still untested in this Linux environment.

Build on Windows with `packaging/build-release-msvc.ps1 -Version 2.0.1`.
Run `tests/run-webrtc-integration.ps1` for native integration. It generates temporary users;
use its JSON report to clean only those accounts and their avatar metadata/files.

Latest deployed-domain run passed: 41 decoded 64×64 H.264 fixture frames and 164 decoded audio frames, including non-silent PCM. Temporary users/avatars/room data were removed afterwards.
