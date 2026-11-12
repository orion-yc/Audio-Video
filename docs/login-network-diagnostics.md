# 登录与延迟诊断（2.0.2）

正常启动客户端即自动记录。当前用户的日志目录为
`%APPDATA%\Audio-Video\diagnostics`，文件名含启动时间。
房间右上角的诊断日志按钮可以打开目录。日志通过已有后台写入线程保存，
继续使用每文件 10 MiB、最多 5 个文件的滚动限制，不自动上传。

## 左上角数值

- **网关**：登录页和大厅显示 HTTP 健康检查总耗时，包括 DNS、TCP、TLS、
  服务处理及响应传输。每 5 秒检测，最多同时一个请求，4 秒未完成即失败。
  1000 ms 以上不会被压低或平滑；它不能直接说明音视频延迟也超过 1 秒。
- **媒体**：房间内显示 LiveKit 当前选中的 ICE 连接 RTT，取发布与接收连接较高值；
  没有有效 ICE 样本时回退到 RTCP 往返时间。即使没有开启共享也会查询统计。
- 请求失败显示“不可用”，无媒体样本或重连时显示 `--`，避免继续显示旧值。
  媒体样本超过 5 秒未刷新，会在下一次定时检查时清除。

## 登录失败

单次登录请求最多等待 8 秒。网络超时、连接中断或暂时的 502/503/504、520–524
响应会在 250 ms 后重试一次；保留原有 20 秒总期限。密码错误、TLS 失败不会重试。
注册、修改资料、创建/加入房间等操作不自动重放。
取消或超时会中止旧请求和计划中的重试，旧响应不会再次登录。

`gateway_request` 事件记录 `operation`、`host`、`port`、`attempt`、`http_status`、
`network_error`、`elapsed_ms`、`timed_out`、`success`、`cancelled` 和最后的 `phase`。
额外记录 Cloudflare Ray ID，便于服务端查对应请求。

`tcp_connect_ms`、`tls_ready_ms`、`request_sent_ms`、`response_headers_ms`
都是从本次请求开始累计的时间点，不能当成各阶段独立耗时。
复用连接时不一定再次触发连接/TLS 事件，缺少字段不代表对应阶段耗时为零。
`configured_proxy_type` 是 Qt 管理器配置的代理类型；默认代理类型不等于没有系统代理。

- 连接/TLS 时间点很晚：检查 DNS、网络路径、系统代理及证书协商。
- 请求已提交，但很久没有响应头：检查到网关的链路、CDN 回源及后端服务。
- HTTP 520–524：结合 Ray ID 检查 CDN 和源站；客户端重试不能修复源站持续故障。
- 网关很慢而媒体 RTT 很低：重点查控制面请求链路。
- 媒体 RTT 也很高：继续结合 `desktop_share_second` 的丢包、码率、采集和编码耗时定位。

诊断不记录手机号、密码、令牌、请求/响应正文、Authorization 头或聊天内容。
`media_latency` 事件仅包含本次媒体 RTT，`-1` 表示没有有效样本。

## 轨道发布超时与共享停滞

`livekit_connect_started/succeeded`、`livekit_audio_track_publish_started/succeeded`
分别记录连接和音频发布；`livekit_start_failed` 的 `phase` 区分失败阶段，
并记录尝试次数及该阶段耗时。音频发布超时会清理旧连接，在 1 秒后重新连接一次；
退出房间会取消待执行的重试。

`livekit_video_track_publish_started/succeeded` 和 `livekit_track_publish_error`
记录视频发布结果。视频发布超时最多尝试 3 次，每次失败结束后等待至少 1.5 秒；
没有发布成功的源不接收编码帧。持续超时仍需结合服务端日志检查确认响应。

共享状态中的“入队”表示 LiveKit 接受帧，不代表网络发送成功；
`desktop_share_second` 的网络发送帧数与码率单独取自 SDK 统计。
等待媒体连接时不会触发编码/发送停滞恢复。关键帧反馈请求现有编码器输出 IDR，
不再每次重建编码器；分辨率、目标帧率和目标码率配置保持原设置。

## 验证

运行 `tests/run-gateway-recovery.ps1` 检查模拟服务的超时、重试、取消、探测恢复和凭据脱敏；
运行 `tests/run-network-recovery.ps1` 检查登录按钮恢复和界面指标来源。
运行 `tests/run-h264-smoke.ps1 -RequireNvenc` 检查硬件编码、强制关键帧和晚加入解码。
实际验收时保持相同分辨率、目标帧率和码率，比较相同操作下的媒体 RTT、丢包、
采集/编码 FPS 以及阶段 P95。网关读数和媒体读数应分别比较，不能把指标切换当成性能提升。
