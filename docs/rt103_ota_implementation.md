# RT-103 双阶段 OTA 实现思路

更新时间：2026-06-30

适用项目：RT-103，STM32F103RCT6，PlatformIO，FreeRTOS，W25Q16 外部 Flash，串口 COM5。

## 这篇文章解决什么问题

RT-103 量产后主要可用通道是串口，没有默认依赖 ST-Link 的现场升级条件。目标是在设备已经运行 app 的情况下，通过串口下发新的 app bin，让设备自己完成升级，同时保护 W25Q 中的用户数据，并尽量提升断电恢复能力。

当前 OTA 采用双阶段方案：

- app 负责通过 USART1 接收固件，写入 W25Q16 的 OTA 暂存区。
- bootloader 负责校验暂存镜像，并写入 STM32 内部 Flash 的目标 app slot。
- app 和 bootloader 通过 W25Q manifest 交接 OTA 状态。
- bootloader 通过内部 Flash boot state 记录 A/B slot、确认状态、防回滚版本和启动尝试次数。
- `OTA APPLY` 使用软跳 bootloader，不使用硬复位，避免 PB10/KEY_OUT 掉电。

## 关键结论

这一版已经从“单 app 区升级”增强为“内部 Flash A/B app 分区升级”：

| 能力 | 当前实现 |
|---|---|
| 版本号 | manifest v2 增加 `firmware_version` 和 `min_allowed_version` |
| 防回滚 | boot state 保存 `accepted_version`，低版本镜像会被拒绝 |
| 断点续传 | manifest 保存 `received_size`，每 4KB checkpoint 持久化一次 |
| 提速 | 保留 ASCII 文本协议，同时新增 `OTAB` 直接二进制包 |
| A/B 分区 | app A 和 app B 分别链接到不同内部 Flash slot |
| 断电恢复 | 新固件先写 inactive slot，启动成功后 app confirm，否则 bootloader 回滚 |
| 售后定位 | manifest 增加 `failure_reason` 和 `failure_detail` |

## 内部 Flash 布局

STM32F103RCT6 内部 Flash 共 256KB。A/B 方案下布局如下：

| 区域 | 地址 | 大小 | 用途 |
|---|---:|---:|---|
| bootloader | `0x08000000` | 28KB | 引导、OTA 应用、slot 选择 |
| boot state primary | `0x08007000` | 2KB | A/B 状态主记录 |
| boot state backup | `0x08007800` | 2KB | A/B 状态备份记录 |
| app slot A | `0x08008000` | 112KB | A 槽 app |
| app slot B | `0x08024000` | 112KB | B 槽 app |

注意：真 A/B 不能把同一个 app bin 同时放到两个地址运行。app A 和 app B 的 VTOR、ResetHandler、常量地址都与链接地址相关，所以必须分别构建：

```text
pio run -e app_a
pio run -e app_b
```

`app_a` 使用 `STM32F103XX_APP_A.ld`，向量表偏移 `0x00008000`。`app_b` 使用 `STM32F103XX_APP_B.ld`，向量表偏移 `0x00024000`。

## W25Q16 布局

外部 Flash 仍然只作为 OTA 暂存和用户数据存储，不直接作为执行区：

| 区域 | 地址 | 用途 |
|---|---:|---|
| manifest | `0x00000000` | OTA 状态、版本、CRC、失败原因 |
| image slot | `0x00001000` | 暂存 app bin |
| LittleFS | `0x00041000` | 用户数据文件系统 |

OTA 只擦写 manifest 和 image slot，不擦写 LittleFS。W25Q 驱动有共享锁，避免 LittleFS 和 OTA 同时抢 SPI 总线。

## manifest v2

manifest 是 app 和 bootloader 之间的交接契约。v2 主要字段如下：

| 字段 | 含义 |
|---|---|
| `state` | `RECEIVING`、`STAGED`、`PENDING`、`APPLIED`、`ERROR` |
| `image_size` | 待升级 app bin 大小 |
| `image_crc32` | 整包 CRC32 |
| `received_size` | 已接收字节数，用于断点续传 |
| `target_slot` | 目标 slot，A 为 0，B 为 1 |
| `target_app_addr` | 目标 slot 起始地址 |
| `firmware_version` | 新固件版本号 |
| `min_allowed_version` | 允许升级的最低当前版本 |
| `failure_reason` | 失败原因码 |
| `failure_detail` | 失败补充信息 |
| `checkpoint_size` | 断点续传 checkpoint，当前为 4KB |
| `binary_sequence` | 二进制包序号 |

## 防回滚策略

boot state 位于内部 Flash 的两个保留页，采用主备冗余和 CRC。它保存：

- `active_slot`：bootloader 当前要启动的 slot
- `confirmed_slot`：已经被 app 确认可用的 slot
- `pending_slot`：刚升级、等待 app 确认的 slot
- `accepted_version`：已经确认运行过的最高版本
- `pending_version`：当前 pending 固件版本
- `boot_attempts`：pending slot 启动尝试次数
- `sequence` 和 CRC：用于在主备记录中选择最新有效状态

升级时，如果 `firmware_version < accepted_version`，bootloader 拒绝应用并记录 `OTA_FAIL_VERSION_ROLLBACK`。如果设备当前版本低于包里声明的 `min_allowed_version`，也拒绝应用。

## A/B 启动与回滚

完整流程：

1. 当前 app 运行在 A 时，默认 OTA 目标是 B；当前 app 运行在 B 时，默认 OTA 目标是 A。
2. app 接收 bin 到 W25Q image slot，并把 manifest 标记为 `STAGED`。
3. `OTA APPLY` 把 manifest 标记为 `PENDING`，然后软跳 bootloader。
4. bootloader 校验 manifest、镜像头、整包 CRC。
5. bootloader 擦写 inactive slot，并校验写入后的内部 Flash CRC。
6. bootloader 写 boot state：`pending_slot = target_slot`，`active_slot = target_slot`。
7. 新 app 启动后调用 `ota_update_confirm_boot()`。
8. confirm 成功后，boot state 更新 `confirmed_slot` 和 `accepted_version`。
9. 如果 pending slot 多次启动仍未 confirm，bootloader 回滚到上一个 confirmed slot。

当前确认阈值是 `OTA_BOOT_CONFIRM_MAX_ATTEMPTS = 2`。

## 串口传输协议

### 兼容文本协议

旧文本协议仍然保留，便于串口助手和人工调试：

```text
OTA ABORT
OTA BEGIN size=<bin_size> crc=<image_crc32> version=<version> min=<min_version> slot=<A|B|0|1>
OTA DATA off=<offset> len=<len> crc=<chunk_crc32> hex=<hex_data>
OTA END
OTA APPLY
OTA STATUS?
OTA RESUME?
```

`slot`、`version`、`min` 可选。未指定 `slot` 时，app 自动选择当前运行 slot 的另一侧。

### 直接二进制协议

为了提升速度，新增 `OTAB` 二进制包。二进制包避免 hex 膨胀，单包 payload 最大 256 字节。

包头为小端格式：

| 字段 | 大小 | 含义 |
|---|---:|---|
| magic | 4 | `OTAB`，数值 `0x4241544F` |
| header_size | 2 | 当前为 20 |
| payload_len | 2 | payload 字节数 |
| sequence | 4 | 包序号 |
| offset | 4 | 写入 W25Q image slot 的偏移 |
| payload_crc32 | 4 | payload CRC32 |
| payload | N | 原始 app bin 数据 |

UART 收到以 `OTAB` 开头的数据块时，直接走 `ota_binary_process()`；普通 ASCII 行继续走 `ota_command_process()`。

主机侧可以使用仓库内脚本发送二进制 OTA：

```text
python tools/rt103_ota_send.py --port COM5 --image .pio/build/app_b/Ems_0.0.1.bin --version 2 --slot B --apply
```

如果当前运行在 B 槽，下一次应发送 `app_a` bin，并把 `--slot` 改为 `A`。

## 断点续传

app 在接收过程中维护 `received_size` 和运行中的 CRC。为了减少 W25Q manifest 擦写次数，不是每包都写 manifest，而是每跨过 4KB checkpoint 或收完整包时写一次。

断电或串口中断后，PC 可以发送：

```text
OTA RESUME?
```

设备返回当前可续传位置，例如：

```text
OK OTA RESUME receiving off=4096 size=92356 version=2 slot=1
```

PC 从 `off` 位置重新发送。checkpoint 之后已写入但尚未持久化的尾部数据会被覆盖，不影响最终 CRC。

## 失败原因码

manifest v2 的失败码用于售后定位。当前已定义：

| 原因码 | 含义 |
|---|---|
| `OTA_FAIL_NONE` | 无错误 |
| `OTA_FAIL_HEADER_INVALID` | 暂存镜像向量表非法 |
| `OTA_FAIL_IMAGE_CRC_MISMATCH` | W25Q 暂存镜像 CRC 不匹配 |
| `OTA_FAIL_FLASH_ERASE_FAILED` | 内部 Flash 擦除失败 |
| `OTA_FAIL_FLASH_PROGRAM_FAILED` | 内部 Flash 写入失败 |
| `OTA_FAIL_APP_CRC_MISMATCH` | 写入后的 app CRC 不匹配 |
| `OTA_FAIL_VERSION_ROLLBACK` | 版本低于已接受版本 |
| `OTA_FAIL_BOOT_CONFIRM_TIMEOUT` | pending slot 启动后未确认，触发回滚 |
| `OTA_FAIL_BOOT_STATE_WRITE_FAILED` | boot state 写入失败 |

`OTA STATUS?` 会返回 `fail=<reason> detail=<detail>`。

## 为什么 OTA APPLY 仍然用软跳

RT-103 的电源保持依赖 PB10/KEY_OUT。硬复位期间 app 无法继续维持 PB10，板子可能掉电，所以 `OTA APPLY` 不使用 `HAL_NVIC_SystemReset()`。

软跳步骤：

1. app 保持 PB10 高电平。
2. 停 SysTick。
3. 清 PendSV 和 SysTick pending。
4. 清 NVIC enable 和 pending。
5. 切回 HSI。
6. 切 VTOR 到 bootloader。
7. 设置 bootloader MSP。
8. 跳 bootloader ResetHandler。

bootloader 跳 app 前也会清 DMA、USART、SPI、I2C、TIM、ADC 等 app 可见外设，并刷新 STM32F1 Flash prefetch，避免软跳留下的外设状态影响 app 初始化。

## 用户数据如何保存

用户数据保存在 W25Q 的 LittleFS 区，OTA 暂存区和 LittleFS 分离：

- OTA manifest 位于 `0x00000000`
- OTA image slot 位于 `0x00001000`
- LittleFS 位于 `0x00041000`

内部 Flash 的 A/B slot 只保存程序，不保存用户数据。升级和回滚不会主动擦 LittleFS。

## 验证状态

本轮 A/B 增强已完成：

- PowerShell 全量 guard 通过。
- `pio run -e bootloader -e app_a -e app_b` 编译通过。
- app A 和 app B 均约 92KB，小于单 slot 112KB。
- bootloader 约 6.5KB，小于 28KB bootloader 区。

需要继续做的硬件验证：

- 用 COM5 对当前 A 槽设备下发 `app_b` bin，验证 A 到 B。
- 重启后确认 `ota_update_confirm_boot()` 能把 B 标记为 confirmed。
- 再下发 `app_a` bin，验证 B 到 A。
- 人为断电测试：接收中断点续传、apply 过程中断电、pending 未 confirm 回滚。
