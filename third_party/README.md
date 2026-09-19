# Bundled MSVC dependencies

This directory contains the small release-only dependency set needed to build
Audio-Video after cloning the repository. It deliberately excludes vcpkg
sources, package build caches, debug libraries, PDB files, and installer
output.

- `vcpkg/installed/x64-windows` contains the OpenCV and Speex headers,
  import libraries, and runtime DLLs for the `x64-windows` MSVC ABI.
- `ffmpeg-master-latest-win64-lgpl-shared` contains the FFmpeg headers,
  import libraries, and runtime DLLs used by the client: `avcodec-63.dll`,
  `avutil-61.dll`, and `swscale-10.dll`.

The checked-in binaries are used only with Qt 6.11.1 MSVC 2022 64-bit. See
the root README for the required Visual Studio and Qt versions and the build
command.
