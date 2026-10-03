<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# FoloToy AI Passport 世界之窗

面向 ESP32-C3 Passport 的中文公开摄像机地图浏览器。本功能分支包含设备应用、局域网图片网关和手机浏览器直连检测页。

目录收录 825 个公开摄像机，覆盖 72 个国家和地区。另有 195 个国家的首都登记表，目前 50 个首都找到公开来源，49 个取得图片。“每个首都有可用摄像机”的要求仍未完成，缺少来源的地点明确标记。

## 设备操作

| 操作 | 地图 | 画面观看 |
| --- | --- | --- |
| 前后键 | 选择地点 | 切换地点 |
| OK | 查看画面 | 刷新 |
| 长按 OK | 联网设置 | 返回地图 |
| 长按上键 | 随机游览 | 随机游览 |
| 长按下键 | 全部地点 / 首都 | 切换全屏 |

显示摄像机快照或发布方预览图，观看时每 60 秒尝试刷新。各源站的拍摄频率不同，获取时间不等于拍摄时间。全屏保留画面比例，必要时留黑边。设备显示地点、国家和获取时间；发布方署名与来源链接显示在配套网页。

## 启动局域网网关

在与 Passport 同一可信局域网的电脑上使用 Python 3.12+：

```bash
python3 -m venv .worldcam-venv
.worldcam-venv/bin/pip install -r gateway/requirements.txt
.worldcam-venv/bin/python gateway/server.py --host 0.0.0.0 --port 8787
```

访问 `http://<computer-LAN-IP>:8787`，保持网关运行。设备首次启动后，连接屏幕显示的 `WorldCam-XXXX` 热点，打开 `http://192.168.4.1`，填写 2.4 GHz Wi-Fi 和网关地址。设备网关地址不能使用 `localhost`。应用将设置保存在自己的 NVS 命名空间中。不要把这个无身份验证的本地网关直接暴露到公网。

## 手机检测图片访问

可复用的静态检测页位于 `gateway/mobile-test/dist/`：

```bash
python3 -m http.server 8790 --bind 0.0.0.0 --directory gateway/mobile-test/dist
```

手机打开 `http://<computer-LAN-IP>:8790`。图片由手机当前网络直接向发布方加载，支持快速和全量检测、暂停、复制摘要及导出 JSON。页面不上传报告。保持页面在前台，进入后台会暂停正在进行的请求。浏览器结果不能证明物理出口国家、VPN 状态、拍摄时效或设备运行正常。

电脑可运行只读检测工具 `tools/probe_worldcam_network.py`，直连与代理对比方法见[完整说明](docs/assets/worldcam.zh_CN.md)。

## 构建与验证

启用 ESP-IDF 5.5.3，安装网关依赖，并准备 Node.js 18+ 用于浏览器检测逻辑的主机测试：

```bash
./tools/validate.sh
```

完整检查验证仓库和主机测试，构建 ESP32-C3 固件并检查合并镜像 `build/FoloToy-AI-Passport-full.bin`，烧录地址为 `0x0`。固件属于构建产物，不放入代码仓库。烧录需单独授权，无需全片擦除。

构建和主机检查通过。USB 烧录、中文屏幕显示、按键手势、Wi-Fi 配网、堆内存峰值和长时间运行尚未经过真机验证。手机图片访问测试与设备验收是不同的检查。

参见[世界之窗说明](docs/assets/worldcam.zh_CN.md)、[素材来源与许可证](assets/README.zh_CN.md)及[上游产品文档](docs/README.zh_CN.md)。
