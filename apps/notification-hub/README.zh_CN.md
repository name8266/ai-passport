# Passport Notification Archive｜断电保存的 iPhone 通知档案

> 开发分支：feature/notification-archive。此分支从 feature/notification-hub 派生，**尚未编译、运行或实机验证**。
> 本轮按照需求未触发 GitHub Actions，也未刷写设备；不要把本分支标记为可用发布版本。

## 新增：AI 定时通知总结（源码阶段）

这一版本在设备通知归档基础上，新增 **可选的定时 AI 总结**：

1. Passport 的 Flash 一直保存 ANCS 通知原始快照（不改变历史、防撤回策略）。
2. 用户首次通过 USB 串口配置 Wi-Fi、HTTPS 网关域名和设备访问令牌。
3. 自托管的 Python 后台运行在常开的 Mac/NAS/服务器中，
   提供 \`/admin\` 管理页面；默认 DeepSeek，
   API 地址 \`https://api.deepseek.com/chat/completions\`，
   默认模型 \`deepseek-v4-flash\`。
4. 管理员在后台填写 API Key、调整模型/供应商、间隔（15–1440 分钟）、
   每批条数及不参与 AI 总结的应用标识，并明确勾选启用。
5. Passport 每分钟查询一次后台配置；到达总结时间后，把**尚未总结**
   的归档预览以最多 8 条/批的方式通过验证证书的 HTTPS 发送给网关。
   网关筛掉默认设置中的验证码/密码/银行信息提醒，再转交配置的
   OpenAI 兼容 AI API。
6. AI 总结返回后，Passport **先将总结追加到 Flash**，
   成功后才将已处理归档序号提交 NVS。超时或离线不清除归档、不会推进进度。
7. 应用分组页**长按上键**打开“AI SUMMARY”摘要页面；
   上下键切换摘要页面；长按下键可以手动发起一次总结（必须后台已启用），
   长按 OK 返回。这里只展示最新摘要；旧摘要保存在 Flash，
   历史浏览功能尚未完成。

本轮严格按要求**不编译、不运行、不刷写、不发布 BIN**。
软硬件并发运行（BLE + Wi-Fi + TLS）、中文字形、设备配对及网络失败恢复
仍需真机验证。没有 PSRAM 的 ESP32-C3 内存可能不足，
不能因为源码已提交就断言能稳定使用。

### 私密通知和 AI 的边界

- **默认关闭联网 AI**；后台主动启用后，才会发送新一轮数据。
- API Key 只放在自托管网关上，用环境变量密钥加密保存；
  不发送到 Passport，更不会提交 GitHub。
- 设备 Wi-Fi 密码和设备令牌暂存在 NVS，开发机的 USB 终端可能回显输入，
  这**不等于静态存储加密**。商用前要落实 Flash 加密、管理员访问控制和
  安全配网机制。
- 网关不保存原始通知文件，原文只有在转给 AI 服务商时被使用；
  Passport Flash 则会继续保留通知与 AI 摘要。
- 这不是苹果 Apple Intelligence 或 iOS 内置系统功能，
  只是基于 iPhone 已授权 ANCS 通知的**第三方硬件 AI 摘要**。
  未显示在 ANCS 的私聊、图片、语音和完整正文无法被总结。
- 用户需要自行评估第三方服务的价格、数据保留规则及隐私合规要求；
  默认筛掉密码/验证码类消息，可由后台进一步排除 App。
- 一小时通知量很大时，系统按批逐次处理，不能声称每次调用包含完整历史。
  每轮最多处理四个小批次，剩余记录留在 Flash 排队。

### 开发后台的部署与首次联网

后台工程及环境变量详见
[网关使用说明](gateway/README.md)。
后台配置支持 DeepSeek 和兼容 OpenAI Chat Completions 的其他模型。

首次开发配置通过 Passport USB 串口（仅供可信环境试用）：

\`\`\`text
hub help
hub wifi MyWiFi|MyPassword
hub server https://your-trusted-domain.example
hub token THE_SAME_TOKEN_AS_GATEWAY_ENV
hub restart
\`\`\`

由于设备通过 ESP-IDF 证书信任链验证 HTTPS，网关必须有有效受信任证书，
不能为了省事关闭证书检查。设备启动后必须先同步网络时间。
后台可以运行在常开的 Mac 或 NAS，但访问域名、TLS、路由可达性和持续供电
仍需要使用者配置。本版本没有 iOS 专属 App，也不依赖国行 iPhone 的系统 AI 权限。

## 功能定位

Passport 独立通过 Apple Notification Center Service (ANCS) 读取 iPhone
经用户授权共享的**普通应用通知**。没有导航、定位、音乐、Wi-Fi 或互联网服务。
iPhone 不需要安装一个拥有系统级隐私权限的 App。

- 事件一到，先把 **通知 UID + 类别的元数据快照**写入 Flash；
  收到详情后，再追加一份包含应用标识、标题、有限正文的快照。
- 若 iPhone 后续通知被移除（可能是清除通知中心、应用更新或消息撤回），
  归档记录**不会自动删除**；不能把 ANCS Removed 事件等同于“对方撤回”证据。
- 按 ANCS App Identifier 自动分组，可以用上下键选择 App，OK 进入历史，
  再浏览此 App 的通知快照。长按 OK 返回上一层。
- 设备重启、蓝牙断开后归档仍在，只读查看历史无需手机连接。
- 通知内容既不上传云端，也不在日志里打印正文。

## 硬件与容量

ESP32-C3，8 MB Flash，无 PSRAM，240×320 显示屏。新分区：

| 分区 | 起始地址 | 容量 |
| --- | --- | --- |
| NVS | 0x9000 | 24 KiB |
| PHY | 0xF000 | 4 KiB |
| 固件 factory | 0x10000 | 4032 KiB |
| 通知历史 archive | 0x400000 | 4096 KiB |

存储采用 **FATFS + SPI Flash wear levelling**。本地归档使用固定 384 字节快照；
理论上的存储上限略小于 1.1 万条快照，考虑文件系统开销、预留 32 KiB 和
每条通知可能包含“元数据＋详情”两次快照，实际可留存的完整通知数量通常
低于快照数上限。具体容量必须在真机确认，不能理解为无限保存。

**达到上限时不循环覆盖旧记录，停止继续归档并提示 ARCHIVE FULL。**
闪存无法保证永不损坏。当前版本没有导出与清理历史的界面；正式使用前必须
补齐用户主动备份、手动清理或扩展外部存储的能力。

### 断电一致性

- 每次快照追加到一个 Flash 日志文件后执行 flush/fsync。
- 旧快照不覆盖；后续详情通过追加新快照关联通知元数据。RAM 内维护
  “已完善元数据”的索引，启动时顺序扫描重建。
- 文件尾部不完整、校验失败或挂载已有文件系统失败时，不会自动删除历史，
  进入错误/只读状态等待人工处理。
- 全部独立存储操作运行在 worker Task，BLE 事件回调只把快照放入队列。
- 如果突然断电恰好发生在提交前，或者通知风暴导致队列溢出，仍可能遗漏。
  这是尽力保存而非强证据链或百分百的防撤回保证。

### “防撤回”的精确含义

iPhone 只有已生成、已授权并通过 ANCS 转发的通知内容才能被留存。
如果发送者在 iOS 发出通知之前撤回，或者 iOS 仅转发“你收到了一条消息”
这种隐藏预览，Passport **没有办法恢复原始正文、图片、语音或聊天历史**。
当前单条正文限 191 字节，标题限 95 字节，应用标识限 63 字节；
超长 UTF-8 文本按字符边界截断。一个应用可能有多次更新快照；
部分未取得详情的通知会出现在 Unresolved 分组。

## 设备操作

- App 分组页：UP/DOWN 切换分组，OK 进入当前 App 的历史。
- 历史列表页：UP/DOWN 切换该应用的归档记录，OK 看详细快照。
- 详细页：长按 OK 返回历史列表；历史列表长按 OK 返回应用分组。
- 页面显示归档状态，包括文件系统故障、容量不足或队列丢失警告。
- 用户在 iPhone「设置 → 蓝牙」配对 Notify Hub 并授权共享系统通知。

## 隐私与安全

本版是**敏感通知的持久化明文存储原型**。BLE 的配对加密保护传输，并不等于
Flash 静态内容加密；持有设备或能读取闪存的人可能读到私人消息。
在实用发布前，应加入设备本地访问锁、可靠的备份删除策略，以及评估适合
现有 ESP32-C3 生产流程的 Flash/NVS 加密、密钥恢复与安全擦除。
不要把这种设备当作司法取证器或不可篡改的存储设备。

## 编译 / 刷写（本轮不执行）

ESP-IDF 5.5.3，开发分支提供源代码与分区表。
为了保留历史，**禁止默认执行** \`erase-flash\`，也不要使用会重写
数据分区的整个 0x0 合并镜像；将来须核对分区兼容性后分段刷写固件。
首次从旧的「单一 factory 直至 Flash 尾部」分区迁移前，
必须先审查原有固件、Flash 高地址数据是否可保留。

在任何“可用”的宣称之前必须完成：编译、主机测试、固件启动、
首次格式化、断电重启、多 App 分组、大量通知、撤回后保留、
高频消息、磁盘写满、文件损坏、中文字符/表情、安全配对和恢复测试。

参考：
- https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleNotificationCenterServiceSpecification/Specification/Specification.html
- https://docs.espressif.com/projects/esp-idf/en/v5.5.3/esp32c3/api-reference/storage/fatfs.html
