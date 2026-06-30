# RT-103 OTA 联调问题定位与解决复盘

更新时间：2026-06-30

适用项目：RT-103，STM32F103RCT6，串口 COM5，双阶段 OTA。

## 这篇文章解决什么问题

这篇文章不是单纯记录“做了 OTA”，而是复盘 RT-103 OTA 从串口烧录、app 暂存、bootloader 应用到软跳回 app 的完整问题闭环。

重点回答：

- 为什么不能只靠硬复位进入 bootloader
- 为什么 OTA image 写入成功后仍然可能不启动
- 为什么 app 和 bootloader 软跳时要额外清外设状态
- 为什么 W25Q 必须加共享锁
- 最终怎么证明 OTA 是真的闭环成功

## 常见浅层判断

这类问题很容易被误判成下面几类：

- 串口不通，就直接怀疑波特率
- app 不启动，就直接怀疑 vector table
- OTA BEGIN 失败，就直接怀疑 W25Q 坏了
- bootloader 已经打印跳 app，就认为 app 肯定执行了
- 硬复位能恢复，就认为软跳不需要处理

这些判断都只看到了局部现象。RT-103 的真实问题横跨硬件上电保持、串口控制线、W25Q 并发访问、bootloader 状态机、FreeRTOS 和 DMA 外设状态。

## 对方真正关心什么

这次联调真正考验的不是“会不会写 OTA 命令”，而是：

- 能不能把启动链路拆清楚
- 能不能区分硬件掉电、ROM bootloader、app bootloader、app runtime
- 能不能用日志和读回校验缩小问题边界
- 能不能找到软跳和硬复位的关键差异
- 能不能保护用户数据和电源保持
- 能不能把一次成功变成可重复验证的机制

## 总方法

这类问题不能先猜代码。有效方法是：

1. 先固定硬件事实
   - RTS 高电平复位
   - RTS 低电平释放复位
   - DTR 低电平进入 STM32 ROM bootloader
   - DTR 高电平正常 Flash 启动
   - 板子需要 SW1 上电，app 通过 PB10/KEY_OUT 维持上电

2. 再拆启动链路
   - PC 串口控制线
   - STM32 ROM bootloader
   - 自定义 bootloader
   - app ResetHandler
   - app 外设初始化
   - FreeRTOS 任务启动

3. 再用证据逐层定位
   - ROM 同步 ACK
   - Flash 写入和读回校验
   - bootloader 阶段字母日志
   - app 早期阶段日志
   - app 主循环日志
   - OTA 命令回复和 manifest 状态

4. 最后只修已证实的边界
   - W25Q 并发访问问题用共享锁解决
   - 软跳不等于硬复位的问题用外设和内核状态清理解决
   - 电源保持问题用 PB10 保持和避免硬复位解决

## 问题一：COM5 消失和 ROM 串口烧录不稳定

### 背景

第一次烧录需要通过 STM32 ROM bootloader，把 bootloader 写到 `0x08000000`，把 app 写到 `0x08008000`。

硬件只有串口，并且板子不是插上 USB 就稳定上电，而是需要按 SW1。app 启动后会通过 PB10/KEY_OUT 输出高电平维持上电。

### 现象

烧录时出现：

- COM5 偶尔消失
- ROM 同步前出现 Windows `PermissionError(13)`
- 脚本无法打开 COM5
- 屏幕不亮，app 没有日志

### 定位

这些失败发生在 ROM 同步 ACK 之前，说明 Flash 还没有被擦写。串口设备消失更像是板子掉电或 CH340 重新枚举，而不是 STM32 协议错误。

关键事实是：进入 ROM bootloader 或复位期间，app 不运行，PB10 不能靠 app 维持高电平。因此如果 SW1 没按住，板子会掉电。

### 根因

烧录时没有持续按住 SW1，导致复位或进入 ROM bootloader 时板子掉电，COM5 重新枚举或消失。

### 方案

固定烧录动作：

- DTR 低电平，选择 STM32 ROM bootloader
- RTS 高电平，保持复位
- 用户按住 SW1
- 等待足够时间
- RTS 低电平释放复位
- 发送 `0x7F` 同步
- 收到 ACK 后擦写和读回校验
- 用户保持 SW1 到 `VERIFY_OK`

### 收益

使用自定义 ROM 烧录脚本后，合并镜像可以稳定写入并读回校验通过。

### 风险

如果用户提前松开 SW1，COM5 仍会掉线。这个是硬件上电保持条件决定的，不是软件能完全绕开的。

## 问题二：bootloader 早期卡住

### 背景

bootloader 需要初始化 GPIO、SPI1、W25Q，并在没有 OTA 时跳 app。

### 现象

调试时 bootloader 早期日志停在某个阶段，后续没有进入 app。

### 定位

诊断日志显示 bootloader 走到 `HAL_Delay()` 附近后停止。bootloader 工程没有完整 app 的 FreeRTOS 环境，SysTick tick 处理必须由 bootloader 自己提供。

### 根因

bootloader 使用 HAL delay，但没有本地 `SysTick_Handler()` 增加 HAL tick，导致 `HAL_Delay()` 不能正常结束。

### 方案

bootloader 增加本地 SysTick tick handler。

### 收益

bootloader 能继续完成 GPIO、SPI、W25Q 初始化，并正常跳 app。

### 风险

bootloader 不应复用 app 的完整初始化逻辑。它应该只初始化自己需要的最小硬件，否则容易影响 PB10 上电保持和 app 入口状态。

## 问题三：OTA BEGIN 返回 ERR OTA FLASH

### 背景

app 收到 `OTA BEGIN` 后，需要清 manifest 并擦除 W25Q OTA image slot。

### 现象

小数据量测试偶尔能过，大一点的 OTA BEGIN 或擦除阶段容易返回 `ERR OTA FLASH`。

### 定位

W25Q 同时被两类代码访问：

- LittleFS 存储任务
- OTA 暂存写入

LittleFS 自己有锁，但 OTA 直接调用 W25Q 驱动。也就是说两个路径没有共享同一把锁。

### 根因

W25Q SPI 访问存在并发竞争。LittleFS 和 OTA 可能同时操作 W25Q，导致擦除或写入失败。

### 方案

在 W25Q 驱动层增加共享锁：

- app build 使用 FreeRTOS semaphore
- bootloader build 不使用 FreeRTOS，锁为空操作
- LittleFS port 进入文件系统操作时同时拿 W25Q 锁
- OTA store 读写 manifest 和 image slot 时也拿 W25Q 锁

### 收益

`OTA BEGIN` 能稳定擦除 image slot，完整 OTA DATA 能写完，`OTA END` 能校验通过。

### 风险

锁粒度不能太小。擦除 image slot 时需要覆盖整个擦除过程，避免中途被 LittleFS 插入访问。

## 问题四：OTA APPLY 后 bootloader 已跳 app，但 app 没起来

### 背景

`OTA APPLY` 不做硬复位，而是 app 软跳到 bootloader。bootloader 写完 app 后再软跳回 app。

### 现象

诊断版 bootloader 日志显示：

- manifest 是 `PENDING`
- 镜像头校验通过
- W25Q image CRC 通过
- 内部 Flash 写入完成
- 内部 Flash app CRC 通过
- bootloader 执行到跳 app 前

但是之后没有 app 主循环日志。硬复位后 app 又能正常启动。

### 定位

先加 bootloader 字母日志，确认卡点不在 W25Q 镜像，也不在内部 Flash 写入。

再加 app 超早期诊断，确认 app 的 `SystemInit()` 和 `main()` 能进入。

最后在 app 初始化阶段逐步打点，发现 OTA APPLY 后可以走到 `MX_GPIO_Init()` 后，但卡在 `MX_DMA_Init()` 附近。正常硬复位启动同一份代码可以通过 `MX_DMA_Init()`。

### 根因

软跳不等于硬复位。bootloader 跳 app 前，DMA、外设中断标志、NVIC pending、系统异常 pending、Flash prefetch 等状态没有完全恢复到硬复位入口状态。

app 在 `MX_DMA_Init()` 里启用 DMA 中断时，旧的 DMA pending 或外设残留状态可能立即进入新 app 的中断路径，导致初始化阶段卡住。

### 方案

bootloader 跳 app 前增加复位式清理：

- 停 SysTick
- 清 PendSV 和 SysTick pending
- 清 NVIC enable 和 pending
- 清 DMA1、DMA2 通道
- 清 DMA interrupt flags
- 复位 app 会重新初始化的 USART、SPI、I2C、TIM、ADC
- 刷新 STM32F1 Flash prefetch
- 切 VTOR 到 app base
- 设置 MSP、PSP、CONTROL、BASEPRI、FAULTMASK
- 再跳 app ResetHandler

同时不能调用 `HAL_DeInit()`，因为它会重置 GPIO，可能导致 PB10/KEY_OUT 掉电。

### 收益

修复后，生产版端到端 OTA 验证通过：

- `OTA BEGIN` 返回 OK
- 全部 app bin 分包写入
- `OTA END` 返回 OK
- `OTA APPLY` 返回 OK
- bootloader 应用镜像
- app 直接回到 `[ui_task] entering main loop`

### 风险

bootloader 跳 app 前复位外设时不能复位 GPIOB，否则 PB10 可能掉电。清理范围要覆盖 app runtime 外设，但避开电源保持所依赖的 GPIO。

## 问题五：OTA DATA 中途主机收不到 OK

### 背景

串口 OTA 使用 ASCII 命令，每包 app bin 转成 hex 发送，app 每包回复 `OK OTA DATA`。

### 现象

某次传输中，主机等待某包 `OK OTA DATA` 超时，但串口还能看到 app 的其他运行日志。

### 定位

发送 `OTA STATUS?` 后，设备返回的 received size 已经超过主机认为失败的 offset。

说明设备实际已经收到并写入该包，只是主机没有读到对应 OK 回复，可能被日志穿插、串口读取节奏或主机脚本超时影响。

### 根因

这是传输控制层的确认丢失问题，不是固件写入失败。

### 方案

PC 端脚本增加恢复机制：

- 某包等待 OK 超时后，不立刻失败
- 发送 `OTA STATUS?`
- 如果设备 reported received size 大于主机 offset，则主机跳到设备 reported offset 继续发
- 如果 received size 没变，则重发当前 offset

### 收益

最终生产版 OTA 过程中出现过 1 次 DATA 回复超时，但通过 `OTA STATUS?` 恢复后继续完成升级。

### 风险

当前协议仍是 ASCII hex，效率不高。后续如果需要更快，可以设计二进制协议，但要保留 offset、chunk CRC、整包 CRC 和状态查询能力。

## 最终验证

最终验证使用生产版，关闭临时诊断：

- bootloader 位于 `0x08000000`
- app 位于 `0x08008000`
- ROM 串口刷入合并镜像，读回校验通过
- app 正常启动并进入主循环
- app 响应 `OTA STATUS?`
- 串口完整下发 app bin
- `OTA END` 后状态为 `STAGED`
- `OTA APPLY` 后软跳 bootloader
- bootloader 写入内部 Flash app 区
- app 重新进入主循环
- 全量 guard 脚本通过

## 副作用和取舍

### 为什么不用硬复位

硬复位会让 app 无法维持 PB10/KEY_OUT，板子可能掉电。软跳更符合当前硬件条件。

### 为什么不用 app 直接写内部 Flash

app 自己擦写 app 区风险更高。bootloader 写 app 区更安全，失败时也能保留引导能力。

### 为什么还需要外部 W25Q

W25Q 提供 staging 区。app 先把完整镜像存好并校验，再让 bootloader 应用，避免半包数据直接影响内部 Flash。

### 为什么需要共享锁

LittleFS 和 OTA 都访问 W25Q。没有共享锁时，偶发失败很难复现，也很难定位。锁把问题从随机时序问题变成确定串行访问。

## 如果以后继续追问

### 怎么证明不是 app bin 错了

bootloader 校验了 W25Q image header 和整包 CRC；写入内部 Flash 后又校验 app CRC。硬复位后同一 app 也能启动。

### 怎么证明不是串口传输错了

每包有 chunk CRC，整包有 image CRC。`OTA END` 只有整包 CRC 通过才会返回 OK。

### 怎么证明用户数据没丢

OTA image slot 和 LittleFS 区分离，OTA 擦写范围不到 `0x00041000` 后的 LittleFS。实际启动日志也能看到 LittleFS 文件和 boot_count 继续存在。

### 如果升级中断电怎么办

当前方案能保证 bootloader 不被擦掉，manifest 状态可用于判断升级是否完成。但 app 单分区方案在内部 Flash 写入阶段断电仍有风险。更强方案是 A/B app 分区或增加恢复镜像。

## 收尾

这次 OTA 最难的不是串口命令本身，而是把“串口控制线、硬件上电保持、W25Q 并发、manifest 状态机、bootloader 写 Flash、软跳状态清理”这几条链路同时闭环。

真正稳定的 OTA 不是一次能升级成功，而是每个阶段都有边界、有校验、有状态、有失败处理，并且能保护用户数据和电源保持。
