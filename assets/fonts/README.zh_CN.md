<p align="right"><strong>简体中文</strong> · <a href="README.md">English</a></p>

# 通知应用中文字库

`notification_hub_16.c` 是由 Google Noto Sans SC 生成的静态 LVGL 字体，字重 400、
16 像素、每像素 2 位，不压缩、不使用字距调整。
`notification_hub_font.json` 记录来源网址、SHA-256 和转换器版本。
随附的 `notification_hub_OFL.txt` 保留 SIL 开放字体许可证。

`notification_hub_glyphs.txt` 包含可打印 ASCII、有效 GB2312 字符与固定中文界面用字，
共 7,540 个字形。应用为全部中文标签指定此字体。动态文字的字库外字符显示为 `?`，
不修改原始通知存档。表情和任意 CJK 扩展不在覆盖范围。主机检查验证生成字体的 LVGL
映射表，启动检查通过 LVGL 查询固定界面字形；两者都不能证明实际屏幕渲染效果。

重新生成时，在 Python 虚拟环境安装 `fonttools==4.60.2`，用 npm 安装
`lv_font_conv@1.5.3`。下载 JSON 中记录的源字体，再运行
`python tools/generate_notification_font.py --source <ttf> --converter <lv_font_conv>`。
脚本会先核验源字体哈希，再生成资源。

`notification_hub_24.c` 是用于固定标题的 24 像素、36 字形子集。
`notification_hub_title_glyphs.txt` 记录用字，生成和覆盖检查同时处理两种字号。
