# Audio-Video

[简体中文](README.zh-CN.md) | English

Audio-Video is a Windows desktop client for small real-time video meetings. It combines room-based calls, camera and desktop sharing, voice, profiles, and a lightweight TCP relay protocol in one Qt application.

## Highlights

- Create a room or join one with a room number.
- Register with an optional nickname and avatar; update the profile later from Settings.
- Share a camera or desktop and switch between them while in a room.
- H.264 video with NVIDIA NVENC selected first, Windows Media Foundation hardware encoding second, and an OpenH264 software fallback.
- The viewer automatically focuses the first remote video stream and restores its media subscriptions after a connection drop.
- Speex voice transport and a built-in connection-latency indicator.
- Optional Windows WASAPI loopback capture during desktop sharing, with a choice of the default or a specific active playback device; the system stream is mixed into the existing microphone transport.
- Desktop sharing lets the presenter choose 720p, 1080p, 1440p, or 4K and 15, 30, or 60 fps. It automatically selects a matching H.264 bitrate up to 72 Mbps; camera capture targets 1920 x 1080 at 60 fps. The delivered quality still depends on the source, GPU, CPU, network, and receiving device.

## Requirements

- Windows 10 or Windows 11, 64-bit
- Access to a compatible Audio-Video server
- Camera and microphone permissions for audio/video calls
- An NVIDIA GPU and current driver are optional. Systems without NVENC automatically use software H.264 encoding.

## Install and Run

Use a release asset when one is available:

1. Run `Audio-Video-Setup-<version>.exe` for a normal per-user installation. Administrator rights are not required by the installer.
2. Or extract `Audio-Video-Portable-<version>.zip` and run `Audio-Video.exe`. Keep every extracted file and directory together.
3. Start the client, register or sign in, then create a room or enter an existing room number.

Windows may show an unknown-publisher warning for unsigned builds. This is expected until the release executable is code-signed.

## Server Configuration

The desktop client reads `config.ini` next to `Audio-Video.exe`:

```ini
[Net]
host=8.163.3.67
port=8000
```

Change the endpoint before distribution when the service is deployed elsewhere. The client needs one persistent TCP connection to the configured host and port; make sure the endpoint is reachable from every participant's network.

For an Internet-facing deployment, follow [deploy/README.md](deploy/README.md). It documents the supported FRP layout, database credentials, service setup, and firewall boundaries. The current public-client protocol is plaintext TCP, so it should be placed behind a trusted network or a transport layer protected by your deployment.

### Server Compatibility

The server routes room and media packets and does not transcode video. It must forward H.264 payloads unchanged, support the current outer length-prefixed TCP framing, and permit media payloads up to 8 MiB for the 4K sharing option. Custom avatars additionally require the avatar request types `10016` through `10019` and session-token validation on the control connection.

## Build From Source

Audio-Video supports one build target: 64-bit MSVC. The repository includes the release OpenCV, Speex, and FFmpeg files required to compile and package the client. Build output and debug symbols are intentionally not committed.

### Required Local Toolchain

- Visual Studio 2026 Community or newer, with the Desktop development with C++ workload and the MSVC x64 toolset
- Qt 6.11.1 MSVC 2022 64-bit at `E:\App\Qt\6.11.1\msvc2022_64`, or pass its location with `-QtRoot`
- Inno Setup 6 only when generating the installer

Qt and Visual Studio are not stored in this repository. All other project-specific native dependencies are checked in below `third_party`:

- `third_party\vcpkg\installed\x64-windows` — release OpenCV and Speex headers, libraries, and runtime DLLs
- `third_party\ffmpeg-master-latest-win64-lgpl-shared` — FFmpeg headers, import libraries, and the four runtime DLLs required by the client

After cloning, build the release package from PowerShell:

```powershell
.\packaging\build-release-msvc.ps1 -Version 1.1.12
```

The generic `build-release.ps1` entry point runs the same MSVC build. Successful builds place only these artifacts in `dist\MSVC`:

- `Audio-Video-MSVC-Setup-1.1.12.exe`
- `Audio-Video-MSVC-Portable-1.1.12.zip`

The H.264 encoder/decoder smoke project is available at [tests/h264codec_smoke.pro](tests/h264codec_smoke.pro).

## Project Layout

```text
AudioApi/       Microphone capture, playback, and Speex integration
VideoApi/       Camera and desktop capture, H.264 encode/decode, OpenCV helpers
netapi/         TCP client and protocol definitions
uiapi/          Shared widgets and application styling
packaging/      Runtime staging, installer script, and release builder
deploy/         Server, database, and FRP deployment templates
tests/          Focused codec smoke test
```

## Media Notes

H.264 is encoded with NVENC when the runtime can open it. If NVENC cannot be used, the client tries Windows Media Foundation with hardware encoding before falling back to bundled OpenH264. Desktop sharing replaces queued stale frames with the newest frame, so a busy sender preserves responsiveness instead of building delay. The share banner reports real one-second capture, encode, and send FPS plus deliberate frame drops. A stalled capture, encoder, or video channel is recovered automatically, and each new share refreshes video-channel registration. All participants should use a current client version for H.264 video interoperability.

High capture settings do not guarantee the same image quality in every call. Desktop resolution is constrained by the shared display, source scaling, local encoding capacity, available upload bandwidth, packet loss, and the receiver's decoding capacity. Close GPU-heavy applications and prefer a stable wired connection for the clearest desktop text.

System sound uses Windows WASAPI loopback on the selected playback endpoint and is mixed into the existing 8 kHz mono Speex voice frames. It is intended for meeting audio and notification sounds rather than high-fidelity music. Loopback captures the endpoint's complete system mix, so use headphones or a separate output endpoint to avoid feeding remote audio back into the meeting.

## Third-Party Notices

Release packages include `THIRD_PARTY_NOTICES.txt` and `THIRD_PARTY_LICENSES/FFmpeg-LGPL-3.0.txt`. Preserve these files when redistributing a portable package.

## Contributing

Please open an issue with the client version, Windows version, server endpoint, reproduction steps, and relevant logs. Keep pull requests focused and avoid committing generated build output from `build-qt6` or `dist`.

## Desktop-sharing channel recovery in 1.1.12

Desktop sharing now uses a new dedicated video connection for every room entry, new share, and recovery. Media acknowledgements and disconnect notifications are marshalled to the UI thread and tied to the socket that produced them, so a delayed callback from a retired connection cannot stop a newly authenticated share. Once accepted, the client restarts the encoder to guarantee that the first transmitted frame is an IDR frame, so another participant can begin decoding immediately. Click the latency badge in the upper-left corner to reveal or hide the detailed sharing status. The MSVC package also includes `swresample-7.dll`, required by the bundled FFmpeg encoder.

The sender preserves a queued H.264 key frame until it has been written to the
video channel. Later delta frames may still be replaced to keep latency low,
but can no longer overwrite the frame a newly joined viewer needs to start
decoding. The mouse pointer is composited into shared-desktop pixels by default;
viewers require no extra setting or notification. The room-number dialog uses
black digits on its input field for reliable readability.

## Desktop-sharing recovery and connection safeguards

The default client endpoint is `8.163.3.67:8000`. The previous hostname is
currently routed through a Cloudflare HTTP proxy and cannot carry this raw TCP
protocol. Update the adjacent `config.ini` only when the server endpoint changes.

Media registration supports both server variants. A legacy server begins relay
after the 10013 / 10014 registration request and sends no response; a newer
server may additionally return 10021 / 10022. The client opens the channel
after the registration write and reconnects only if TCP fails or an optional
matching response explicitly rejects it.

Login has a 20-second overall deadline. Media channels reconnect after TCP
failure, and replies from older login sessions are ignored. After control
reauthentication the client explicitly rejoins the previous room; if that room
no longer exists, the normal join error is shown. Video reconnection resets the
codec state, and the meeting window shows connection and receive status.

The client now asserts the exact layout of registration, room and profile
packets at build time, preventing a compiler or field-order change from silently
breaking compatibility with the server.

## Transport security

The current protocol is raw TCP and is not protected by TLS. Until a TLS
certificate and matching server deployment are introduced, run it only over a
trusted network or VPN and restrict the server firewall to known client IPs.

After an MSVC release build, run `tests/run-network-recovery.ps1` to exercise
registration and login deadlines against the actual client objects using Qt's
offscreen platform and a local test listener. It never connects to the configured
production server. This is not a two-client screen-sharing end-to-end test.
