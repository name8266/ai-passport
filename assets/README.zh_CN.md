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

## 世界之窗字体与地图

- `fonts/NotoSansCJKsc-Regular.otf`：来自
  [notofonts/noto-cjk](https://github.com/notofonts/noto-cjk) 的 Noto Sans CJK SC Regular，
  SIL Open Font License 1.1，许可证保留为 `fonts/OFL.txt`。16 MB原字体为重新生成时的可选本地依赖，
  通过 `tools/fetch_worldcam_font_source.py` 从固定提交 `f8d157532fbfaeda587e826d4cd5b21a49186f7c`
  下载并检查SHA256。生成工具会在缺少时下载；普通固件构建编译已提交的子集，不下载原字体。
- `fonts/NotoSans-Regular.ttf`：来自
  [notofonts/noto-fonts](https://github.com/notofonts/noto-fonts) 的 Noto Sans Regular，
  SIL Open Font License 1.1，许可证保留为 `fonts/NotoSans-OFL.txt`。补充CJK源字体没有的扩展拉丁字符。
- `fonts/worldcam_font_16.c`：通过 `lv_font_conv` 1.5.3 生成的16px、4bpp无压缩字库。
  实际字符清单与来源记录在 `fonts/worldcam_chars.txt` 和 `fonts/worldcam_font_manifest.json`，
  用 `tools/generate_worldcam_font.py` 重新生成，由main组件编译。
- `maps/world.geo.json`：来自
  [natural-earth-vector](https://github.com/nvkelso/natural-earth-vector) 的国界轮廓，基于公共领域的
  [Natural Earth](https://www.naturalearthdata.com/about/terms-of-use/) 数据。
  `tools/generate_worldcam_map.py` 生成Flash地图 `maps/world_map.c` 和网页 `gateway/world-map.svg`。
- 城市和首都匹配使用 [GeoNames](https://www.geonames.org/)，
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)；国家名称使用
  [mledoze/countries](https://github.com/mledoze/countries)，ODbL 1.0；195 个首都登记点的坐标均匹配 GeoNames 城市。维护目录保留来源、坐标精度和首都缺口。
  地理位置为简略定位，不代表来源可用。参见[世界之窗说明](../docs/assets/worldcam.zh_CN.md)。

- `images/worldcam-phone-check.png`：应用自有手机检测页的390px浏览器截图，展示云端30源抽测通过。
  不包含摄像机照片或私人设备信息，不代表硬件或大陆网络已经验证。

股票看板字体：`fonts/stock_font_16.c`，同目录保存字符清单和清单文件。来自 Noto Sans CJK SC / Noto Sans Regular，SIL OFL 1.1，许可位于 `fonts/`。使用 16px、2bpp、不压缩字体覆盖支持的 GBK 股票名称和界面文字。参见[股票看板](../docs/apps/stock-quotes.zh_CN.md)。
