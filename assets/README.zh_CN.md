<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

| 文件 | 用途与来源 |
| --- | --- |
| [`fonts/zuma_cjk_14.c`](fonts/zuma_cjk_14.c)、[`zuma_cjk_16.c`](fonts/zuma_cjk_16.c)、[`zuma_cjk_20.c`](fonts/zuma_cjk_20.c) | 《星环弹珠》固定中文界面的 Flash 常驻 4 bpp LVGL 子集字库，使用 `python3 tools/generate_star_orbit_font.py` 和 `lv_font_conv@1.5.3` 生成。源字体为 Adobe 思源黑体简体中文 Regular，固定提交 `a4f7cf94edfb9d7ffbdfc4841de276358bd7e0f2`，SHA-256 为 `f1d8611151880c6c336aabeac4640ef434fa13cbfbf1ffe82d0a71b2a5637256`。覆盖 ASCII U+0020–U+007E 及 `main/main.c` 字符串中的全部非 ASCII 字符。`--check` 无需下载即可核对字形范围。子集字库及 [`OFL-SourceHanSans.txt`](fonts/OFL-SourceHanSans.txt) 使用 SIL OFL 1.1 许可。 |
| [`fonts/arcade_cjk_14.c`](fonts/arcade_cjk_14.c)、[`arcade_cjk_20.c`](fonts/arcade_cjk_20.c) | 《八境街机》中文界面的 Flash 常驻 4 bpp LVGL 子集字库。`python3 tools/generate_arcade_font.py` 使用 `lv_font_conv@1.5.3` 重新生成；`--check` 核对 `main/arcade_main.c` 和 `main/arcade_game.c` 中的 ASCII 与中文字形。源字体为上述 Adobe 思源黑体简体中文 Regular，依照 SIL OFL 1.1 许可分发。 |

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |
| [`images/star-orbit/`](images/star-orbit/) | PNG 源图、240 × 320 棋盘预览、72 × 72 守护者预览、14 × 14 弹珠预览 | 《星环弹珠》的原创视觉素材。于 2026-10-11 使用内置图像生成工具专门为本项目生成，未使用其他游戏图像。`tools/generate_star_orbit_art.py` 确定性地缩放源图并将碰撞路径烘进 `main/art/*.rgb565*`，由 ESP-IDF 嵌入 Flash。`gameplay-preview.png` 是合成示意图，不是真机截图。素材依照仓库 MIT 许可证分发。 |
| [`images/eight-realms/`](images/eight-realms/) | 1448 × 1086 背景图集、1774 × 887 透明精灵图集、八张 240 × 320 背景、八个小精灵、960 × 640 总览图 | 《八境街机》的原创场景与角色素材，于 2026-10-13 使用内置图像生成工具生成。`tools/generate_arcade_art.py` 裁切、缩放并转换为 `main/art/eight-realms/` 下的 RGB565/RGB565A8 文件，供固件常驻 Flash。未使用第三方游戏图片；生成素材依照仓库 MIT 许可证分发。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

| 文件 | 格式 | 用途与来源 |
| --- | --- | --- |
| [`music/star-orbit-preview.wav`](music/star-orbit-preview.wav) | 16 kHz、16 位单声道 PCM、16 秒 | `main/zuma_synth.c` 原创实时合成器的主机预览，于 2026-10-11 用 `tools/render_zuma_audio.c` 生成。固件实时发声，不嵌入该 WAV。依照仓库 MIT 许可证分发。 |

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
