# LEDC Dimming Library（ESP-IDF）

基于 ESP-IDF 的多通道调光库，支持：
- 多通道线性渐变（fade）
- RGB / CCT 快捷接口
- 每通道最大值限制
- 每通道 Gamma 校正（标准 Gamma 与自定义查表）

当前工程默认用于 ESP32-C3，示例入口为 `main/dimming_test_main.c`。

---

## 1. 工程结构

```text
.
├─ CMakeLists.txt
├─ README.md
├─ README_DIMMING_LIB.md
└─ main
   ├─ CMakeLists.txt
   ├─ dimming_lib.h
   ├─ dimming_lib_new.c          # 当前启用的调光库实现
   ├─ dimming_lib.c              # 旧实现（当前不参与编译）
   ├─ platform_timer.h
   ├─ platform_timer_esp32.c
   ├─ dimming_test_main.c        # 测试/演示入口
   └─ ledc_basic_example_main.c  # LEDC基础示例
```

当前组件编译源（`main/CMakeLists.txt`）为：
- `dimming_test_main.c`
- `dimming_lib_new.c`
- `platform_timer_esp32.c`

---

## 2. 依赖与构建

### 2.1 环境依赖

- ESP-IDF（建议 v5.x）
- 可用的 `idf.py`
- 目标开发板（如 ESP32-C3）

### 2.2 构建与烧录

```bash
idf.py set-target esp32c3
idf.py build
idf.py -p <PORT> flash monitor
```

---

## 3. 设计概览

### 3.1 数据模型

每个通道维护：
- `current_value`：当前线性值
- `target_value`：目标线性值
- `step_size`：每次定时更新的增量
- `step_count`：剩余步数
- `max_value`：该通道上限

另有每通道 Gamma 配置：
- `gamma_type`：`GAMMA_NONE / 18 / 22 / 24 / CUSTOM`
- `gamma_enabled`：是否启用
- `custom_gamma_table[256]`：自定义 LUT

### 3.2 运行流程

1. 用户通过 API 设置单通道或多通道目标值。  
2. 若是渐变，计算 `step_count` 与 `step_size`。  
3. 定时器周期触发，更新 `current_value`。  
4. 输出前调用 Gamma 映射，最终通过 `driver_cb(channel, value)` 下发到驱动层。  

注意：库内部存储的 `current_value/target_value` 都是**线性值**，Gamma 仅作用于最终输出值。

---

## 4. API 说明

头文件：`main/dimming_lib.h`

### 4.1 初始化与销毁

```c
dimming_handle_t dimming_init(const dimming_config_t* config, dimming_driver_cb_t driver_cb);
void dimming_deinit(dimming_handle_t handle);
```

### 4.2 基本调光

```c
bool dimming_set_immediate(dimming_handle_t handle, uint8_t channel, uint32_t value);
bool dimming_set_with_fade(dimming_handle_t handle, uint8_t channel, uint32_t value, uint32_t duration_ms);
bool dimming_set_multiple_with_fade(dimming_handle_t handle, const uint8_t* channels,
                                    const uint32_t* values, uint8_t count, uint32_t duration_ms);
```

### 4.3 状态与限制

```c
void dimming_stop_all_fades(dimming_handle_t handle);
uint32_t dimming_get_current_value(dimming_handle_t handle, uint8_t channel);
uint32_t dimming_get_target_value(dimming_handle_t handle, uint8_t channel);
bool dimming_is_fading(dimming_handle_t handle);
bool dimming_set_max_value(dimming_handle_t handle, uint8_t channel, uint32_t max_value);
```

### 4.4 便捷模式

```c
bool dimming_set_rgb(dimming_handle_t handle, uint8_t red, uint8_t green, uint8_t blue, uint32_t duration_ms);
bool dimming_set_cct(dimming_handle_t handle, uint32_t warm, uint32_t cool, uint32_t duration_ms);
```

约定：
- `dimming_set_rgb` 使用通道 `0/1/2` 分别对应 `R/G/B`
- `dimming_set_cct` 使用通道 `0/1` 分别对应 `warm/cool`

### 4.5 Gamma 校正

```c
bool dimming_set_gamma_type(dimming_handle_t handle, uint8_t channel, gamma_type_t gamma_type);
bool dimming_set_custom_gamma_table(dimming_handle_t handle, uint8_t channel, const uint8_t* gamma_table);
uint32_t dimming_apply_gamma(dimming_handle_t handle, uint8_t channel, uint32_t value);
uint32_t dimming_remove_gamma(dimming_handle_t handle, uint8_t channel, uint32_t value);
bool dimming_enable_gamma(dimming_handle_t handle, uint8_t channel, bool enable);
void dimming_get_standard_gamma_table(float gamma, uint8_t* table);
```

行为说明：
- `dimming_set_gamma_type(channel, GAMMA_22)` 会同时启用该通道 Gamma。
- `dimming_set_custom_gamma_table(...)` 会切换为 `GAMMA_CUSTOM` 并启用。
- `dimming_enable_gamma(channel, true)` 且当前类型为 `GAMMA_NONE` 时，会自动切到 `GAMMA_22`。

---

## 5. 快速接入示例

### 5.1 初始化

```c
dimming_config_t config = {
    .channel_count = 3,
    .max_value = 255,
    .timer_period_ms = 12
};

dimming_handle_t handle = dimming_init(&config, driver_callback);
```

### 5.2 驱动回调（线性值或 Gamma 后值）

库传给回调的是“最终输出值”（已按该通道 Gamma 处理），你只需要做硬件映射：

```c
static void driver_callback(uint8_t channel, uint32_t value)
{
    uint32_t duty = (value * 8191U) / 255U;
    // ledc_set_duty(...) + ledc_update_duty(...)
}
```

### 5.3 设置颜色与渐变

```c
dimming_set_immediate(handle, 0, 128);
dimming_set_with_fade(handle, 0, 255, 1200);
dimming_set_rgb(handle, 255, 32, 8, 1500);
```

### 5.4 启用标准 Gamma

```c
dimming_set_gamma_type(handle, 0, GAMMA_22);   // 通道0启用Gamma2.2
dimming_set_gamma_type(handle, 1, GAMMA_24);   // 通道1启用Gamma2.4
```

### 5.5 自定义 Gamma LUT

```c
uint8_t table[256];
dimming_get_standard_gamma_table(2.0f, table); // 先生成一个示例表
dimming_set_custom_gamma_table(handle, 2, table);
```

---

## 6. 平台定时器抽象

接口定义在 `main/platform_timer.h`：
- `platform_timer_create`
- `platform_timer_start`
- `platform_timer_stop`
- `platform_timer_is_running`
- `platform_timer_delete`

ESP32 适配在 `main/platform_timer_esp32.c`，基于 `esp_timer`。

移植到其它平台时，只需实现 `platform_timer.h` 中的接口。

---

## 7. 已知限制（当前代码状态）

1. `platform_timer_esp32.c` 当前将周期硬编码为 `12000us`，即固定 12ms。  
   这意味着即使 `dimming_config_t.timer_period_ms` 设为其他值，实际定时触发周期仍是 12ms。

2. 渐变步长是整数除法计算，某些小步长场景下会在最后一步收敛到目标值。  
   这是预期行为，不影响最终目标值正确性。

3. 库目前未加锁，默认按“单任务上下文调用”设计。  
   多任务并发调用时，建议外部加互斥保护。

---

## 8. 测试入口

`main/dimming_test_main.c` 已包含以下演示：
- 单通道立即设置与渐变
- RGB 渐变
- CCT 渐变
- 中途停止渐变

串口中可观察各通道输出值和 PWM duty 变化。

---

## 9. 后续建议

- 修复 `platform_timer_esp32.c`：使用 `config->period_ms` 替代硬编码 12ms。
- 根据业务增加模式接口：如 HSV、场景序列、曲线渐变。
- 如需更平滑视觉效果，可在应用层搭配更高 PWM 分辨率与更细粒度值域。
