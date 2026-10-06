<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

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

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

### 游乐场中文界面字体

`fonts/NotoSansCJKsc-Arcade.otf` 是 [Noto Sans CJK SC Regular](https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf) 的子集，采用 SIL OFL 1.1 许可（保留在 `fonts/OFL.txt`）。包含可打印 ASCII 以及 `fonts/arcade_symbols.txt` 中的固定界面字符。14px/20px、4bpp、未压缩的 `arcade_font_*.c` 由 main 组件编译到 Flash。使用 lv_font_conv 1.5.3，通过 `python3 tools/regenerate_arcade_fonts.py --converter /path/to/lv_font_conv` 重新生成。增加字符前，须从上游字体重新构建 OTF 子集。启动时检查两种字号及一个已知缺失字符的负例。

### 经典老虎机卷带图案

`images/slot-classic-atlas.png` 是 2026-10-06 使用内置图像生成工具为本应用原创制作的 1536×1024 RGBA 六图案素材表。采用机械卷带常见图案：樱桃、柠檬、BAR、金铃铛、紫李子及红色 7；未嵌入第三方图稿或品牌标识。视觉参考包含真实机械卷带，例如 [1938 年 Mills Cherry Bell](https://www.antiguedades.es/es/otros-objetos-antiguos-vendidos/1154-tragaperras-antigua-para-mercado-ingles-del-ano-1938-en-funcionamiento-con-peniques-ingleses-para-su-manejo.html)，未复制其图稿。

通过 `python3 tools/convert_slot_symbols.py`（需要 Pillow）重新生成固件素材。转换器裁切六个透明单元、保持图案比例、居中放入经典模式 48×64 或多线模式 32×36 的象牙白底图，并编码为小端 RGB565。`images/slot-*.png` 保留实际尺寸预览，`images/slot-symbols-native.png` 为 288×64 的经典图案总览，`images/slot-symbols-small-native.png` 为 192×36 的多线图案总览。`images/slot_symbols.c` 提供十二个共享 const LVGL 图像描述符，像素共占 Flash 50,688 字节，无逐行像素副本或透明图层。原 `SLOT_GEM` 编号现显示紫李子，符号编号、出现概率及结果逻辑保持一致。`tests/test_slot_symbols.py` 检查尺寸、描述符顺序、数据长度、唯一性、对比度及底图四角。
