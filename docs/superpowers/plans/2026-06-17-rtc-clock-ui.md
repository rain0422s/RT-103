# RTC 时钟界面实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现真实 RTC 时钟主界面，双击进入/退出二级菜单，并新增 `Time Set` 与 `RTC Calib`。

**Architecture:** 新增一个轻量 RTC 模块封装 STM32F1 备份域、LSE RTC 秒计数和软件秒/天校准。`App/display.c` 增加顶层 UI 模式，默认显示时钟，二级菜单继续复用现有列表渲染。`RT.ioc` 同步启用 RTC，让 CubeMX 打开时能看到 RTC 外设配置。

**Tech Stack:** STM32F103RCT6、CMSIS 寄存器、FreeRTOS、U8G2、PlatformIO、现有 EEPROM storage API。

---

### Task 1: 持久化配置扩展

**Files:**
- Modify: `App/eeprom_layout.h`
- Modify: `App/storage.c`

- [ ] **Step 1: 扩展配置结构**

把 `EEPROM_CONFIG_VERSION` 改为 `2`，把 `eeprom_config_t` 扩展为保存二级菜单选择项、RTC 软件校准值和校准锚点。保持结构体小于 32 字节，避免碰到 `EEPROM_OFFSET_CALIB`。

- [ ] **Step 2: 更新旧配置回退**

在 `storage_load_config()` 里识别旧版/无效 EEPROM 时填入默认值：`ui_select=0`、`rtc_calib_sec_per_day=0`、锚点为 0。

- [ ] **Step 3: 编译验证**

Run: `pio run`

Expected: 编译会继续到后续缺失 RTC UI 符号处失败，或者如果本任务独立完成则通过；不能出现配置结构体大小或 storage 语法错误。

### Task 2: RTC 硬件与时间计算模块

**Files:**
- Create: `App/rtc_clock.h`
- Create: `App/rtc_clock.c`

- [ ] **Step 1: 定义模块接口**

`rtc_clock.h` 暴露：

```c
typedef struct {
        uint8_t hour;
        uint8_t minute;
        uint8_t second;
        bool valid;
} rtc_clock_time_t;

bool rtc_clock_init(void);
bool rtc_clock_is_ready(void);
bool rtc_clock_time_is_set(void);
uint32_t rtc_clock_get_raw_seconds(void);
bool rtc_clock_set_time(uint8_t hour, uint8_t minute, uint8_t second);
rtc_clock_time_t rtc_clock_get_time(const eeprom_config_t *cfg);
int8_t rtc_clock_calib_get(const eeprom_config_t *cfg);
void rtc_clock_calib_set(eeprom_config_t *cfg, int8_t sec_per_day);
```

- [ ] **Step 2: 实现 STM32F1 RTC 秒计数**

`rtc_clock.c` 使用 `RCC->BDCR`、`PWR->CR`、`BKP->DR1`、`RTC->CNTH/CNTL` 等寄存器配置 LSE、RTCSEL=LSE、RTCEN、PRL=32767，并使用备份寄存器标记 RTC 已设置。

- [ ] **Step 3: 实现软件校准**

显示时间按 `raw_seconds + ((raw_seconds - anchor_raw) * sec_per_day) / 86400` 计算，最后取一天内秒数转换为 `HH:MM:SS`。

- [ ] **Step 4: 编译验证**

Run: `pio run`

Expected: RTC 模块自身编译通过；若 UI 尚未接入，不能出现 RTC 模块相关错误。

### Task 3: CubeMX `.ioc` 与初始化路径

**Files:**
- Modify: `RT.ioc`
- Modify: `Core/Src/main.c`
- Modify: `Core/Inc/stm32f1xx_hal_conf.h`

- [ ] **Step 1: 同步 `RT.ioc`**

把 `RTC` 加入 `Mcu.IP` 列表，把 `Mcu.IPNb` 增加到 14，添加 RTC 虚拟项，并把 `MX_RTC_Init` 加入 `ProjectManager.functionlistsort`。

- [ ] **Step 2: 添加初始化调用**

在 `main.c` include `rtc_clock.h`，在外设初始化后、启动 RTOS 前调用 `rtc_clock_init()`。

- [ ] **Step 3: HAL 配置**

保持直接寄存器实现，不依赖缺失的 `stm32f1xx_hal_rtc.c/.h`。不启用 `HAL_RTC_MODULE_ENABLED`，避免 include 缺失驱动头导致编译失败。

- [ ] **Step 4: 编译验证**

Run: `pio run`

Expected: 不出现 RTC HAL 头文件缺失错误；`.ioc` 配置文本包含 RTC 外设。

### Task 4: 时钟主界面与二级菜单模式

**Files:**
- Modify: `App/display.c`
- Modify: `App/display.h`

- [ ] **Step 1: 增加 UI 模式**

在 `display.c` 增加时钟主界面模式和二级菜单模式。默认模式为时钟主界面。

- [ ] **Step 2: 时钟渲染**

时钟主界面读取 `rtc_clock_get_time()`，有效时显示 `HH:MM:SS`，无效时显示 `--:--:--`。

- [ ] **Step 3: 双击进入/退出**

`key_scan()` 保留双击事件，时钟模式下双击切到二级菜单；菜单模式下双击返回时钟。

- [ ] **Step 4: 动画优化**

复用现有 `ui_run()` 插值，在模式切换时加入轻量 x 偏移过渡；动画活跃时保持 10ms 刷新，静止时 100ms。

- [ ] **Step 5: 编译验证**

Run: `pio run`

Expected: UI 编译通过，现有菜单项仍可注册。

### Task 5: Time Set 与 RTC Calib 菜单动作

**Files:**
- Modify: `App/ui_menu_registry.c`
- Modify: `App/display.c`
- Modify: `App/display.h`

- [ ] **Step 1: 注册菜单项**

在 `ui_menu_registry_register_all()` 中追加 `Time Set` 与 `RTC Calib`。

- [ ] **Step 2: 实现 `Time Set` 编辑器**

编辑小时、分钟、秒。单击递增当前字段，长按切换字段，双击保存到 RTC 并返回菜单。

- [ ] **Step 3: 实现 `RTC Calib` 编辑器**

显示并编辑 `s/day`。单击递增绝对值，长按切换正负方向，双击保存到 EEPROM 并返回菜单。

- [ ] **Step 4: 编译验证**

Run: `pio run`

Expected: `Time Set`、`RTC Calib` 动作可链接，固件编译成功。

### Task 6: 最终验证

**Files:**
- Inspect: `RT.ioc`
- Inspect: `App/display.c`
- Inspect: `App/rtc_clock.c`
- Inspect: `App/storage.c`

- [ ] **Step 1: 构建**

Run: `pio run`

Expected: exit code 0，生成 `.pio/build/genericSTM32F103RC/Ems_0.0.1.elf` 等输出。

- [ ] **Step 2: 配置检查**

Run: `rg -n "Mcu.IP.*RTC|RTC\\.IPParameters|RCC\\.RTCClockSelection|MX_RTC_Init" RT.ioc`

Expected: 命中 RTC IP、LSE RTC clock 和 `MX_RTC_Init`。

- [ ] **Step 3: 工作区检查**

Run: `git status --short`

Expected: 只包含本功能文件、已保留的 spec 和用户已有未提交文件。
