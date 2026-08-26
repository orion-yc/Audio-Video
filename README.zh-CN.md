# Audio-Video

[English](README.md) | 简体中文

Audio-Video 是一款面向小型实时会议的 Windows 桌面客户端。项目基于 Qt，提供房间会议、摄像头与桌面共享、语音、用户资料和轻量级 TCP 转发协议。

## 功能概览

- 创建房间，或通过房间号加入已有房间。
- 注册时可选填昵称和头像，也可以在“设置”中后续修改。
- 在房间内使用摄像头或桌面共享，并可在两者之间切换。
- 使用 H.264 视频编码，优先使用 NVIDIA NVENC；不可用时自动回退到 OpenH264 软件编码。
- 使用 Speex 进行语音传输，并提供连接延迟显示。
- 桌面共享可由共享者选择 720p、1080p、1440p、4K，以及 15、30、60 fps；客户端会自动匹配最高 56 Mbps 的 H.264 码率。摄像头采集目标为最高 1920 x 1080、60 fps。实际画质会受到采集源、显卡、CPU、网络和接收端设备的共同影响。

## 运行要求

- Windows 10 或 Windows 11，64 位
- 可访问兼容的 Audio-Video 服务端
- 音视频会议需要授予摄像头和麦克风权限
- NVIDIA 显卡及驱动为可选项。没有 NVENC 时，客户端会自动使用软件 H.264 编码。

## 安装与使用

有发布包时，可任选一种方式使用：

1. 运行 `Audio-Video-Setup-<version>.exe`，按安装向导完成安装。默认按当前用户安装，不需要管理员权限。
2. 或解压 `Audio-Video-Portable-<version>.zip`，运行 `Audio-Video.exe`。便携版中的所有文件和目录需要保持在一起。
3. 启动客户端后注册或登录，再创建房间或输入房间号加入会议。

未签名的构建在 Windows 中可能出现未知发布者提示；在使用商业代码签名证书之前，这是正常现象。

## 服务端配置

客户端读取 `Audio-Video.exe` 同目录下的 `config.ini`：

```ini
[Net]
host=www.onnotify.top
port=8000
```

服务部署在其他位置时，先修改该文件中的地址和端口再分发。每位参会者都需要能够与这里配置的主机和端口建立持续的 TCP 连接。

面向公网部署时，请阅读 [deploy/README.md](deploy/README.md)。其中包含 FRP 转发、数据库账号、常驻服务和防火墙的配置方式。当前客户端与公网入口之间使用明文 TCP 协议，应部署在受信网络中，或由部署侧提供传输层保护。

### 服务端兼容性

服务端负责房间和媒体包转发，不对视频重新编码。它需要保持现有的 TCP 外层长度帧、原样转发 H.264 负载，并允许最高 8 MiB 的媒体包，以支持 4K 桌面共享。自定义头像还依赖控制连接上的会话令牌校验，以及协议号 `10016` 至 `10019` 的上传、获取处理。

## 从源码构建

项目在 Windows 下使用 qmake 和 MinGW 构建。发布脚本会一并打包应用程序、Qt 运行库、OpenCV 运行库、FFmpeg 运行库和安装程序。

### 开发环境

- Qt 6.11.1，MinGW 64 位套件
- MinGW 13.1.0，64 位
- OpenCV 4.13.0，MinGW 版本
- Speex 1.2.1，MinGW 版本
- `third_party/ffmpeg-master-latest-win64-lgpl-shared` 中的 FFmpeg LGPL 共享运行库
- Inno Setup 6，用于生成 Windows 安装包

OpenCV 和 Speex 的默认本地路径分别在 [VideoApi/videoapi.pri](VideoApi/videoapi.pri) 与 [AudioApi/audioapi.pri](AudioApi/audioapi.pri) 中定义。SDK 安装在其他目录时，请先修改这两个变量。

在 PowerShell 中执行：

```powershell
.\packaging\build-release.ps1 -Version 1.0.10
```

如果本机目录不同，脚本支持传入 `-QtRoot`、`-MinGwRoot`、`-OpenCvRoot`、`-SpeexRoot` 和 `-FFmpegRoot`。构建成功后，`dist` 目录会生成：

- `Audio-Video-Setup-1.0.10.exe`
- `Audio-Video-Portable-1.0.10.zip`

在 `dev` 分支构建 MSVC 2022 发布版时，执行：

```powershell
.\packaging\build-release-msvc.ps1 -Version 1.0.10
```

该脚本会生成 `Audio-Video-MSVC-Setup-1.0.10.exe` 和 `Audio-Video-MSVC-Portable-1.0.10.zip`。首次执行会将 MSVC ABI 的依赖安装在项目内的 `third_party\vcpkg`。

H.264 编解码的独立冒烟测试工程位于 [tests/h264codec_smoke.pro](tests/h264codec_smoke.pro)。

## 目录说明

```text
AudioApi/       麦克风采集、播放与 Speex 集成
VideoApi/       摄像头与桌面采集、H.264 编解码、OpenCV 辅助功能
netapi/         TCP 客户端与协议定义
uiapi/          通用界面组件和应用样式
packaging/      运行库整理、安装脚本和发布构建脚本
deploy/         服务端、数据库和 FRP 部署模板
tests/          编解码冒烟测试
```

## 画质说明

客户端会先尝试通过 NVENC 进行 H.264 编码。显卡驱动、GPU 或编码会话不可用时，会自动切换到内置 OpenH264 软件编码，会议流程无需改变。为确保 H.264 视频兼容，建议房间内所有用户使用较新的客户端版本。

采集参数较高并不代表每次通话都能得到相同的清晰度。桌面画质还会受到共享屏幕的分辨率、源图缩放、本地编码性能、实际可用上行带宽、丢包率和接收端解码能力影响。共享文字内容时，建议关闭占用 GPU 的程序并使用稳定的有线网络。

## 第三方声明

发布包包含 `THIRD_PARTY_NOTICES.txt` 和 `THIRD_PARTY_LICENSES/FFmpeg-LGPL-3.0.txt`。重新分发便携版时，请保留这两个文件。

## 参与贡献

提交问题时，请说明客户端版本、Windows 版本、服务端地址、复现步骤和相关日志。提交代码时请保持改动聚焦，不要提交 `build-qt6` 或 `dist` 下的生成文件。
