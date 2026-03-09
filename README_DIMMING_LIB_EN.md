# LEDC Dimming Library (ESP-IDF)

A production-ready multi-channel dimming component tailored for ESP-IDF. It delivers a lightweight PWM fading engine, convenient RGB/CCT helpers, and pluggable gamma correction, enabling silky smooth brightness transitions without blocking application logic.

> 简体中文版本见：[README_DIMMING_LIB.md](README_DIMMING_LIB.md)

---

## Table of Contents
1. [Features](#features)
2. [Repository Layout](#repository-layout)
3. [Environment & Dependencies](#environment--dependencies)
4. [Build & Flash](#build--flash)
5. [Quick Start](#quick-start)
6. [API Reference](#api-reference)
7. [Gamma Correction Notes](#gamma-correction-notes)
8. [Platform Timer Abstraction](#platform-timer-abstraction)
9. [Testing & Debugging](#testing--debugging)
10. [Production Tips](#production-tips)
11. [Known Limitations & Future Work](#known-limitations--future-work)

---

## Features
- ✅ **Scalable channel fade engine**: any number of channels, linear interpolation, auto step computation.
- ✅ **RGB / CCT / multi-channel helpers**: `dimming_set_rgb`, `dimming_set_cct`, `dimming_set_multiple_with_fade` ready out of the box.
- ✅ **Per-channel max clamp**: enforce electrical safety limits independently.
- ✅ **Gamma correction**: built-in gamma 1.8/2.2/2.4 and user-defined LUTs.
- ✅ **Portable timer abstraction**: implement `platform_timer.*` once per MCU/RTOS.
- ✅ **Comprehensive demos**: `main/dimming_test_main.c` showcases single-channel, RGB, CCT, and stop-fade scenarios.

---

## Repository Layout

```text
.
├─ components
│  └─ dimming_lib
│     ├─ include
│     │  ├─ dimming_lib.h          # Public API
│     │  └─ platform_timer.h       # Timer abstraction
│     └─ src
│        ├─ dimming_lib_new.c      # Active dimming implementation
│        └─ platform_timer_esp32.c # ESP-IDF timer port
├─ main
│  ├─ dimming_test_main.c          # Demo entry point
│  └─ ledc_basic_example_main.c    # ESP-IDF LEDC reference
├─ README.md
└─ README_DIMMING_LIB_EN.md        # This file
```

> Tip: the two main demos are mutually exclusive. Adjust `main/CMakeLists.txt` to select the desired entry point before building.

---

## Environment & Dependencies
- ESP-IDF v5.x (v4.4 works but v5.x is recommended).
- Python 3.8+ (required by ESP-IDF tooling).
- Any Espressif board supporting LEDC (default demo targets ESP32-C3).
- USB cable for flashing & logging.

---

## Build & Flash

```bash
idf.py set-target esp32c3   # adjust to your MCU
idf.py build
idf.py -p <PORT> flash monitor
```

- Replace `<PORT>` with the serial port (e.g., `COM7`, `/dev/ttyUSB0`).
- Monitor output shows fade logs and resulting PWM duty.
- Exit monitor with `Ctrl+]`.

---

## Quick Start

1. **Configure LEDC**
   - See `ledc_pwm_init()` inside `main/dimming_test_main.c`.
   - Default wiring: RGB LEDs on GPIO5/6/7 (customizable).

2. **Implement driver callback**
   ```c
   static void dimming_driver_callback(uint8_t channel, uint32_t value)
   {
       // value is gamma-processed linear output (0..max_value)
       uint32_t duty = (value * 8191U) / 255U;  // 13-bit LEDC
       ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, ledc_channel_map[channel], duty));
       ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, ledc_channel_map[channel]));
   }
   ```

3. **Initialize the library**
   ```c
   dimming_config_t config = {
       .channel_count = 3,
       .max_value = 255,
       .timer_period_ms = 12,
   };
   dimming_handle_t handle = dimming_init(&config, dimming_driver_callback);
   ```

4. **Schedule fades**
   ```c
   dimming_set_immediate(handle, 0, 128);
   dimming_set_with_fade(handle, 0, 255, 2000);
   dimming_set_rgb(handle, 255, 64, 0, 1500);
   dimming_set_cct(handle, 200, 80, 1000);
   ```

5. **Cleanup**
   ```c
   dimming_deinit(handle);
   ```

---

## API Reference

| Group | Key APIs | Description |
| --- | --- | --- |
| Init / Teardown | `dimming_init`, `dimming_deinit` | Create/destroy context and register driver callback. |
| Single-channel control | `dimming_set_immediate`, `dimming_set_with_fade` | Immediate or time-based fades. |
| Multi-channel control | `dimming_set_multiple_with_fade` | Synchronize fades across channels. |
| RGB / CCT helpers | `dimming_set_rgb`, `dimming_set_cct` | Channels 0/1/2 map to R/G/B, 0/1 map to Warm/Cool. |
| State queries | `dimming_get_current_value`, `dimming_get_target_value`, `dimming_is_fading` | Inspect current/target values or fade state. |
| Runtime control | `dimming_stop_all_fades`, `dimming_set_max_value` | Abort fades or clamp per-channel maximum. |
| Gamma | `dimming_set_gamma_type`, `dimming_set_custom_gamma_table`, `dimming_enable_gamma`, `dimming_apply_gamma`, `dimming_remove_gamma`, `dimming_get_standard_gamma_table` | Toggle standard/custom gamma, convert between linear and gamma space. |

Refer to `components/dimming_lib/include/dimming_lib.h` for exhaustive documentation.

---

## Gamma Correction Notes
- **Default**: each channel starts with `GAMMA_22` enabled.
- **Disable gamma** via `dimming_set_gamma_type(..., GAMMA_NONE)` or `dimming_enable_gamma(..., false)`.
- **Custom LUT** workflow:
  1. Prepare a 256-entry table mapping perceived brightness.
  2. Call `dimming_set_custom_gamma_table(handle, channel, table)`.
  3. The library automatically scales the LUT to the channel’s `max_value` range.
- **Linear readback**: `dimming_get_current_value` returns the pre-gamma linear value. Use `dimming_apply_gamma` to compute the final output level if needed.

---

## Platform Timer Abstraction
- API definitions: `platform_timer.h`.
- ESP32 implementation: `platform_timer_esp32.c` (built on top of `esp_timer`).
- Porting checklist:
  1. Implement `platform_timer_*` using a high-resolution timer or RTOS timer on the target MCU.
  2. Ensure callbacks run in a safe context; hand off heavy work to tasks if needed.
  3. Provide `platform_get_time_ms` and `platform_delay_ms` wrappers for basic timing utilities.

---

## Testing & Debugging
- `main/dimming_test_main.c` demonstrates:
  1. Single-channel immediate set + fades
  2. RGB fade cycle
  3. CCT tuning
  4. Mid-fade stop with state readback
- Serial logs print per-channel values and computed PWM duty cycles.
- Add `ESP_LOG*` inside `dimming_lib_new.c` for deeper inspection if required.

---

## Production Tips
1. **Thread safety**: the library is not internally synchronized. Guard concurrent calls with mutexes/queues.
2. **Memory strategy**: initialization uses `calloc`. Replace with `heap_caps_malloc` or static buffers in systems that forbid dynamic memory.
3. **PWM resolution**: demo uses 13-bit LEDC. Adjust resolution and callback scaling to match your hardware.
4. **Fail-safe**: ensure LEDC (or other driver) configuration errors are handled—avoid leaving LEDs fully on/off unintentionally.
5. **Thermal considerations**: sustained high duty cycles may require extra cooling or current limiting.

---

## Known Limitations & Future Work
1. **Timer period hard-coded**: `platform_timer_esp32.c` currently ignores `config->period_ms` and defaults to 12 ms. Update the port to honor the configuration.
2. **Integer interpolation granularity**: very short fades may jump to the final value on the last tick—this is by design to avoid floating-point overhead.
3. **Concurrency**: no built-in locking. Future iterations could ship with optional mutex integration or message-queue interfaces.
4. **Advanced curves**: only linear fades are available today. S-curves, exponential ramps, or user-provided interpolators can be added later.

Feel free to build upon the existing framework to add animation sequences, scene scripting, or OTA-configurable profiles. For troubleshooting, inspect serial logs, `dimming_get_target_value`, and `dimming_is_fading` to pinpoint runtime state.
