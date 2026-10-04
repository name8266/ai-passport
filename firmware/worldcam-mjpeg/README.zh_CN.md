<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# WorldCam 实时直播固件

分支：`feature/worldcam-mjpeg`

固件：`FoloToy-AI-Passport-worldcam-real-live.bin`

这是 ESP32-C3 合并镜像，从 `0x0` 地址刷入。

Wi-Fi 连接后自动播放精选的公共 MJPEG 直播源，支持 UP/DOWN 切换直播源、
OK 重新连接以及直播源自动故障切换，并保留原有的 WorldCam 快照目录。

请在刷入前使用 `SHA256SUMS.txt` 校验固件。分支 CI 会根据对应源码生成固件。

## Wi-Fi 配网

首次启动时，用手机连接设备屏幕显示的 `WorldCam-XXXX` 热点。
配网热点默认开放，无需密码。浏览器打开 `http://192.168.4.1`，
填写你的 2.4 GHz Wi-Fi 名称和密码。以后可长按 OK 再次打开配网。

刷入此合并镜像会替换固件并清除 NVS 中已保存的设置，包括 Wi-Fi 配置。
