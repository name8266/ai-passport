<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 塔罗牌素材

固件使用 1909 年 Waite-Smith 原始牌扫描图。仓库内图集由
`sixseeds/tarot-api` 镜像的固定提交
`71825eed74683305b139a669b23ca5dc12f76857` 生成；该镜像 README 明确说明
78 张 Rider-Waite 图片均为公有领域。生成器也支持直接从 Wikimedia Commons
取得「Roses & Lilies」扫描图。本项目明确排除可能仍受版权保护的现代重新着色版本。

`source-manifest.json` 记录每张牌的来源页、源文件哈希、下载文件哈希、署名
字段、许可标记、牌序、生成尺寸及最终图集哈希。使用以下命令重新生成：

```bash
python3 tools/gen_tarot_assets.py
```

从固定镜像的本地检出重新生成仓库内图集：

```bash
python3 tools/gen_tarot_assets.py --source-dir /path/to/tarot-api/cards
```

生成器依赖 Pillow，把 78 张牌分别转换为 112 x 192、小端 RGB565 帧，再连接成
`main/assets/tarot_cards.rgb565`。固定步长使固件可以直接从 Flash 定位牌面，
无需在 RAM 中解码或保存整张原图。
