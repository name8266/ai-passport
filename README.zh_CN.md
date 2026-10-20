<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# Brake Hero 2.0 极限刹停王

FoloToy AI Passport 离线刹车游戏：ESP32-C3、8 MB Flash、240 × 320 屏幕，无 PSRAM。

第三人称跟车视角展示原创的尊界 V800 外观低多边形 MPV 模型。车顶帐篷随实际开度连续升起及收回；有效制动时尾灯点亮；碰撞呈现随游戏撞击速度变化的车身变形、玻璃裂纹、碎片和扬尘，坠崖有下落动画。这些是游戏效果，不是真实车辆的碰撞预测。

## 操作

| 实体按键 | 驾驶时作用 |
| --- | --- |
| 上键 | 打开车顶帐篷 |
| 下键 | 增大刹车力度 |
| 确定键 | 增大油门 |

按住增加，松开后各控制量独立回落。硬件采用单路 ADC 分压按键，不支持组合键。驾驶提示均使用中文，包括车顶帐篷开度、刹车力度、油门、剩余距离和制动失效。

在有限的 500 米赛道内加速，然后停在障碍前。踏板力超过虚构的 1600 N 阈值后，本局刹车永久断裂，仍可用帐篷补救。峰值速度至少 100 km/h 才能进入本机前十。成绩采用带版本及 CRC 的 NVS 记录。

## 构建与验证

使用 ESP-IDF 5.5.3 和锁定的托管依赖：

```bash
source /path/to/esp-idf-v5.5.3/export.sh
./tools/test_brake_hero.sh
./tools/validate.sh --firmware
./tools/validate.sh
```

完整门禁仍会遇到基线已有的 `worldcam-site/README.md` 双语文档错误，未跳过或屏蔽。独立固件门禁使用版本控制中的默认配置，构建并合并镜像，保留匹配 ELF/MAP 归档。只下载完整合并镜像，确认设备及数据影响后从 **0x0** 刷写；可能重置原 NVS 设置，不要例行整片擦除。

## 声音与渲染

优先级 5 的唯一音频任务负责 ES8311，每次送入 20 ms PCM。优先级 2 的渲染任务采用一份索引帧和一份 40 行 DMA 缓冲，等待 SPI 完成后再复用。运行时不再整屏执行 LVGL 图像解码，也不长时间持有 LVGL 锁。物理按时间戳以 60 Hz 推进，与绘制帧率分离。诊断输出实际完成帧数、堆、丢失输入和播放片段内的最大 PCM 送数间隔。真机帧率与实际听感仍需设备验收。

详见[工程说明](docs/apps/brake-hero.zh_CN.md)、[仿真统计](docs/apps/brake-hero-simulation.json)及[素材来源](assets/README.zh_CN.md)。
