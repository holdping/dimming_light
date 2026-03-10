# LEDC Dimming Library（ESP-IDF）

一个专注于 ESP-IDF 平台的多通道调光组件，提供生产级的 PWM 渐变引擎、RGB/CCT 快捷接口以及可插拔的 Gamma 校正机制。该库对 MCU 资源占用极低，能够在不阻塞主业务逻辑的情况下实现平滑的 LED 亮度控制。

> English version: [README_DIMMING_LIB_EN.md](README_DIMMING_LIB_EN.md)

---

## 目录
1. [特性概览](#特性概览)
2. [仓库结构](#仓库结构)
3. [环境与依赖](#环境与依赖)
4. [构建与烧录](#构建与烧录)
5. [快速上手](#快速上手)
6. [API 速查](#api-速查)
7. [Gamma 校正说明](#gamma-校正说明)
8. [平台定时器抽象](#平台定时器抽象)
9. [测试与调试](#测试与调试)
10. [生产使用建议](#生产使用建议)
11. [已知限制与改进方向](#已知限制与改进方向)

---

## 特性概览
- ✅ **多通道渐变引擎**：支持任意通道数量，线性插值，自动计算步长与周期。
- ✅ **RGB / CCT / 多通道便捷接口**：内置 `dimming_set_rgb`、`dimming_set_cct`、`dimming_set_multiple_with_fade` 等 API。
- ✅ **每通道最大值限制**：可针对不同 LED 电气特性设置安全上限。
- ✅ **Gamma 校正**：内置多种常用 Gamma（1.8/2.2/2.4），亦支持自定义查表。
- ✅ **平台计时器抽象**：轻松移植到不同 RTOS/MCU，仅需实现 `platform_timer.h` 中接口。
- ✅ **示例齐备**：`main/dimming_test_main.c` 提供单通道、RGB、CCT 及中途停止渐变的完整演示。

---

## 示意图与流程

![示例 PWM 波形](image/ledc_pwm_signal.png)

> 上图展示了 LEDC 以 4 kHz 频率输出 50% 占空比 PWM 时的波形，可用于验证硬件连线与 LEDC 配置。

```mermaid
graph LR
    A[App Logic / User Task] --> B[dimming_set_* APIs]
    B --> C[Dimming Core]
    C --> D[Fade Scheduler<br/>Platform Timer]
    D --> E[Gamma Engine]
    E --> F[Driver Callback]
    F --> G[LEDC / Hardware PWM]
    G --> H[LED Strip / Fixture]
```

---

## 仓库结构

```text
.
├─ components
│  └─ dimming_lib
│     ├─ include
│     │  ├─ dimming_lib.h          # 调光库 API
│     │  └─ platform_timer.h       # 平台定时器抽象
│     └─ src
│        ├─ dimming_lib_new.c      # 当前启用的调光实现
│        └─ platform_timer_esp32.c # ESP-IDF 平台定时器
├─ main
│  ├─ dimming_test_main.c          # 调光库演示入口
│  └─ ledc_basic_example_main.c    # 官方 LEDC 基础示例
├─ README.md
└─ README_DIMMING_LIB.md           # 本文件
```

> **提示**：`main` 目录下的基础 LEDC 示例与调光示例互斥，构建前请在 `CMakeLists.txt` 中确认入口文件。

---

## 环境与依赖
- ESP-IDF v5.x（最低 v4.4 亦可，但推荐使用 v5.x 以获得更佳工具链支持）。
- Python 3.8+（ESP-IDF 依赖）。
- 一块支持 LEDC 的 Espressif SoC 开发板（示例默认 ESP32-C3）。
- 可用的 USB 串口连接线。

---

## 构建与烧录

```bash
idf.py set-target esp32c3   # 根据硬件选择目标
idf.py build
idf.py -p <PORT> flash monitor
```

- `<PORT>` 为串口号（如 `COM7` 或 `/dev/ttyUSB0`）。
- 在 `monitor` 中可观察调光过程日志与 PWM duty 值。
- 退出串口监视器：`Ctrl+]`。

---

## 快速上手

1. **配置 LEDC**：
   - 参考 `main/dimming_test_main.c` 内的 `ledc_pwm_init()`。
   - 将三个 LED 分别接入 GPIO5/6/7（可自行修改）。

2. **实现驱动回调**：
   ```c
   static void dimming_driver_callback(uint8_t channel, uint32_t value)
   {
       // value 为经过 Gamma 处理后的线性输出值（范围 0~max_value）
       uint32_t duty = (value * 8191U) / 255U; // 13bit -> 8191
       ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, ledc_channel_map[channel], duty));
       ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, ledc_channel_map[channel]));
   }
   ```

3. **初始化调光库**：
   ```c
   dimming_config_t config = {
       .channel_count = 3,
       .max_value = 255,
       .timer_period_ms = 12,
   };
   dimming_handle_t handle = dimming_init(&config, dimming_driver_callback);
   ```

4. **发起调光任务**：
   ```c
   dimming_set_immediate(handle, 0, 128);             // 立即设置通道0
   dimming_set_with_fade(handle, 0, 255, 2000);       // 2 秒渐变
   dimming_set_rgb(handle, 255, 64, 0, 1500);         // RGB 渐变
   dimming_set_cct(handle, 200, 80, 1000);            // CCT 渐变
   ```

5. **清理资源**：
   ```c
   dimming_deinit(handle);
   ```

---

## API 速查

| 分组 | 关键 API | 说明 |
| --- | --- | --- |
| 初始化/销毁 | `dimming_init`, `dimming_deinit` | 创建/销毁上下文，注册驱动回调。 |
| 单通道调光 | `dimming_set_immediate`, `dimming_set_with_fade` | 立即生效或按时长渐变。 |
| 多通道调光 | `dimming_set_multiple_with_fade` | 同步渐变多个通道。 |
| RGB/CCT 便捷接口 | `dimming_set_rgb`, `dimming_set_cct` | 固定映射通道顺序（0/1/2 对应 R/G/B，0/1 对应 Warm/Cool）。 |
| 状态查询 | `dimming_get_current_value`, `dimming_get_target_value`, `dimming_is_fading` | 获取当前值/目标值/渐变状态。 |
| 运行控制 | `dimming_stop_all_fades`, `dimming_set_max_value` | 强制停止或设置通道最大值。 |
| Gamma 控制 | `dimming_set_gamma_type`, `dimming_set_custom_gamma_table`, `dimming_enable_gamma`, `dimming_apply_gamma`, `dimming_remove_gamma`, `dimming_get_standard_gamma_table` | 开启/关闭标准或自定义 Gamma，并支持线性值与 Gamma 值互换。 |

详见 `components/dimming_lib/include/dimming_lib.h` 中的注释。

---

## Gamma 校正说明
- **默认状态**：每个通道启用 `GAMMA_22`，适合大多数视觉线性需求。
- **禁用 Gamma**：调用 `dimming_set_gamma_type(..., GAMMA_NONE)` 或 `dimming_enable_gamma(..., false)`。
- **自定义 LUT**：
  1. 准备 256 长度的查找表，输入值代表 0~255 的亮度感知映射。
  2. 调用 `dimming_set_custom_gamma_table(handle, channel, table)`。
  3. 库内会自动将 LUT 结果映射到当前通道 `max_value` 量程。
- **线性值回读**：
  - `dimming_get_current_value` 返回 Gamma 前的线性值。
  - 若需要获取 “输出值”，可使用 `dimming_apply_gamma`。

---

## 平台定时器抽象
- 接口定义见 `platform_timer.h`。
- ESP32 实现基于 `esp_timer`（文件：`platform_timer_esp32.c`）。
- 移植步骤：
  1. 在新平台实现 `platform_timer_*` API，内部使用该平台的高精定时器或 RTOS 定时器。
  2. 确保回调在中断安全或任务上下文运行，并在必要时切换到任务上下文执行较重逻辑。
  3. `platform_get_time_ms`、`platform_delay_ms` 用于库内基础时间操作，可映射到 HAL 接口。

---

## 测试与调试
- `main/dimming_test_main.c` 包含以下示例：
  1. 单通道立即设置 + 渐变
  2. RGB 循环渐变
  3. CCT 调光
  4. 中途停止渐变并读取当前值
- 串口日志会打印每次回调的 `value` 与计算出的 PWM `duty`，方便排查。
- 如需更详细日志，可在 `dimming_lib_new.c` 中添加 `ESP_LOG*` 调用，或在应用层统计 `dimming_get_current_value`。

---

## 生产使用建议
1. **线程安全**：库内未加锁，如存在多任务并发调用，需在外层加互斥锁或消息队列保护。
2. **内存分配**：初始化阶段使用 `calloc`，可在静态内存环境中改写为 `heap_caps_malloc` 或静态数组，以满足无动态内存需求。
3. **PWM 分辨率**：示例使用 13bit LEDC，可根据实际硬件提升或降低分辨率，但需同步调整回调中的换算。
4. **Fail-Safe**：若驱动层（如 LEDC）配置失败，应在回调中返回错误并做降级处理，避免 LED 长时间全亮或全灭。
5. **功耗/发热**：长期高占空比运行时请考虑额外散热及过流保护。

---

## 已知限制与改进方向
1. **定时器周期硬编码**：`platform_timer_esp32.c` 当前忽略 `config->period_ms`，固定为 12ms。建议根据配置传参，或在初始化处断言提醒。
2. **步长粒度**：线性插值采用整数除法，在极短渐变时可能在最后一次跳变至目标值，这是为避免浮点引入的取舍。
3. **多上下文调用**：尚未内置线程安全机制，未来可选配互斥锁或消息队列以支持多任务安全访问。
4. **高级曲线**：当前仅支持线性渐变，后续可扩展 S 曲线、指数或用户自定义插值器。

欢迎在此基础上扩展更多高级调光策略，如动画序列、场景脚本或远程 OTA 配置下发等。若在使用过程中遇到问题，可通过串口日志、`dimming_get_target_value` 与 `dimming_is_fading` 快速定位。

---

## Light Bulb 高级玩法（新增）

`components/dimming_lib/include/light_bulb.h` 提供了更高层的灯具能力，适用于跨平台移植（通过 `driver.init / driver.deinit / driver.set_channel` 适配底层硬件）。

### 主要新增能力

- 白光调节：`light_bulb_transition_cct`、`light_bulb_transition_cw`、`light_bulb_transition_ww`
- 预设场景：`light_bulb_apply_relax_scene`、`light_bulb_apply_reading_scene`、`light_bulb_apply_night_scene`、`light_bulb_sunrise_simulation` 等
- 动效玩法：`light_bulb_run_rainbow_effect`、`light_bulb_run_breath_effect`、`light_bulb_run_strobe_effect`、`light_bulb_run_fire_effect`、`light_bulb_run_scanner_effect`
- 颜色工具：`light_bulb_color_mix`、`light_bulb_color_brightness`、`light_bulb_color_temperature_to_rgb`、`light_bulb_color_temperature_to_cct`
- 运行控制：`light_bulb_start_effect`、`light_bulb_stop_effect`、`light_bulb_is_effect_running`

### 示例

```c
light_bulb_transition_cct_percent(bulb, 25, 255);   // 偏暖白
light_bulb_transition_cw(bulb, 255);                // 仅冷白
light_bulb_run_breath_effect(bulb, LIGHT_BULB_COLOR_CYAN, 1800, 10);
light_bulb_run_rainbow_effect(bulb, 2400, 5);
```
