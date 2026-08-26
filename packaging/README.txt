Audio-Video 1.0.10
=================

System requirements
-------------------
- Windows 10 or Windows 11, 64-bit
- Internet access to www.onnotify.top:8000
- Camera and microphone for audio/video meetings
- For NVIDIA NVENC hardware encoding, an NVIDIA driver compatible with NVENC API 13.1

Installation
------------
Run Audio-Video-Setup-1.0.10.exe and follow the installer prompts.
The default installation is per-user and does not require administrator rights.

Portable version
----------------
Extract Audio-Video-Portable-1.0.10.zip, then run Audio-Video.exe.
Keep all extracted files and folders together.

Configuration
-------------
The server is configured in config.ini next to Audio-Video.exe:

[Net]
host=www.onnotify.top
port=8000

Notes
-----
- Video uses H.264. NVIDIA NVENC is selected first; if it is unavailable, the
  client automatically uses the bundled OpenH264 software encoder.
- Everyone in the room should use an Audio-Video client with H.264 support.
  This version can still receive JPEG video from earlier clients.
- The server only relays the video payload and needs no H.264 configuration.
- FFmpeg LGPL runtime notices are included in THIRD_PARTY_NOTICES.txt and
  THIRD_PARTY_LICENSES.
- Windows may display an unknown-publisher warning because the package is not
  signed with a commercial code-signing certificate.
- If camera or microphone access is blocked, enable desktop-app access in
  Windows privacy settings.
