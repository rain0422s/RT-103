# RT-103 双阶段 OTA 设计

日期：2026-06-23
项目：RT-103，STM32F103RCT6 环境监测站固件

## 目标

实现完整的双阶段 OTA 升级链路：

- bootloader 固定驻留在内部 Flash 起始区域；
- app 固件从偏移地址运行；
- app 通过现有 USART1 上位机文本命令口接收升级包；
- 升级镜像先写入 W25Q16 外部 Flash 的裸 OTA slot；
- 重启后 bootloader 校验镜像，并把新 app 搬运到内部 Flash app 分区；
- OTA 不破坏 EEPROM 配置和 W25Q16 上的用户数据。

当前固件基线编译结果为 app Flash 约 85.6 KB，RAM 约 35.8 KB。STM32F103RCT6
内部 Flash 为 256 KB，因此第一版可以采用 32 KB bootloader 加 224 KB app 的布局。

## 非目标

- 不在第一版实现无线网络下载。传输来源固定为 USART1 上位机命令口。
- 不让 bootloader 解析 LittleFS。bootloader 只读取外部 Flash 的裸地址 OTA slot。
- 不做应用自刷内部 Flash。擦写 app 分区只由 bootloader 执行。
- 不引入加密签名。第一版使用 magic、大小、地址范围和 CRC32 做完整性保护。

## 内部 Flash 分区

内部 Flash 总大小为 256 KB，起始地址 `0x08000000`。

```text
0x08000000 - 0x08007FFF   bootloader, 32 KB
0x08008000 - 0x0803FFFF   app, 224 KB
```

bootloader 链接脚本只允许使用前 32 KB。app 链接脚本的 `FLASH ORIGIN` 改为
`0x08008000`，长度改为 `224K`。app 启动后必须设置 vector table offset，使中断向量表
指向 `0x08008000`。

bootloader 跳转 app 前需要检查：

- `0x08008000` 处的初始 MSP 位于 SRAM 范围；
- `0x08008004` 处的 reset handler 位于 app Flash 范围；
- 没有 pending OTA，或 pending OTA 校验/刷写失败但旧 app 仍有效。

## 外部 Flash 分区

W25Q16 通过 SPI1 访问，现有 LittleFS 已使用外部 Flash。为了让 bootloader 保持简单，
OTA slot 使用裸地址，LittleFS 固定后移。

```text
0x000000 - 0x000FFF   OTA manifest sector, 4 KB
0x001000 - 0x040FFF   OTA image slot, 256 KB
0x041000 - end        LittleFS user data
```

manifest 与 image slot 分开，便于按 4 KB sector 擦写 manifest 状态。image slot 大小
大于 app 分区，便于放下完整 app bin 和后续少量元数据；bootloader 仍必须拒绝大于 app
分区长度的镜像。

## 用户数据保护

EEPROM 配置数据不属于 OTA 擦写范围。bootloader 不访问 M24C02，也不修改姿态校准、电池
校准、RTC 锚点等配置。

W25Q16 用户数据保存在 LittleFS。OTA 改动会把 LittleFS 起始地址从当前偏移迁移到
`0x041000`。为了保护已有设备数据，app 第一版需要提供一次性迁移逻辑：

- 启动后先检测新 LittleFS 分区是否已有迁移完成标记；
- 如果没有，再尝试按旧 LittleFS 偏移挂载旧文件系统；
- 如果旧文件系统存在，读取当前需要保留的用户数据；
- 格式化或初始化新 LittleFS 分区；
- 把用户数据写回新 LittleFS；
- 在 EEPROM 或新 LittleFS 中写入迁移完成标记。

如果现场确认没有需要保留的历史 W25Q16 用户数据，可以跳过迁移并直接初始化新分区。但代码
设计应保留迁移入口，避免以后现场设备升级时丢数据。

## USART1 OTA 协议

沿用现有 USART1 文本行命令模式。第一版使用 hex 编码数据块，避免二进制数据与当前换行分帧
逻辑冲突。

命令格式：

```text
OTA BEGIN size=<bytes> crc=<crc32>
OTA DATA off=<offset> len=<n> crc=<crc32> hex=<2*n hex chars>
OTA END
OTA APPLY
OTA ABORT
OTA STATUS?
```

行为要求：

- `OTA BEGIN` 校验 size 不为 0，且不超过 app 分区大小；
- `OTA BEGIN` 擦除 OTA image slot 和 manifest sector，并进入接收状态；
- `OTA DATA` 校验 offset、len、hex 长度和块 CRC32，通过后写入 W25Q16；
- `OTA DATA` 必须按 offset 写入，不允许越过声明的 size；
- `OTA END` 读取 OTA slot 计算整包 CRC32，成功后写入 manifest 的 staged 状态；
- `OTA APPLY` 只在 staged 状态下写入 pending 标记，并调用软复位；
- `OTA ABORT` 清除接收状态和 manifest 标记，不擦 LittleFS；
- `OTA STATUS?` 返回 idle、receiving、staged、pending、error 以及已接收字节数。

当前 `UART_LINE_LEN` 为 96。第一版可以先使用较小数据块保证稳定；如果需要提升速度，再为
OTA 模式增加单独的大行缓冲，而不是影响普通命令的内存占用。

## OTA Manifest

manifest 固定放在外部 Flash `0x000000` sector。字段采用固定小端格式，便于 bootloader 和
app 共用同一个头文件。

必须包含：

- magic；
- manifest 结构版本；
- state：idle、receiving、staged、pending、applied、error；
- image_size；
- image_crc32；
- received_size；
- target_app_addr；
- target_app_max_size；
- sequence 或 version；
- manifest_crc32。

bootloader 只接受 state 为 pending 的 manifest。manifest 自身 CRC 不通过、magic 不匹配、
size 越界、target 地址不等于 app 分区地址时，bootloader 不擦 app。

## Bootloader 流程

启动流程：

1. 初始化 HAL、时钟、GPIO、SPI1 和 W25Q16。
2. 读取 manifest。
3. 如果 manifest 不是 pending，跳转现有 app。
4. 如果 manifest pending，校验 manifest、image size、target 地址和整包 CRC32。
5. 校验失败时写入 error 状态，保持旧 app 不动，跳转旧 app。
6. 校验成功后擦除内部 Flash app 分区所需 page。
7. 从 W25Q16 分块读取镜像，按 half-word 写入内部 Flash。
8. 写完后读取内部 Flash 重新计算 CRC32。
9. 校验成功则写入 applied/idle 状态，跳转新 app。
10. 校验失败则写入 error 状态；如果 app 向量仍有效，跳转 app，否则停留在错误指示状态。

刷写过程中如果掉电，下一次启动仍能从 pending manifest 和完整 OTA slot 重新执行刷写。第一版
不要求 page 级断点续写，重刷整个 app 分区即可。

## App 侧改动

app 需要新增 OTA 模块，职责为：

- 解析并执行 USART1 OTA 命令；
- 管理 OTA 接收状态；
- 写入和读取 W25Q16 OTA slot；
- 计算块 CRC32 和整包 CRC32；
- 写入 manifest；
- 在 `OTA APPLY` 后触发 `HAL_NVIC_SystemReset()`；
- 暴露 `OTA STATUS?` 所需状态。

`App/uart_forward.c` 继续作为 USART1 命令入口。OTA 解析逻辑应拆到独立模块，避免把
`uart_process_command()` 继续扩大成一个难维护的大函数。

app 构建需要使用偏移后的链接脚本，并在系统初始化早期设置 vector table。现有业务任务和
传感器、UI、storage 逻辑应保持行为不变。

## 测试和验证

本项目没有完整自动化单元测试，第一版以静态护栏和编译验证为主。

新增 PowerShell 护栏测试，至少检查：

- bootloader/app 分区地址常量一致；
- app 链接脚本从 `0x08008000` 开始；
- bootloader 链接脚本长度不超过 32 KB；
- LittleFS 偏移为 `0x041000` 或等价 block 配置；
- OTA manifest magic、state、CRC 字段存在；
- app 设置 vector table offset；
- `uart_process_command()` 能路由 OTA 命令到独立 OTA 模块。

构建验证：

```bash
pio run -e bootloader
pio run -e app
```

若保留当前默认环境，也可以让默认环境指向 app，确保 `pio run` 仍能生成应用固件。

手工联调成功标准：

- 上位机通过 USART1 发送完整 app bin；
- `OTA END` 能校验整包 CRC；
- `OTA APPLY` 后设备复位；
- bootloader 刷写 app 分区并跳转新固件；
- EEPROM 配置保持不变；
- W25Q16 LittleFS 用户数据在迁移后可继续读取；
- CRC 错误或传输中断时旧 app 不被擦除。

## 风险和取舍

- Bootloader 越大，留给 app 的增长空间越小。因此 bootloader 第一版只保留必要 HAL、SPI、
  W25Qxx、CRC 和 Flash 写入逻辑。
- Hex 编码传输效率低，但与现有文本命令通道兼容，调试简单。速度问题后续可以通过扩大 OTA
  专用行缓冲或改二进制帧解决。
- LittleFS 起始地址后移会带来一次性迁移成本。为了保护用户数据，这个成本比让 bootloader
  解析 LittleFS 更可控。
- 没有签名意味着 CRC 只能防传输损坏，不能防恶意镜像。若设备进入不可信升级环境，后续需要
  增加签名校验。
