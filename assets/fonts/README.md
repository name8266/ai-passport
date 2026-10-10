<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Notification Hub Chinese font

`notification_hub_16.c` is a static LVGL font generated from Google's Noto Sans SC
at weight 400, size 16 px, 2 bits per pixel, without compression or kerning.
`notification_hub_font.json` records the source URL, SHA-256 and converter version.
The accompanying `notification_hub_OFL.txt` retains the SIL Open Font License.

`notification_hub_glyphs.txt` includes printable ASCII, valid GB2312 characters
and fixed Chinese interface characters (7,540 glyphs). The application assigns
this font to all Chinese labels. Unsupported dynamic characters display as `?`
without changing stored notifications. Emoji and arbitrary CJK extensions are
not covered. Host coverage checks inspect generated LVGL cmaps; startup checks
query LVGL for fixed interface glyphs. Neither proves physical screen rendering.

To regenerate, install `fonttools==4.60.2` in a Python virtual environment and
`lv_font_conv@1.5.3` using npm. Download the source URL recorded in the JSON, then
run `python tools/generate_notification_font.py --source <ttf> --converter <lv_font_conv>`.
The script verifies the source hash before producing assets.

`notification_hub_24.c` is a 24 px, 36-glyph subset for fixed headings.
`notification_hub_title_glyphs.txt` records it. Generation and coverage checks handle both sizes.
