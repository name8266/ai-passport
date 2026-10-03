<p align="right"><a href="worldcam.md">English</a> · <strong>简体中文</strong></p>

# Passport 世界之窗：设备直连工程说明

世界之窗现在由 ESP32-C3 AI Passport 自己完成联网取图，**网关服务器不再是运行依赖**。

## 固件目录

维护数据包含 825 个摄像机记录和 1020 个地点。生成工具把 URL、特殊解析类型、坐标、状态标记、中文地点名和中文国家名编译到 `main/worldcam_catalog.c`，运行时直接从 Flash 读取。

修改 `gateway/cameras.json` 或 `gateway/locations.json` 后执行：

```bash
python3 tools/generate_worldcam_catalog.py
python3 tools/generate_worldcam_catalog.py --check
```

`gateway/` 这个历史目录名暂时保留，是为了维护和审计兼容；Passport 日常使用不需要启动其中的服务器。

## 网络与图片链路

```text
Wi-Fi STA
  → esp_http_client HTTPS
  → ESP-IDF 根证书包
  → 有上限的流式读取
  → jpeg_roi_decoder
  → RGB565 显示缓冲
  → LVGL
```

原始压缩 JPEG 不会整张缓存在 RAM。解码器使用 2 KiB 输入预取缓冲和 4 KiB 工作缓冲，RGB565 逐行输出；网络 worker 使用 16 KiB 栈，给 HTTPS/TLS 调用深度留出余量。

每个源最多读取 8 MiB。普通显示 192×128，全屏 240×160。旧版 `WCIX` / `WCAM` 网关二进制协议已经退出设备运行链路。

## Wi-Fi 配网

设备仍保留自身 AP 和 `192.168.4.1` 本地配置页，因为三键设备需要一个可靠的输入方式。页面现在只保存 SSID 和密码，不再要求网关地址。STA 成功联网后配置 AP 和本地 HTTP 服务自动关闭。

## 图片源处理

当前 825 个摄像机记录中，824 个直接使用 HTTPS JPEG。南极站 1 个来源先返回元数据，`wc_resolve_usap()` 在设备内严格校验其中 JPEG 文件名，再拼接美国南极科考计划官方 HTTPS 地址。

当前 JPEG 解码器不支持渐进式 JPEG；缩放只使用 1、1/2、1/4、1/8，并采用居中视口，所以不同长宽比的源可能裁切边缘。获取或解码失败时只显示错误，不把旧图冒充新图。

## 中国大陆直连测量

云端检测不能证明中国大陆网络实际可用。需要测某一条真实网络时，仍可选用维护工具：

```bash
python tools/probe_worldcam_network.py --vantage-label mainland-local --output WorldCam-mainland-report.json
```

它只是诊断工具，不是 Passport 运行所需组件。

## 验收状态

主机测试覆盖 Flash 目录生成、地点筛选/随机、USAP 元数据解析、UTF-8 和“无网关运行契约”。固件 CI 使用 ESP-IDF 5.5.3 和受管理的 JPEG 解码组件构建。

正式发布前仍需在真机验证：Wi-Fi 配网、目标网络 TLS 访问、堆/栈峰值、JPEG 颜色和裁切、按键手势、重启保留设置、下载中切换地点以及持续每 60 秒刷新。

设备每次连接 Wi-Fi 后会启动 SNTP，配置阿里、腾讯及 pool.ntp.org 时间服务器。冷启动后，网络任务最多等待 15 秒取得时间，再进行 HTTPS 证书校验；超时会显示错误，可按 OK 重试。所用网络需允许公共网络校时和摄像机地址访问。
