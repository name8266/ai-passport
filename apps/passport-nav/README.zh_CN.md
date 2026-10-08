# Passport Nav — iPhone + Passport 实时导航仪表

[English](README.md) · **简体中文**

独立于原硬件测试菜单的双端项目。iPhone（iOS 17 及以上）使用 Apple
MapKit 规划路线、CoreLocation 定位，通过 BLE 将 20 字节导航数据发送给
Passport；ESP32-C3 只渲染仪表盘，不传送地图图片，不修改其他功能分支。

## 目录
- firmware/：ESP-IDF 5.5.3 工程，复用本仓库 BSP。
- ios/：SwiftUI + MapKit + CoreBluetooth 手机 App，采用 XcodeGen 工程描述。
- tests/：导航协议 C 语言单元测试。

## iOS 安装
在 Mac 安装 Xcode 与 XcodeGen，然后：
\`\`\`sh
cd apps/passport-nav/ios
xcodegen generate
open PassportNav.xcodeproj
\`\`\`
在 Xcode 设置自己的 Signing Team 和唯一 Bundle ID，用真实 iPhone
运行并授权蓝牙、定位；模拟器不能完成真实 BLE 和 GPS 验证。
连接设备「Passport Nav」，搜索地点并点击「开始导航」。
「模拟仪表数据」不用规划路线即可测试通讯。

v0.1 使用苹果地图框架，不需要高德 Key。路线进度/偏航重算属于
实验版本，复杂路口的转向提示需要实际行驶测试，不可替代交通标志。

## Passport 固件
\`\`\`sh
source /path/to/esp-idf-v5.5.3/export.sh
cd apps/passport-nav/firmware
idf.py set-target esp32c3
idf.py build
idf.py merge-bin -o build/passport-nav-full.bin --format raw
\`\`\`
单独测试协议：
\`\`\`sh
cc -std=c11 -Wall -Wextra -Werror \
  apps/passport-nav/tests/test_nav_packet.c \
  apps/passport-nav/firmware/main/nav_packet.c -o /tmp/navtest && /tmp/navtest
\`\`\`
完整合并固件在地址 0x0 刷写可能覆盖 NVS 等用户数据；
如需保留原有设置，应先确认分区兼容性并使用适当的分段刷写方式，
不能默认清空闪存。

## BLE 协议
- 服务：8A2FA760-11F0-4FD9-9B94-6E092E60E21A
- 写入特征：8A2FA760-11F0-4FD9-9B94-6E092E60E21B
- 单包 20 字节；[0] A5、[1] 版本 1，均为小端。
- [2] 转向类型：0 无、1 直行、2 左、3 右、4 掉头、
  5 到达、6 靠左、7 靠右。
- [3] 状态：bit0 导航中、bit1 有 GPS、bit2 重算中。
- [4..5] 转向距离（米）；[6..7] 车速（0.1 km/h）。
- [8..9] 剩余距离（10 米）；[10..11] 剩余秒数。
- [12] 预计到达小时；[13] 分钟；[14..15] 帧序号。
- [16..17] 航向角（0–359 度）；[18] 保留 0。
- [19] 前 19 字节异或校验。

无有效数据超过 3 秒显示超时，不继续把旧数据显示为实时。
上/下键切换导航、速度、行程三种界面，确认键切换背光亮度。
支持通过板载电池驱动显示电量，不可用时显示 --%。

## 已知限制与安全性
当前 BLE 写入尚未实现认证或加密，不宜承载个人隐私。
正式版需增加配对授权机制和必要的抗误连保护。
iOS 已配置后台定位及蓝牙恢复，但锁屏持续刷新和自动重连均未
通过真机验证；苹果地图搜索与路线规划受到地区和网络条件约束。
Passport 为避免缺字暂时采用英文/数字显示，中文路名仅在手机侧显示。

项目不包含离线地图、语音播报、CarPlay、地图 App 之间的导航数据读取。
工作流 passport-nav.yml 对协议、固件和 iOS 模拟器工程进行构建验证；
只有 CI 成功才可报告构建通过，仍需单独实机测试。
