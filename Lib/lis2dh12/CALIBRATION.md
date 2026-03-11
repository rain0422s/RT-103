# LIS2DH12 calibration

**参考文档**（项目 `E:\Project\0a\Document` 下）：
- **dm00365457-...-stmicroelectronics.pdf** — AN5005 应用笔记（中文）：引脚、寄存器表、工作模式、启动序列。
- **C110926_姿态传感器-陀螺仪_LIS2DH12TR_规格书_WJ1523476.PDF** — LIS2DH12 数据手册（英文）：电气特性、术语（Sensitivity / Zero-g level）、Factory calibration、SPI/I2C 时序。

## 1. 数据手册要点（据上述两文档）

- **WHO_AM_I** 地址 0x0F。AN5005 表 2 为 `0011 0011`（0x33）；本驱动在初始化时同时接受 0x33 与 0x41，无需改 `LIS2DH12_ID`。
- **OUT_X_L/H ~ OUT_Z_L/H** 0x28~0x2D；多字节读时地址 MSB=1 自动递增（I2C 0x80，SPI 0x40+0x80）。
- **零 g 与校准**：数据手册 3.5 节 “Factory calibration” 说明芯片出厂已校准灵敏度与 zero-g level（TyOff），典型零 g 精度 ±40 mg（Table 4）。若需更高精度，可在软件中做：`校准值 = 原始值 - offset`；芯片无用户可写 offset 寄存器。
- **2g HR 模式**：灵敏度 1 mg/digit（AN5005 表 3、数据手册 Table 4），12 位左对齐，1g ≈ 1024 LSB。
- **REFERENCE 26h**：AN5005 与数据手册均建议，从高分辨率切到掉电再切回时，先读 REFERENCE(26h) 以复位滤波模块。

## 2. 项目中的操作核对（当前使用 SPI）

| 项目           | 数据手册 / 规格书  | 项目实现                         | 状态   |
|----------------|--------------------|----------------------------------|--------|
| 接口           | SPI 或 I2C         | **SPI2**，`dev_ctx.handle = &hspi2` | 已配置 |
| SPI 写         | 地址 \| 0x40 自动增 | `Reg \|= 0x40`                   | 正确   |
| SPI 读         | 地址 \| 0x80 \| 0x40 | `Reg \|= 0xC0`                 | 正确   |
| CS             | 软件 NSS           | `CS_SPI2_GPIO_Port/Pin`（默认 GPIOC, PIN_7） | 可改宏 |
| ODR/FS/Mode    | CTRL_REG1/4        | `data_rate_set` / `full_scale_set` / `operating_mode_set` | 已启用 |

## 3. 校准步骤（参考文档：静止、Z 轴向上）

1. 将传感器**水平放置**，**Z 轴垂直向上**（期望输出约 0, 0, 1g）。
2. 调用 `lis2dh12_calibrate(dev_ctx)`：内部会采集 64 点取平均，计算 offset 使静止时 (0, 0, 1g) 对应输出被减成 (0, 0, 0) 的参考。
3. 将得到的 offset 写入 EEPROM，下次上电加载：
   - `lis2dh12_get_calib_offset(&x, &y, &z);`
   - 填入 `lis2dh12_calib_t`，`storage_save_lis2dh12_calib(&cal);`
4. 上电后：`storage_load_lis2dh12_calib(&cal)`，再 `lis2dh12_set_calib_offset(cal.offset_x, cal.offset_y, cal.offset_z)`。

## 4. API 摘要

- `lis2dh12_get_ctx()`：获取 init 时使用的 `stmdev_ctx_t*`，供 `lis2dh12_read_data` / `lis2dh12_calibrate` 使用。
- `lis2dh12_set_calib_offset(x, y, z)` / `lis2dh12_get_calib_offset(&x, &y, &z)`：设置/读取当前零 g 偏移 (LSB)。
- `lis2dh12_calibrate(dev_ctx)`：执行一次校准并更新内部 offset（需传感器静止、Z 轴向上）。
- `storage_load_lis2dh12_calib` / `storage_save_lis2dh12_calib`：从 EEPROM 读/写 `lis2dh12_calib_t`。

## 5. 文档章节对照（便于翻 PDF）

| 内容         | dm00365457 (AN5005 中文) | C110926 (Datasheet 英文)   |
|--------------|---------------------------|----------------------------|
| 引脚/寄存器表 | §1 引脚说明，§2 寄存器     | §1.2 Pin description, Table 2 |
| 工作模式/ODR  | §3 工作模式，表 3/4       | §3.2.1, Table 10/11        |
| 灵敏度/零 g   | 表 3 So @ ±2g             | §3.1.2 Zero-g level, Table 4 |
| 出厂校准      | —                         | §3.5 Factory calibration   |
| 启动/配置顺序 | §4 启动序列               | —                          |
| REFERENCE 用法 | §3.4 高分辨率模式          | §3.2.1 表注                 |
| SPI 读写信令  | —                         | §6.2, Figure 7–10          |

## 6. 示例：运行一次校准并保存

```c
#include "read_data_simple.h"
#include "storage.h"

stmdev_ctx_t *ctx = lis2dh12_get_ctx();
if (lis2dh12_calibrate(ctx) == 0) {
    lis2dh12_calib_t cal;
    cal.magic = EEPROM_CALIB_MAGIC;
    lis2dh12_get_calib_offset(&cal.offset_x, &cal.offset_y, &cal.offset_z);
    storage_eeprom_init();
    storage_save_lis2dh12_calib(&cal);
}
```
