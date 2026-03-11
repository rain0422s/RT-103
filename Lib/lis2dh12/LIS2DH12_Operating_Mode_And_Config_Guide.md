# LIS2DH12 工作模式与配置指南

**参考文档**（项目 `E:\Project\0a\Document` 下）：
- **dm00365457-...-stmicroelectronics.pdf** — AN5005 应用笔记（中文）：引脚、寄存器、工作模式、启动序列、FIFO、中断。
- **C110926_姿态传感器-陀螺仪_LIS2DH12TR_规格书_WJ1523476.PDF** — 数据手册（英文）：电气特性、时序、Factory calibration、SPI/I2C。

**代码实现**：`Lib/lis2dh12/lis2dh12_config.h`、`lis2dh12_config.c` 提供以下封装 API，可直接调用（需先 `lis2dh12_init()` 并取 `lis2dh12_get_ctx()`）：
- 四种模式：`lis2dh12_config_set_mode()`，导通时间：`lis2dh12_config_settling_ms()`
- 数据读取：`lis2dh12_config_enable_bdu()`、`lis2dh12_config_enable_drdy_int1()`、`lis2dh12_config_poll_once()`
- FIFO：`lis2dh12_config_fifo_enable()`、`lis2dh12_config_fifo_set_mode()`、`lis2dh12_config_fifo_set_watermark()`、`lis2dh12_config_fifo_int1()`、`lis2dh12_config_fifo_batch_read()`
- 中断：`lis2dh12_config_int1_free_fall()`、`lis2dh12_config_int1_6d_4d()`、`lis2dh12_config_int1_click()`、`lis2dh12_config_int1_clear_source()`
- 自检：`lis2dh12_config_self_test_run()`
- 活动/不活动：`lis2dh12_config_activity_inactivity()`

---

## 1. 工作模式配置指南

### 1.1 四种工作模式

| 模式           | 说明           | 配置方法 |
|----------------|----------------|----------|
| **掉电 (Power-down)** | 不采样，功耗最低 | CTRL_REG1：ODR = 0000 |
| **低功耗 (Low-power, LP)** | 8 位数据，低功耗 | CTRL_REG1：LPEN=1，ODR≠0；CTRL_REG4：HR=0，且为 NM/LP 模式 |
| **正常 (Normal, NM)** | 10 位数据       | CTRL_REG1：LPEN=0，ODR≠0；CTRL_REG4：HR=0，mode=NM |
| **高分辨率 (High-resolution, HR)** | 12 位数据，精度最高 | CTRL_REG1：LPEN=0，ODR≠0；CTRL_REG4：HR=1 |

驱动中对应枚举（`lis2dh12_reg.h`）：
- **ODR**：`LIS2DH12_POWER_DOWN`(0)、`LIS2DH12_ODR_1Hz`～`LIS2DH12_ODR_400Hz`、以及 LP 专用 1.344 kHz / 5.376 kHz。
- **模式**：`LIS2DH12_HR_12bit`(0)、`LIS2DH12_NM_10bit`(1)、`LIS2DH12_LP_8bit`(2)。

### 1.2 模式切换条件与寄存器

- **掉电 → 任意工作模式**：写 CTRL_REG1 使 ODR ≠ 0，并设置 CTRL_REG4 的 FS、HR、BDU 等。
- **工作模式 → 掉电**：写 CTRL_REG1 使 ODR = 0（Power-down）。
- **正常/高分辨率 ↔ 低功耗**：改 CTRL_REG1 的 LPEN 与 CTRL_REG4 的 HR / 分辨率模式。

**注意**：从**高分辨率**切到**掉电**再切回**高分辨率**时，数据手册建议先**读 REFERENCE(0x26)** 再切模式，以复位内部滤波状态。

---

## 2. 模式切换导通时间（Settling time）

数据手册与 AN5005 约定：

| 场景                     | 导通时间        | 说明 |
|--------------------------|-----------------|------|
| 仅改变 **ODR**（同模式） | **1/ODR**       | 等待一个采样周期后数据稳定 |
| 从 **掉电** 进入工作模式 | **7/ODR**       | 上电稳定需约 7 个采样周期 |

示例：ODR = 100 Hz → 仅改 ODR 时约 10 ms；从掉电唤醒约 70 ms 后再使用数据。

**推荐**：切换模式或 ODR 后延时 `7/ODR` 再读（或轮询 STATUS_REG 的 ZYXDA）。

---

## 3. 数据读取三种方案

### 3.1 轮询 STATUS_REG (0x27)

- **ZYXDA**：X/Y/Z 均有新数据时置 1。
- 流程：循环读 STATUS_REG，若 `zyxda == 1` 再读 OUT_X_L(0x28)～OUT_Z_H(0x2D)（建议一次多字节读 6 字节）。

```c
lis2dh12_status_reg_t status;
do {
    lis2dh12_status_get(ctx, &status);
} while (!status.zyxda);
int16_t raw[3];
lis2dh12_acceleration_raw_get(ctx, raw);
```

### 3.2 DRDY 中断（INT1）

- 将 **数据就绪** 映射到 INT1 引脚：CTRL_REG3 的 **I1_ZYXDA = 1**。
- 主机在 INT1 中断/回调里读加速度，避免轮询。

```c
lis2dh12_ctrl_reg3_t c3 = { 0 };
c3.i1_zyxda = 1;  /* INT1 = 数据就绪 */
lis2dh12_pin_int1_config_set(ctx, &c3);
/* 配置 MCU 外部中断在 INT1 下降沿触发，在 ISR 中读 lis2dh12_acceleration_raw_get() */
```

### 3.3 BDU（块更新）防高低字节错位

- 使能 **CTRL_REG4 BDU=1**：读 OUT_X_L 时锁定同一采样时刻的 X_L/X_H…Z_L/Z_H，直到整组 6 字节读完再更新。
- 在中断或高 ODR 下强烈建议开启 BDU，避免读过程中数据更新导致半字节错位。

```c
lis2dh12_block_data_update_set(ctx, 1);
```

---

## 4. FIFO 全功能详解

### 4.1 四种 FIFO 模式（FIFO_CTRL_REG 0x2E：FM[1:0]）

| FM | 模式名称           | 说明 |
|----|--------------------|------|
| 00 | **Bypass**         | FIFO 关闭，FIFO 仅作 1 级缓冲，等同于直通 |
| 01 | **FIFO mode**      | 写满 FTH 后停止写入，等主机读空后再采 |
| 10 | **Stream**         | 循环覆盖，持续保存最近 FTH 个样本 |
| 11 | **Stream-to-FIFO** | 由 INT1/INT2 触发后从 Stream 转 FIFO 模式，用于捕获事件前后数据 |

驱动枚举：`LIS2DH12_BYPASS_MODE`、`LIS2DH12_FIFO_MODE`、`LIS2DH12_DYNAMIC_STREAM_MODE`、`LIS2DH12_STREAM_TO_FIFO_MODE`。

### 4.2 水印、溢出、空标志（FIFO_SRC_REG 0x2F）

| 位/字段   | 含义 |
|-----------|------|
| **FSS[4:0]** | 当前 FIFO 内样本数（0～31） |
| **EMPTY**   | 1 = FIFO 空 |
| **OVRN_FIFO** | 1 = 发生溢出（新数据覆盖未读） |
| **WTM**     | 1 = 达到水印（样本数 ≥ FTH） |

- **水印 FTH**：FIFO_CTRL_REG 低 5 位 FTH[4:0]（0～31），表示“满”的阈值。
- 可将 **WTM** 或 **OVRN_FIFO** 映射到 INT1/INT2（CTRL_REG3/CTRL_REG6），用于批量读取或溢出告警。

### 4.3 批量读取方法与速度要求

- 一次多字节读 OUT_X_L(0x28)，长度 6×N（N = 要读的样本数），利用 SPI/I2C 地址自动递增。
- **速度**：读 FIFO 的速率建议 **≥ ODR 产生数据的速率**，否则会溢出。例如 ODR=400 Hz，每样本 6 字节，则 2400 字节/秒；批量读 32 样本需 192 字节，应在 80 ms 内读完。

```c
/* 使能 FIFO Stream，水印 16，BDU 开启 */
lis2dh12_fifo_set(ctx, 1);
lis2dh12_fifo_mode_set(ctx, LIS2DH12_DYNAMIC_STREAM_MODE);
lis2dh12_fifo_watermark_set(ctx, 16);
lis2dh12_block_data_update_set(ctx, 1);

/* 轮询或中断：FIFO_SRC WTM/OVRN 后，按 FSS 读 FSS*6 字节 */
lis2dh12_fifo_src_reg_t src;
lis2dh12_fifo_status_get(ctx, &src);
if (src.wtm || src.ovrn_fifo) {
    uint8_t n = src.fss;
    for (; n > 0; n--) {
        int16_t raw[3];
        lis2dh12_acceleration_raw_get(ctx, raw);
        /* 处理 raw[] */
    }
}
```

---

## 5. 中断功能实战配置

### 5.1 自由落体、唤醒、6D/4D、单击/双击

- **INT1_CFG(0x30) / INT2_CFG**：  
  - **AOI**：与(0)/或(1) 组合；**6D**：1=6 方向检测，0=4 方向。  
  - **XHIE/XLIE、YHIE/YLIE、ZHIE/ZLIE**：各轴高/低阈值使能（自由落体常用 Z 轴低、6D 常用各轴高低）。
- **INT1_THS(0x32) / INT1_DURATION(0x33)**：阈值（7 位）与持续时间（7 位，单位 1/ODR）。
- **自由落体**：三轴均在阈值内且持续一段时间 → 设 THS 较小、DURATION 短，使能 XL/YL/ZL。
- **唤醒**：通常用 INT2 的活动/不活动（见下节），或 INT1 阈值超限。
- **6D/4D 朝向**：INT1_CFG 中 **6d=1**（6D）或 **6d=0**（4D），并设 XHIE/XLIE/YHIE/YLIE/ZHIE/ZLIE；THS 设姿态变化阈值。
- **单击/双击**：  
  - **CLICK_CFG(0x38)**：XS/XD/YS/YD/ZS/ZD 使能单(click)/双(double)击轴。  
  - **CLICK_THS(0x3A)**：阈值；**TIME_LIMIT(0x3B)**：双击时间窗上限；**TIME_LATENCY(0x3C)**：双击两次间隔；**TIME_WINDOW(0x3D)**：第二次敲击检测窗。  
  - **CTRL_REG3 I1_CLICK=1** 将 Click 映射到 INT1；读 **CLICK_SRC(0x39)** 清除并区分单/双击与轴。

### 5.2 阈值、时长、时序寄存器一览

| 功能       | 寄存器           | 说明 |
|------------|------------------|------|
| INT1 阈值  | INT1_THS 0x32    | 7 位，与量程/模式相关（见数据手册 LSB↔mg） |
| INT1 时长  | INT1_DURATION 0x33 | 7 位，单位 1/ODR |
| INT2 阈值  | INT2_THS 0x36    | 同上 |
| INT2 时长  | INT2_DURATION 0x37 | 同上 |
| 单击阈值   | CLICK_THS 0x3A   | 7 位 |
| 双击时间限 | TIME_LIMIT 0x3B  | 7 位 |
| 双击间隔   | TIME_LATENCY 0x3C | 8 位 |
| 双击窗口   | TIME_WINDOW 0x3D  | 8 位 |

### 5.3 可直接复制的配置代码步骤（示例）

```c
#include "lis2dh12_reg.h"

/* 1) 基础：2g HR，100Hz，BDU，REFERENCE 若从掉电恢复 HR 则先读 REFERENCE */
uint8_t ref[1];
lis2dh12_filter_reference_get(ctx, ref);
lis2dh12_data_rate_set(ctx, LIS2DH12_ODR_100Hz);
lis2dh12_full_scale_set(ctx, LIS2DH12_2g);
lis2dh12_operating_mode_set(ctx, LIS2DH12_HR_12bit);
lis2dh12_block_data_update_set(ctx, 1);

/* 2) INT1 = 数据就绪 DRDY */
lis2dh12_ctrl_reg3_t c3 = { 0 };
c3.i1_zyxda = 1;
lis2dh12_pin_int1_config_set(ctx, &c3);

/* 3) INT1 自由落体：三轴低于阈值，持续 1 个 ODR */
lis2dh12_int1_cfg_t icfg = { 0 };
icfg.xlie = 1;
icfg.ylie = 1;
icfg.zlie = 1;
icfg.aoi  = 1;   /* 与 */
lis2dh12_int1_gen_conf_set(ctx, &icfg);
lis2dh12_int1_gen_threshold_set(ctx, 20);   /* 约 20*16mg@2g LP，按实际量程换算 */
lis2dh12_int1_gen_duration_set(ctx, 1);

/* 4) INT1 同时响应 Click：先配置 Click 寄存器，再在 CTRL_REG3 中加上 i1_click（与上面 c3 合并） */
lis2dh12_click_cfg_t click_cfg = { 0 };
click_cfg.xs = 1;
click_cfg.yd = 1;  /* X 单击，Y 双击示例 */
lis2dh12_tap_conf_set(ctx, &click_cfg);
lis2dh12_tap_threshold_set(ctx, 10);
lis2dh12_write_reg(ctx, LIS2DH12_TIME_LIMIT, (uint8_t[]){ 10 }, 1);
lis2dh12_write_reg(ctx, LIS2DH12_TIME_LATENCY, (uint8_t[]){ 80 }, 1);
lis2dh12_write_reg(ctx, LIS2DH12_TIME_WINDOW, (uint8_t[]){ 255 }, 1);
c3.i1_click = 1;   /* 与 c3.i1_zyxda=1 一起，INT1 = DRDY 或 Click */
lis2dh12_pin_int1_config_set(ctx, &c3);
```

---

## 6. 温度传感器与自检流程

### 6.1 温度

- **TEMP_CFG_REG(0x1F)**：temp_en = 11 使能温度（与加速度共用 ADC）。
- 读 **OUT_TEMP_L(0x0C)/OUT_TEMP_H(0x0D)** 或驱动 API `lis2dh12_temperature_raw_get()`，换算见 `LIS2DH12_FROM_LSB_TO_degC_HR` 等宏。

```c
lis2dh12_temperature_meas_set(ctx, LIS2DH12_TEMP_ENABLE);
/* 等待 1/ODR 后 */
int16_t temp_raw;
lis2dh12_temperature_raw_get(ctx, &temp_raw);
float degC = LIS2DH12_FROM_LSB_TO_degC_HR(temp_raw);
```

### 6.2 自检完整步骤（开自检→读数据→关自检→校验差值）

- **CTRL_REG4**：ST[1:0] = 01 正自检，10 负自检。
- 流程：**开自检** → 延时 7/ODR → **读多组数据取平均** → **关自检** → 再延时 7/ODR → **再读平均** → 比较两次平均差值是否在数据手册给定范围内。

```c
/* 自检：开自检 → 读平均 → 关自检 → 再读平均 → 校验 */
#define SAMPLES 32
int32_t sum_before[3] = {0}, sum_after[3] = {0};
int16_t raw[3];
uint8_t i;

lis2dh12_self_test_set(ctx, LIS2DH12_ST_DISABLE);
HAL_Delay(100);
for (i = 0; i < SAMPLES; i++) {
    lis2dh12_acceleration_raw_get(ctx, raw);
    sum_before[0] += raw[0]; sum_before[1] += raw[1]; sum_before[2] += raw[2];
    HAL_Delay(5);
}
lis2dh12_self_test_set(ctx, LIS2DH12_ST_POSITIVE);  /* 或 LIS2DH12_ST_NEGATIVE */
HAL_Delay(100);
for (i = 0; i < SAMPLES; i++) {
    lis2dh12_acceleration_raw_get(ctx, raw);
    sum_after[0] += raw[0]; sum_after[1] += raw[1]; sum_after[2] += raw[2];
    HAL_Delay(5);
}
lis2dh12_self_test_set(ctx, LIS2DH12_ST_DISABLE);
HAL_Delay(100);

int16_t delta[3] = {
    (int16_t)((sum_after[0] - sum_before[0]) / SAMPLES),
    (int16_t)((sum_after[1] - sum_before[1]) / SAMPLES),
    (int16_t)((sum_after[2] - sum_before[2]) / SAMPLES)
};
/* 与数据手册 Table "Self-test output change" 比较，如在范围内则 PASS */
```

---

## 7. 活动/不活动自动节能

- **ACT_THS(0x3E)**：活动/不活动阈值（7 位），与当前量程对应 LSB。
- **ACT_DUR(0x3F)**：不活动持续时间（8 位），单位 1/ODR；超过后进入“不活动”状态。
- 中断路由：**CTRL_REG6 I2_ACT=1** 将活动/不活动映射到 **INT2**；CTRL_REG3 无直接 ACT 位，活动/不活动通常用 INT2。
- 睡眠唤醒：当从“不活动”回到“活动”（加速度超过 ACT_THS）时，INT2 可产生唤醒信号，供 MCU 恢复采样或提高 ODR。

```c
/* 活动/不活动：INT2 输出，阈值与持续时间 */
lis2dh12_act_ths_t act_ths = { .acth = 20 };   /* 约 20 LSB，依量程换算 */
lis2dh12_write_reg(ctx, LIS2DH12_ACT_THS, &act_ths.byte, 1);
lis2dh12_write_reg(ctx, LIS2DH12_ACT_DUR, (uint8_t[]){ 0x20 }, 1);  /* 32*1/ODR 不活动后判定 */

lis2dh12_ctrl_reg6_t c6 = { 0 };
c6.i2_act = 1;   /* INT2 = 活动/不活动 */
lis2dh12_pin_int2_config_set(ctx, &c6);
```

---

## 8. 文档章节对照（便于翻 PDF）

| 内容           | dm00365457 (AN5005 中文) | C110926 (Datasheet 英文) |
|----------------|---------------------------|---------------------------|
| 引脚/寄存器表  | §1 引脚说明，§2 寄存器   | §1.2 Pin description, Table 2 |
| 工作模式/ODR   | §3 工作模式，表 3/4      | §3.2.1, Table 10/11 |
| 导通时间       | §4 启动序列              | 时序说明                  |
| REFERENCE 用法 | §3.4 高分辨率模式        | §3.2.1 表注               |
| FIFO           | FIFO 章节/表             | FIFO 寄存器与模式         |
| 中断/Click/6D  | 中断与 Click 寄存器      | INT/Click 描述            |
| 自检           | 自检寄存器               | Self-test 章节            |
| 活动/不活动    | ACT_THS/ACT_DUR          | Activity 相关章节         |

以上配置均可在本工程中通过 `lis2dh12_reg.h` / `lis2dh12_reg.c` 及 `read_data_simple.c` 提供的 API 实现；SPI 使用 `s_dev_ctx.handle = &hspi2` 与平台 `platform_read`/`platform_write`。
