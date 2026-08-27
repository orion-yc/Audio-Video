# Audio-Video

[简体中文](README.zh-CN.md) | English

Audio-Video is a Windows desktop client for small real-time video meetings. It combines room-based calls, camera and desktop sharing, voice, profiles, and a lightweight TCP relay protocol in one Qt application.

## Highlights

- Create a room or join one with a room number.
- Register with an optional nickname and avatar; update the profile later from Settings.
- Share a camera or desktop and switch between them while in a room.
- H.264 video with NVIDIA NVENC selected first and an OpenH264 software fallback when NVENC is unavailable.
- Speex voice transport and a built-in connection-latency indicator.
- Desktop sharing lets the presenter choose 720p, 1080p, 1440p, or 4K and 15, 30, or 60 fps. It automatically selects a matching H.264 bitrate up to 56 Mbps; camera capture targets 1920 x 1080 at 60 fps. The delivered quality still depends on the source, GPU, CPU, network, and receiving device.

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
host=www.onnotify.top
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
.\packaging\build-release.ps1 -Version 1.0.14
```

The script accepts `-QtRoot`, `-MinGwRoot`, `-OpenCvRoot`, `-SpeexRoot`, and `-FFmpegRoot` when the defaults do not match the local machine. Successful builds place these artifacts in `dist`:

- `Audio-Video-Setup-1.0.14.exe`
- `Audio-Video-Portable-1.0.14.zip`

To build the MSVC 2022 release on the `dev` branch, run:

```powershell
.\packaging\build-release-msvc.ps1 -Version 1.0.14
```

This produces `Audio-Video-MSVC-Setup-1.0.14.exe` and `Audio-Video-MSVC-Portable-1.0.14.zip`. The script installs its MSVC ABI dependencies beneath `third_party\vcpkg` on first use.

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

H.264 is encoded with NVENC when the runtime can open it. If the driver, GPU, or encoder session cannot be used, the client falls back to the bundled OpenH264 encoder without changing the call flow. All participants should use a current client version for H.264 video interoperability.

High capture settings do not guarantee the same image quality in every call. Desktop resolution is constrained by the shared display, source scaling, local encoding capacity, available upload bandwidth, packet loss, and the receiver's decoding capacity. Close GPU-heavy applications and prefer a stable wired connection for the clearest desktop text.

## Third-Party Notices

Release packages include `THIRD_PARTY_NOTICES.txt` and `THIRD_PARTY_LICENSES/FFmpeg-LGPL-3.0.txt`. Preserve these files when redistributing a portable package.

## Contributing

Please open an issue with the client version, Windows version, server endpoint, reproduction steps, and relevant logs. Keep pull requests focused and avoid committing generated build output from `build-qt6` or `dist`.
