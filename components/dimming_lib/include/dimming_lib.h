/**
 * @file dimming_lib.h
 * @brief 通用嵌入式调光库头文件
 * 
 * 该库提供多通道调光控制，支持平滑渐变功能。
 * 基于12ms定时器实现，每个通道独立控制。
 */

#ifndef DIMMING_LIB_H
#define DIMMING_LIB_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ... existing code ...

/**
 * @brief Gamma校正表类型
 */
typedef enum {
    GAMMA_NONE = 0,      ///< 无gamma校正
    GAMMA_22,           ///< Gamma 2.2校正（标准）
    GAMMA_18,           ///< Gamma 1.8校正（适用于某些LED）
    GAMMA_24,           ///< Gamma 2.4校正（高对比度）
    GAMMA_CUSTOM        ///< 自定义gamma表
} gamma_type_t;
/**
 * @brief 调光通道结构体
 */
typedef struct {
    uint32_t current_value;   ///< 当前输出值
    uint32_t target_value;    ///< 目标值
    int32_t step_size;        ///< 步长（可为正负）
    uint32_t step_count;      ///< 剩余步数
    uint32_t max_value;       ///< 最大值限制
    bool is_active;           ///< 通道是否激活
} dimming_channel_t;

/**
 * @brief Gamma校正配置（按通道）
 */
typedef struct {
    bool enabled;        ///< 是否启用gamma
    gamma_type_t type;   ///< gamma类型
} dimming_gamma_config_t;

/**
 * @brief 渐变配置（按通道）
 */
typedef struct {
    bool enabled;                ///< 是否启用渐变（false则强制立即生效）
    uint32_t default_duration_ms;///< 默认渐变时间（用于未指定时）
} dimming_fade_config_t;

/**
 * @brief 调光库配置结构体
 */
typedef struct {
    uint8_t channel_count;            ///< 通道数量
    uint32_t max_value;               ///< 默认最大值
    uint32_t timer_period_ms;         ///< 定时器周期（默认12ms）
    const dimming_gamma_config_t *gamma_configs; ///< 每通道gamma配置（可为NULL，表示默认启用GAMMA_22）
    const dimming_fade_config_t *fade_configs;   ///< 每通道渐变配置（可为NULL，表示默认启用渐变，默认时长=0）
} dimming_config_t;

/**
 * @brief 调光库句柄
 */
typedef void* dimming_handle_t;

/**
 * @brief 驱动更新回调函数类型
 * @param channel 通道索引
 * @param value 新的输出值
 */
typedef void (*dimming_driver_cb_t)(uint8_t channel, uint32_t value);

/**
 * @brief 初始化调光库
 * 
 * @param config 配置参数
 * @param driver_cb 驱动更新回调函数
 * @return dimming_handle_t 调光库句柄，失败返回NULL
 */
dimming_handle_t dimming_init(const dimming_config_t* config, dimming_driver_cb_t driver_cb);

/**
 * @brief 销毁调光库
 * 
 * @param handle 调光库句柄
 */
void dimming_deinit(dimming_handle_t handle);

/**
 * @brief 设置通道颜色/亮度（立即生效，无渐变）
 * 
 * @param handle 调光库句柄
 * @param channel 通道索引
 * @param value 目标值
 * @return true 设置成功
 * @return false 设置失败
 */
bool dimming_set_immediate(dimming_handle_t handle, uint8_t channel, uint32_t value);

/**
 * @brief 设置通道颜色/亮度（带渐变）
 * 
 * @param handle 调光库句柄
 * @param channel 通道索引
 * @param value 目标值
 * @param duration_ms 渐变持续时间（毫秒）
 * @return true 设置成功
 * @return false 设置失败
 */
bool dimming_set_with_fade(dimming_handle_t handle, uint8_t channel, uint32_t value, uint32_t duration_ms);

/**
 * @brief 设置多个通道颜色/亮度（带渐变）
 * 
 * @param handle 调光库句柄
 * @param channels 通道索引数组
 * @param values 目标值数组
 * @param count 通道数量
 * @param duration_ms 渐变持续时间（毫秒）
 * @return true 设置成功
 * @return false 设置失败
 */
bool dimming_set_multiple_with_fade(dimming_handle_t handle, const uint8_t* channels, 
                                   const uint32_t* values, uint8_t count, uint32_t duration_ms);

/**
 * @brief 停止所有渐变
 * 
 * @param handle 调光库句柄
 */
void dimming_stop_all_fades(dimming_handle_t handle);

/**
 * @brief 获取通道当前值
 * 
 * @param handle 调光库句柄
 * @param channel 通道索引
 * @return uint32_t 当前值
 */
uint32_t dimming_get_current_value(dimming_handle_t handle, uint8_t channel);

/**
 * @brief 获取通道目标值
 * 
 * @param handle 调光库句柄
 * @param channel 通道索引
 * @return uint32_t 目标值
 */
uint32_t dimming_get_target_value(dimming_handle_t handle, uint8_t channel);

/**
 * @brief 检查是否有渐变正在进行
 * 
 * @param handle 调光库句柄
 * @return true 有渐变在进行
 * @return false 无渐变在进行
 */
bool dimming_is_fading(dimming_handle_t handle);

/**
 * @brief 设置通道最大值限制
 * 
 * @param handle 调光库句柄
 * @param channel 通道索引
 * @param max_value 最大值
 * @return true 设置成功
 * @return false 设置失败
 */
bool dimming_set_max_value(dimming_handle_t handle, uint8_t channel, uint32_t max_value);

/**
 * @brief RGB颜色设置（带渐变）
 * 
 * @param handle 调光库句柄
 * @param red 红色值 (0-255)
 * @param green 绿色值 (0-255)
 * @param blue 蓝色值 (0-255)
 * @param duration_ms 渐变持续时间（毫秒）
 * @return true 设置成功
 * @return false 设置失败
 */
bool dimming_set_rgb(dimming_handle_t handle, uint8_t red, uint8_t green, uint8_t blue, uint32_t duration_ms);

/**
 * @brief CCT颜色设置（带渐变）
 * 
 * @param handle 调光库句柄
 * @param warm 暖白值
 * @param cool 冷白值
 * @param duration_ms 渐变持续时间（毫秒）
 * @return true 设置成功
 * @return false 设置失败
 */
bool dimming_set_cct(dimming_handle_t handle, uint32_t warm, uint32_t cool, uint32_t duration_ms);


/**
 * @brief 设置通道gamma校正类型
 * 
 * @param handle 调光库句柄
 * @param channel 通道索引
 * @param gamma_type gamma校正类型
 * @return true 设置成功
 * @return false 设置失败
 */
bool dimming_set_gamma_type(dimming_handle_t handle, uint8_t channel, gamma_type_t gamma_type);

/**
 * @brief 设置自定义gamma校正表
 * 
 * @param handle 调光库句柄
 * @param channel 通道索引
 * @param gamma_table gamma校正表（256个元素）
 * @return true 设置成功
 * @return false 设置失败
 */
bool dimming_set_custom_gamma_table(dimming_handle_t handle, uint8_t channel, const uint8_t* gamma_table);

/**
 * @brief 应用gamma校正
 * 
 * @param handle 调光库句柄
 * @param channel 通道索引
 * @param value 原始值
 * @return uint32_t gamma校正后的值
 */
uint32_t dimming_apply_gamma(dimming_handle_t handle, uint8_t channel, uint32_t value);

/**
 * @brief 移除gamma校正
 * 
 * @param handle 调光库句柄
 * @param channel 通道索引
 * @param value gamma校正后的值
 * @return uint32_t 原始值
 */
uint32_t dimming_remove_gamma(dimming_handle_t handle, uint8_t channel, uint32_t value);

/**
 * @brief 启用/禁用gamma校正
 * 
 * @param handle 调光库句柄
 * @param channel 通道索引
 * @param enable true启用，false禁用
 * @return true 设置成功
 * @return false 设置失败
 */
bool dimming_enable_gamma(dimming_handle_t handle, uint8_t channel, bool enable);

/**
 * @brief 获取标准gamma校正表
 * 
 * @param gamma gamma值（如2.2）
 * @param table 输出gamma表（256个元素）
 */
void dimming_get_standard_gamma_table(float gamma, uint8_t* table);

#ifdef __cplusplus
}
#endif

#endif /* DIMMING_LIB_H */