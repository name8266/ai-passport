<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# FoloToy AI Passport 星图塔罗

星图塔罗是一款为 FoloToy AI Passport 设计的全离线简体中文塔罗应用。固件内置完整的
78 张 1909 年 Rider–Waite–Smith 公版塔罗牌、本地牌义、四种占卜方式、历史记录、
收藏和设备设置。使用时不需要账号、网络、云端 API，也不会上传用户数据。

## 主要功能

- 今日指引、单牌问答、三牌阵和凯尔特十字。
- 支持正位、逆位以及结合牌阵位置生成的本地解读。
- 内置全部 78 张牌面，可在牌库百科中离线浏览。
- 自动保存完成的占卜，支持历史翻阅、收藏保护和清理非收藏记录。
- 逆位、声音、亮度、历史及收藏状态通过 NVS 持久保存。
- 非阻塞音效、电量显示、自动降低亮度和自动熄屏。
- 独立设计的深蓝金色 LVGL 中文界面及内置简体中文字体。

## 三键操作

| 按键 | 功能 |
| --- | --- |
| 上 / 下 | 切换菜单、牌位、牌库或历史记录 |
| 确定 | 进入菜单、揭牌、查看解读或修改设置 |
| 长按确定 | 返回上一层或主页 |

一次占卜中的牌全部揭开后会自动保存。在“历史记录”中按“确定”可以收藏或取消收藏；
“设置”中的清理功能只会删除未收藏的历史记录。

## 编译方法

目标硬件为 ESP32-C3、8 MB Flash、无 PSRAM，需要 ESP-IDF 5.5.3。

```bash
source /path/to/esp-idf-v5.5.3/export.sh
idf.py build
```

交付前运行完整验证与合并固件打包：

```bash
./tools/validate.sh
```

验证脚本会生成 `build/FoloToy-AI-Passport-full.bin`，烧录偏移地址为 `0x0`。
成功编译不等同于真机验收；屏幕显示、中文字体、按键、音频、电量读数和长时间运行仍需在
实际设备上验证。

## 素材和许可证

牌图源自 1909 年 Rider–Waite–Smith 公版塔罗牌。固定来源版本、许可说明及每张原图的
哈希记录在 [`assets/tarot/source-manifest.json`](assets/tarot/source-manifest.json)。
简体中文字体子集源自 Noto Sans SC，遵循 SIL Open Font License，详见
[`assets/fonts/`](assets/fonts/)。

本应用仅供娱乐和自我反思，不替代医疗、法律、财务或心理健康领域的专业意见。
