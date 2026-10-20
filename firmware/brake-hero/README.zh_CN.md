<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# Brake Hero 已验证完整固件

下载 `Brake-Hero-full.bin`，适用于 FoloToy AI Passport（ESP32-C3、8 MB Flash、240 × 320 屏幕）。这是合并完整镜像，从 **0x0** 刷写。合并刷写可能重置 NVS 记录，操作前请确认目标设备和数据影响，不要例行全片擦除。

- Build: PASS（ESP-IDF 5.5.3，合并镜像/分区及匹配 ELF 已验证）
- Host tests: PASS（包括 10,000 条轨迹、音频任务及实际 LVGL 解码）
- Device tests: NOT RUN
- Unverified: 真机 30 FPS、实体键与音频、运行时最小堆、NVS 断电恢复
- 静态/完整门禁：FAIL，原因是主线已有 WorldCam README 双语错误

文件大小：1,392,080 字节。SHA256：

`6128ab20f18f0db50d13d2aab459975abfb85579771b33bf0c175f5b4f939940`

本固件由本地未提交的 `feature/brake-hero` 工作区构建。本独立分支仅上传用户要求的固件、校验文件和说明，不发布或替换游戏源码，也不修改 main。本固件分支中其他位置的基线代码并非这份新游戏固件的源码。游戏设定虚构，不预测真实车辆表现。
