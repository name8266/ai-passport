[English](pocket-netsec.md) · **简体中文**

# AI Passport Pocket NetSec

面向 ESP32-C3 Passport 的三键无线安全学习和诊断应用，适配 240×320
屏幕、8 MB Flash、无 PSRAM。独立应用入口和界面取代原硬件演示菜单，
引脚和 BSP 驱动沿用产品基线。基于 `name8266/ai-passport` 的 main 提交
`869d212ac12d6b994604581000bdc3bfdfa39465`；选择时 `ai-passport-next`
默认分支仅有 README。

## 工具与操作

上下键选项，确认键进入。长按确认先从详情返回列表，再从工具返回主页。
橙色焦点、深色背景的独立仪表界面包含 12 个工具、分页列表、详情面板、
信道柱图、实时信号曲线、状态提示、按键说明与电池图标。不会链接原演示页面。

| 工具 | 行为及按键 |
| --- | --- |
| Wi-Fi 扫描 | 每六秒被动扫描一次，每信道 120 ms，包含隐藏网络。按 RSSI 排序；确认查看 SSID/BSSID/信道/RSSI/加密类型，再确认将此 BSSID 设为信号和握手观察目标。刷新保留选中的 BSSID；详情页暂停发起新的自动扫描。 |
| 信道占用 | 显示 AP 数量及相邻信道加权 RSSI 评分，上下查看信道，确认重扫。它是 AP 重叠估算，并非物理频谱或信道利用率测量；低分信道建议不保证最优。 |
| 信号追踪 | 先选目标 AP，在其信道接收 Beacon。记录 60 个一秒 RSSI 样本，显示 −100 到 −20 dBm；缺失样本显示断点，确认暂停。 |
| 帧监视器 | 管理/数据/控制帧数量，Beacon、Probe Request、Probe Response、每秒收到的帧数、拒绝解析帧数量。确认暂停，上下切换并锁定信道；设置中可开启每 500 ms 跳频。 |
| 断网帧检测 | 仅接收 Deauth/Disassoc；每秒总数超过可配置阈值时告警。显示数量、发送者和未加密原因码，不解析受保护原因载荷。突增或 MAC 地址是观察记录，不能据此证明攻击或归属。 |
| BLE 扫描 | 被动接收传统广播，每五秒扫描周期启用去重，设备列表有上限。确认查看地址/地址类型/RSSI/广播类型/名称/原始 AD 数据；确认切换原始字节及解析的 flags/厂商 ID/首个 16 位服务 UUID/TX 功率，上下切换设备。不配对、不连接、不广播。 |
| 局域网信息 | 明确连接通过本地 USB 保存的自有网络。显示 IPv4、掩码、网关、主 DNS、DHCP 客户端状态；确认重试，下键保持连接进入服务发现。连接尝试 15 秒超时，不无限自动重连。 |
| 服务发现 | 已连接 LAN 上进行有界八秒 mDNS DNS-SD 浏览和 SSDP M-SEARCH，组播 TTL 为 1。最多保留 24 条 PTR/SRV/A 记录和公开 LOCATION 元数据。确认详情，再确认刷新；长按上键返回 LAN。查询限制为 `.local`，仅处理同 IPv4 子网的响应。不访问广告 URL、不探测端口。 |
| 帧头抓包 | 确认开始/停止 64 记录 RAM 环形缓冲；长按上键 USB 导出，长按下键清空。每帧仅保留最多 36 字节 802.11 头、原始长度和启动时间戳；不保存载荷、密钥、EAPOL 材料或 FCS。导出停止录制，Wireshark 中有意显示截断帧。 |
| 握手流程观察 | 固定信道被动解析 key-info，分类 M1–M4，可按选中的 BSSID 过滤。最多八个 AP/站点会话，显示观察到的阶段及重复数量，上下切换会话，确认重置计数，长按上键取消目标过滤。不存 nonce、MIC、PMK、PTK、密码或握手载荷。丢帧和加密可能妨碍观察，已看到阶段不证明认证成功。 |
| 扫描历史 | 最近 12 份摘要：启动标识、运行秒数、AP 总数、最强 RSSI、建议信道、保留 AP 的信道数量。确认详情，长按下键清空持久历史。不保存历史 SSID/BSSID。 |
| 设置 | 语言、亮度、固定信道、监视器跳频、异常阈值、地区、历史保存。确认循环修改并立即提交 NVS。默认 WORLD 为 1–11；仅在所在地允许时选择 CN/EU 的 1–13。信号和握手始终固定信道。 |

LAN 到服务发现的操作完全支持三键。其他情况下返回主页停止无线并断开 LAN，
减少功耗和内存。抓包留在 RAM，重启即丢失；设置和历史断电保留。
扫描历史最多每分钟写入一次，离开扫描工具时另行保存；突然断电可能丢失
最新未提交的扫描摘要。页面切换等待无线停止确认后才复用资源，旧队列事件
携带代际标识并被拒绝。

## USB 配网和导出

使用原生 USB Serial/JTAG 控制台，115200 波特率。输入密码时不要开启终端
本地回显，每条命令以换行结束：

```text
HELP
STATUS
SCAN
WIFI <自有网络SSID>|<密码>
CONNECT
FORGET
PAGE <1-12>
TARGET <扫描列表从0开始的序号>
PCAP
CLEAR
HISTORY
```

WIFI 只保存配置；LAN/CONNECT 明确触发连接。SSID 最多 32 个 UTF-8 字节，
密码最多 64 字节，开放网络密码为空；协议分隔符 `|` 不能出现在 SSID 中，
密码中可包含。凭据存储在本地未启用 Flash 加密的 NVS 中；应用不记录或
回显密码，构建中没有嵌入凭据。FORGET 仅删除保存的网络；NVS 初始化失败
会报告错误，不自动擦除其他应用数据。

使用隐藏密码提示的客户端，避免密码进入命令历史：

```bash
python3 -m pip install pyserial
python3 tools/netsec_serial.py --port /dev/cu.usbmodemXXXX --wifi 'Own Lab'
python3 tools/netsec_serial.py --port /dev/cu.usbmodemXXXX --command CONNECT
python3 tools/netsec_serial.py --port /dev/cu.usbmodemXXXX --output headers.pcap
```

导出以 `PCAP_BEGIN <数量>`、`PCAP_DATA <十六进制>`、`PCAP_END` 分帧，
包含小端 PCAP 2.4、DLT_IEEE802_11（105）、snaplen 36、运行微秒时间戳，
并非墙上时钟。客户端忽略无关日志，校验数量和长度，完整接收成功后才写文件。
USB 导出不取得或解密网络载荷，同一串口不要同时开启两个客户端。

## 资源和字体

- 最多 40 个 Wi-Fi AP、32 个 BLE 设备、24 条服务记录、12 份历史、八个
  EAPOL 会话、64 条抓包、60 个 RSSI 样本、32 个队列事件。AP 总数可能超过
  保留数，评分和信道计数基于保留的最强 AP 集合。
- Wi-Fi 与 NimBLE 按模式初始化/释放，不同时运行。回调只进行有界计数、
  复制和入队；应用工作任务负责网络、持久化、串口解析和 UI 更新。非 LVGL
  任务访问界面持有 BSP 锁，导航期间不删除应用屏幕。
- LVGL 静态池为 48 KB；不启动音频解码器、不分配全屏截图缓冲，字形驻留
  Flash。帧计数包括重传；驱动丢帧和跳频意味着它不代表全部无线流量。
  BLE 事件队列溢出数量显示在状态行。
- Noto Sans CJK SC Regular，SIL OFL 1.1，lv_font_conv 1.5.3，14 像素、2bpp、
  未压缩；涵盖 ASCII、U+3000–303F、U+4E00–9FEF、U+FF01–FF60。UI 明确
  选择该字体；其他文字、emoji、错误 UTF-8 显示为 `?`，不会截断有效字符。
  保留缺字占位诊断。字体审计验证汉字区、固定文案和已知缺字 emoji；实体屏
  可读性仍需实测。

从 [Noto 官方项目](https://github.com/notofonts/noto-cjk/tree/main/Sans/OTF/SimplifiedChinese)
取得授权源字体后重生成：

```bash
npm install --no-save lv_font_conv@1.5.3
python3 tools/netsec_font.py --font /path/to/NotoSansCJKsc-Regular.otf \
  --converter ./node_modules/.bin/lv_font_conv
python3 tools/netsec_font.py --audit
```

## 构建和刷机

启用 ESP-IDF 5.5.3，运行完整项目门禁：

```bash
source /path/to/esp-idf-v5.5.3/export.sh
./tools/validate.sh
```

门禁从跟踪的默认设置创建隔离配置，校验合并镜像、分区和匹配 ELF。
生成的固件及调试包不提交。在 Managed Components 已下载后，桌面可用
真实 UI 代码及固定测试数据渲染：

```bash
cmake -S tests/netsec_ui -B build/ui-host
cmake --build build/ui-host -j 8
mkdir -p build/ui-frames
build/ui-host/netsec_ui build/ui-frames
```

它检查 13 个屏幕、重复重绘与 LVGL 内存池，并生成 PPM 帧，不能证明实体屏、
无线或设备行为。macOS 现有宿主运行时测试需要将 GNU `--gc-sections` 替换为
Darwin `-Wl,-dead_strip`，可使用编译器包装器做平台翻译。

分区保持 NVS `0x9000/0x6000`、PHY `0xF000/0x1000`、factory
`0x10000/0x7F0000`，均在 8 MB 内；不增加 OTA 或文件系统。
明确进行完整刷新时，将验证后的合并固件刷在 **0x0**：

```bash
python -m esptool --chip esp32c3 --port PORT --baud 460800 \
  write_flash 0x0 build/FoloToy-AI-Passport-full.bin
```

合并文件填充 NVS 间隙，可能重置设置、配网和历史。需要保留 NVS 的兼容更新，
使用存档中分开的 bootloader、分区表、应用镜像，按已验证 `flash_args` 的
偏移刷写。不要将仅应用镜像刷在 0x0；无需全片擦除。
见 [固件布局和数据政策](engineering/firmware-layout.zh_CN.md)。

## 仍需设备验收

构建和宿主检查不等同实体设备验收。请在 Passport 上检查启动、圆角、文字、
电池、所有按键路径、中文和长 SSID、重复 Wi-Fi/BLE 切换、低内存/失败恢复、
空和拥挤环境、信道限制、目标 RSSI 断点/暂停、实验网络正常重连的 EAPOL 阶段、
合法断连帧计数、BLE 名称/原始数据、错误密码/超时、LAN/服务发现、Wireshark
中的帧头 PCAP、断电设置/历史。多次切换后检查最低剩余堆，不假设最新未提交
摘要能在突然断电后保留；验收不需要任何破坏性流量。

不实现原始 802.11 发包、Deauth 注入、邪恶双胞胎、凭据收集、密码破解、
端口暴力扫描或 BLE 攻击。安全 LAN 服务发现和明确连接自有网络，是正常协议
操作之外的有意诊断发送行为。
