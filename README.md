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

The project is built with qmake and MinGW on Windows. The release script packages the application, Qt runtime, OpenCV runtime, FFmpeg runtime, and installer.

### Toolchain

- Qt 6.11.1 for MinGW 64-bit
- MinGW 13.1.0, 64-bit
- OpenCV 4.13.0 for MinGW
- Speex 1.2.1 for MinGW
- FFmpeg shared LGPL runtime in `third_party/ffmpeg-master-latest-win64-lgpl-shared`
- Inno Setup 6 to produce the Windows installer

The default local paths for OpenCV and Speex are declared in [VideoApi/videoapi.pri](VideoApi/videoapi.pri) and [AudioApi/audioapi.pri](AudioApi/audioapi.pri). Update those two variables when your SDKs are installed elsewhere.

Build a release package from PowerShell:

```powershell
.\packaging\build-release.ps1 -Version 1.1.6
```

The script accepts `-QtRoot`, `-MinGwRoot`, `-OpenCvRoot`, `-SpeexRoot`, and `-FFmpegRoot` when the defaults do not match the local machine. Successful builds place these artifacts in `dist\MinGW`:

- `Audio-Video-Setup-1.1.6.exe`
- `Audio-Video-Portable-1.1.6.zip`

To build the MSVC 2022 release on the `dev` branch, run:

```powershell
.\packaging\build-release-msvc.ps1 -Version 1.1.6
```

This produces `dist\MSVC\Audio-Video-MSVC-Setup-1.1.6.exe` and `dist\MSVC\Audio-Video-MSVC-Portable-1.1.6.zip`. The script installs its MSVC ABI dependencies beneath `third_party\vcpkg` on first use.

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

H.264 is encoded with NVENC when the runtime can open it. If NVENC cannot be used, the client tries Windows Media Foundation with hardware encoding before falling back to bundled OpenH264. Desktop sharing replaces queued stale frames with the newest frame, so a busy sender preserves responsiveness instead of building delay. The share banner reports real one-second capture, encode, and send FPS plus deliberate frame drops. All participants should use a current client version for H.264 video interoperability.

High capture settings do not guarantee the same image quality in every call. Desktop resolution is constrained by the shared display, source scaling, local encoding capacity, available upload bandwidth, packet loss, and the receiver's decoding capacity. Close GPU-heavy applications and prefer a stable wired connection for the clearest desktop text.

System sound uses Windows WASAPI loopback on the selected playback endpoint and is mixed into the existing 8 kHz mono Speex voice frames. It is intended for meeting audio and notification sounds rather than high-fidelity music. Loopback captures the endpoint's complete system mix, so use headphones or a separate output endpoint to avoid feeding remote audio back into the meeting.

## Third-Party Notices

Release packages include `THIRD_PARTY_NOTICES.txt` and `THIRD_PARTY_LICENSES/FFmpeg-LGPL-3.0.txt`. Preserve these files when redistributing a portable package.

## Contributing

Please open an issue with the client version, Windows version, server endpoint, reproduction steps, and relevant logs. Keep pull requests focused and avoid committing generated build output from `build-qt6` or `dist`.

## Desktop-sharing performance and connection safeguards in 1.1.6

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

After a MinGW release build, run `tests/run-network-recovery.ps1` to exercise
registration and login deadlines against the actual client objects using Qt's
offscreen platform and a local test listener. It never connects to the configured
production server. This is not a two-client screen-sharing end-to-end test.
