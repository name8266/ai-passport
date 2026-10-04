<p align="right"><a href="worldcam-mjpeg-test.md">English</a> · <strong>简体中文</strong></p>

# AI Passport MJPEG 实验分支测试

分支：`feature/worldcam-mjpeg`

这个分支不会修改原来的 `feature/worldcam`。它在原快照模式之外增加一个独立的 MJPEG 实验入口。

## 1. 启动可重复测试流

电脑与 Passport 接入同一局域网，在仓库根目录执行：

```bash
python3 tools/mjpeg_test_server.py --fps 5
```

终端会打印类似：

```text
MJPEG test stream: http://192.168.1.23:8080/stream.mjpg
```

测试流使用标准 `multipart/x-mixed-replace`，交替发送两张 240×160 baseline JPEG，不依赖 OpenCV、FFmpeg 或 Pillow。

## 2. 在 Passport 上测试

1. 长按 OK 进入 WorldCam 网络配置。
2. 手机连接设备热点并打开 `192.168.4.1`。
3. 填写 2.4 GHz Wi-Fi。
4. 在“MJPEG测试地址”填入电脑打印出的 `http://.../stream.mjpg`。
5. 保存。设备联网成功后会自动进入“MJPEG实时测试”。
6. 屏幕状态应连续显示 `LIVE · MJPEG · 帧N`，A/B 两帧持续切换。
7. 长按 OK 退出流媒体测试，回到原 WorldCam 地图。

## 当前边界

- 目标：验证 ESP32-C3 在真实长连接上持续接收和解码 MJPEG。
- 支持 HTTP/HTTPS、重定向、multipart MJPEG 和连续 JPEG 字节流。
- 只接受 baseline JPEG；渐进 JPEG 会明确报错。
- 单帧压缩数据上限 512 KiB。
- 当前仍使用 RGB565 完整帧缓冲，属于 POC；真机稳定后再考虑 strip/DMA 直接刷屏以进一步降低 RAM 峰值。
- MJPEG 中断后不会伪装成新画面；保留最后一帧并等待用户按 OK 重连。
