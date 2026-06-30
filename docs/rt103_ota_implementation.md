# RT-103 双阶段 OTA 实现思路

更新时间：2026-06-30

适用项目：RT-103，STM32F103RCT6，PlatformIO，FreeRTOS，外部 W25Q16 Flash，串口 COM5。

## 这篇文章解决什么问题

RT-103 只有串口可用，没有 ST-Link 作为量产升级通道。目标是在设备已经运行 app 的情况下，通过串口下发新的 app bin，让设备自己完成升级，并且不破坏用户数据。

这个项目最终采用“双阶段 OTA”：

- app 负责接收 bin，并暂存到 W25Q 外部 Flash
- bootloader 负责校验暂存镜像，并写入 STM32 内部 Flash 的 app 区
- app 和 bootloader 之间通过 manifest 状态机交接
- `OTA APPLY` 使用软跳 bootloader，而不是硬复位

## 常见误区

不要把 OTA 做成“app 直接写自己所在的内部 Flash”。这样风险很高：

- app 正在从内部 Flash 执行，直接擦写 app 区容易把自己擦掉
- 写入中断电后容易留下不可启动镜像
- app 侧有 FreeRTOS、DMA、串口、LittleFS 等运行时状态，直接切换风险更大

也不要把用户数据和 OTA 暂存区混在一起。RT-103 的用户数据主要在 W25Q 的 LittleFS 区，OTA image slot 必须单独规划，避免升级时擦掉用户数据。

## 总体架构

整体链路是：

1. PC 读取 app bin
2. PC 通过串口发 ASCII OTA 命令
3. app 接收命令，把 bin 分包写入 W25Q OTA image slot
4. app 写 manifest，记录镜像大小、CRC、接收进度和状态
5. `OTA END` 校验整包 CRC，通过后标记为 `STAGED`
6. `OTA APPLY` 把 manifest 标记为 `PENDING`
7. app 保持 PB10/KEY_OUT 高电平，软跳到 bootloader
8. bootloader 读取 manifest，发现 `PENDING`
9. bootloader 校验 W25Q 镜像头和整包 CRC
10. bootloader 擦写内部 Flash app 区
11. bootloader 校验写入后的内部 Flash app CRC
12. bootloader 标记 `APPLIED`
13. bootloader 清理运行状态，跳转到新 app

## Flash 布局

内部 Flash：

| 区域 | 地址 | 用途 |
|---|---:|---|
| bootloader | `0x08000000` | 固定 32KB，引导和 OTA 应用逻辑 |
| app | `0x08008000` | 主应用固件 |

W25Q 外部 Flash：

| 区域 | 地址 | 用途 |
|---|---:|---|
| manifest | `0x00000000` | OTA 状态、大小、CRC、校验 |
| image slot | `0x00001000` | 暂存 app bin |
| LittleFS | `0x00041000` | 用户数据文件系统 |

这个布局保证了 OTA 暂存镜像不会覆盖 LittleFS 用户数据。

## 串口传输协议

PC 不直接发送裸二进制流，而是把 bin 按 64 字节切片，每片转成 hex 字符串，再通过 ASCII 命令发送。

命令顺序：

```text
OTA ABORT
OTA BEGIN size=<bin总大小> crc=<整包CRC32>
OTA DATA off=<偏移> len=<本包长度> crc=<本包CRC32> hex=<本包hex字符串>
OTA DATA ...
OTA END
OTA APPLY
```

示例：

```text
OTA BEGIN size=90732 crc=477DA01C
OTA DATA off=0 len=64 crc=XXXXXXXX hex=<128个hex字符>
OTA END
OTA APPLY
```

设计取舍：

- 使用 ASCII hex，方便串口助手和日志观察
- 每包带 offset，app 可以拒绝乱序包
- 每包带 CRC32，能快速发现单包损坏
- 整包再做 CRC32，避免“单包都对但整体不完整”
- 单包最大 64 字节，串口行长同步扩到 192 字节

## app 侧做什么

app 的 OTA 入口是 `ota_command_process()`，主要处理：

- `OTA BEGIN`：检查 size 和整包 CRC，清 manifest，擦 W25Q image slot，写入 `RECEIVING` manifest
- `OTA DATA`：检查 offset、len、chunk CRC，把数据写入 W25Q image slot
- `OTA END`：检查 received size 和整包 CRC，通过后标记 `STAGED`
- `OTA APPLY`：检查 bootloader 向量有效，把 manifest 改成 `PENDING`，软跳 bootloader
- `OTA STATUS?`：返回当前状态和进度
- `OTA ABORT`：中止当前 OTA，并清 manifest

app 侧不写内部 Flash app 区，只写 W25Q 暂存区。

## manifest 状态机

manifest 是 app 和 bootloader 的交接契约。

| 状态 | 含义 |
|---|---|
| `RECEIVING` | 正在接收串口分包 |
| `STAGED` | app 已收到完整镜像，并且整包 CRC 通过 |
| `PENDING` | 用户执行了 `OTA APPLY`，等待 bootloader 应用 |
| `APPLIED` | bootloader 已经把镜像写入内部 Flash |
| `ERROR` | bootloader 校验或写入失败 |

bootloader 只处理 `PENDING`。如果不是 `PENDING`，bootloader 直接跳 app。

## bootloader 侧做什么

bootloader 启动后会先拉高 PB10/KEY_OUT，维持板子上电，然后初始化本地 GPIO、SPI1、W25Q。

如果 manifest 是 `PENDING`，bootloader 会按顺序做：

1. 读取 manifest
2. 校验 manifest 自身合法性
3. 校验 W25Q image header
   - MSP 必须在 SRAM 范围
   - ResetHandler 必须在 app Flash 范围
   - ResetHandler 必须是 Thumb 地址
4. 校验 W25Q image CRC
5. 擦除内部 Flash app 区
6. 从 W25Q 分块读取并写入 `0x08008000`
7. 校验内部 Flash app CRC
8. 标记 `APPLIED`
9. 跳转 app

## 为什么 OTA APPLY 用软跳

RT-103 的硬件电源保持依赖 PB10/KEY_OUT。硬复位期间 app 不能继续维持 PB10，板子可能掉电，所以 `OTA APPLY` 不使用 `HAL_NVIC_SystemReset()`。

当前做法是：

- app 在离开前保持 PB10 高电平
- 停 SysTick
- 清 PendSV 和 SysTick pending
- 清 NVIC enable 和 pending
- 切回 HSI
- 切 VTOR 到 bootloader
- 设置 bootloader MSP
- 跳 bootloader ResetHandler

这样能在不掉电的情况下进入 bootloader。

## bootloader 跳 app 前为什么要清理状态

软跳不是硬复位，很多外设状态不会自动回到复位值。联调时发现，如果 bootloader 直接跳 app，app 会卡在 DMA 初始化附近。

最终在 bootloader 跳 app 前增加：

- 复位 app 会重新使用的 DMA、USART、SPI、I2C、TIM、ADC
- 清 DMA 通道和 DMA interrupt flags
- 清 NVIC enable 和 pending
- 清 PendSV 和 SysTick pending
- 刷新 STM32F1 Flash prefetch
- 设置 VTOR 到 app base
- 设置 MSP 到 app vector table 的初始栈

这样让 app 入口更接近硬复位后的状态。

## 用户数据如何保存

用户数据保存在 W25Q 的 LittleFS 区，OTA 暂存区和 LittleFS 区分离：

- OTA manifest 在 `0x00000000`
- OTA image slot 从 `0x00001000` 开始
- LittleFS 从 `0x00041000` 开始

OTA 擦写只覆盖 manifest 和 image slot，不擦 LittleFS。内部 Flash 只擦 app 区，不擦 bootloader。

同时 W25Q 驱动增加了共享锁，LittleFS 和 OTA 不会同时抢 SPI 总线。

## 第一次烧录方式

第一次需要通过 STM32 ROM bootloader 烧入合并镜像：

- bootloader 烧到 `0x08000000`
- app 烧到 `0x08008000`

硬件控制线固定为：

- RTS 高电平复位
- RTS 低电平释放复位
- DTR 低电平进入 STM32 ROM bootloader
- DTR 高电平正常从 Flash 启动

因为板子上电需要按 SW1，进入 ROM bootloader 烧录时必须按住 SW1，直到写入和读回校验结束。

## 验证结果

最终生产版验证过：

- ROM 串口刷入合并镜像，写入并读回校验通过
- app 正常启动，日志到达 `[ui_task] entering main loop`
- 串口 OTA 完整下发 app bin
- `OTA BEGIN`、`OTA DATA`、`OTA END`、`OTA APPLY` 均返回 OK
- `OTA APPLY` 后直接回到 app 主循环
- 全量 guard 脚本通过

## 风险和后续优化

当前方案已经能完成 OTA，但仍有几个后续可以增强的点：

- 增加版本号和防回滚策略
- 增加断点续传能力
- 增加传输压缩或直接二进制协议，提高速度
- 增加 A/B app 分区，提高断电恢复能力
- 增加 manifest 失败原因码，方便售后定位

当前版本的重点是先把“串口下发、外部 Flash 暂存、bootloader 应用、保护用户数据、软跳不掉电”这条主链路跑通。
