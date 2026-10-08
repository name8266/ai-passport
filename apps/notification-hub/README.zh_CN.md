# Passport Notification Hub｜iPhone 通知中心

[English](README.md) · **简体中文**

从 `main` 新建的独立 Passport 固件，和 Passport Nav 没有继承关系。
这不是需要安装在 iPhone 上读取其他 App 隐私的程序，而是使用
**Apple ANCS**，让经过蓝牙安全配对和用户授权的 Passport
接收 iPhone 系统对外开放的普通应用通知。

**范围仅限通知**：没有导航、GPS、音乐、Wi-Fi、网络转发、麦克风、
第三方 App 账户登录，也不做通知回复或远程删除。

## 功能
- 获取 iOS 新通知、更新和移除事件。
- 读取通知的应用标识、标题和正文（仅限 iOS 实际提供的信息）。
- RAM 保存最近 8 条，重启/断线清空；绝不持久化正文。
- 上下键切换通知，确认键清除设备上的临时历史。
- 显示设备电量、蓝牙连接与配对状态。
- 显示一次性配对密码，使用 LE Secure Connections + MITM 配对。
- 使用 LVGL 自带思源黑体 SC 16 CJK **字形子集**：可以显示部分中文，
  并非能保证显示任意人名、表情及生僻字；仍需核查字库和实机画面。

## 编译和测试
安装并激活 ESP-IDF 5.5.3：

```bash
source /path/to/esp-idf-v5.5.3/export.sh
cd apps/notification-hub/firmware
idf.py set-target esp32c3
idf.py build
idf.py merge-bin -o notification-hub-full.bin --format raw
```

仓库根目录运行协议解析测试：

```bash
cc -std=c11 -Wall -Wextra -Werror \
  apps/notification-hub/tests/test_hub_protocol.c \
  apps/notification-hub/firmware/main/hub_protocol.c \
  -o /tmp/hub_protocol && /tmp/hub_protocol
```

## 使用
1. 安装固件并打开 Passport，再在 iPhone **设置 → 蓝牙** 中查找 `Notify Hub`。
2. 连接，输入 Passport 屏幕显示的六位配对码。
3. 如 iOS 提示或显示「共享系统通知」，请主动授权；也要确保目标 App
   自己的通知和预览权限已打开。
4. 有通知到来后在 Passport 显示，上下键翻阅、确定键清空本地列表。

普通 iOS App 不能读取其他所有 App 的通知。ANCS 是给蓝牙配件公开的
系统接口，但 iPhone **不保证每一条通知内容均可见**；专注模式、应用设置、
锁屏预览权限、系统版本等均会影响表现。

## 安全与限制
仅建立蓝牙安全连接时才能从 iPhone 获取可授权的数据；固件不执行
ANCS 控制点的通知操作功能，不读取通知以外的个人数据。配对凭据使用
NVS；正文仅在 RAM 中临时保存，断线时清理，不上传云端。

目前属于初版：必须进行实机连接和中文字符覆盖测试。
某些 ANCS 异常（如长时间不返回详情）尚需进一步完善恢复机制。
CI 成功不代表真实设备的 Bluetooth 权限及配对流程已确认。

完整合并镜像从地址 `0x0` 刷写可能重置 NVS 与存储数据。如果已有重要
配置，必须先核对分区兼容性并选择合适刷写方式，不能直接擦除整片闪存。

苹果协议规范：https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/Specification/Specification.html
