/**
 * @file platform_timer.h
 * @brief 平台定时器抽象层
 * 
 * 提供统一的定时器接口，支持不同嵌入式平台
 */

#ifndef PLATFORM_TIMER_H
#define PLATFORM_TIMER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 定时器句柄
 */
typedef void* platform_timer_handle_t;

/**
 * @brief 定时器回调函数类型
 * @param arg 用户参数
 */
typedef void (*platform_timer_callback_t)(void* arg);

/**
 * @brief 定时器配置
 */
typedef struct {
    uint32_t period_ms;               ///< 定时器周期（毫秒）
    bool is_periodic;                 ///< 是否为周期性定时器
    platform_timer_callback_t callback; ///< 回调函数
    void* callback_arg;               ///< 回调函数参数
    const char* name;                 ///< 定时器名称（可选）
} platform_timer_config_t;

/**
 * @brief 创建定时器
 * 
 * @param config 定时器配置
 * @param handle 输出定时器句柄
 * @return true 创建成功
 * @return false 创建失败
 */
bool platform_timer_create(const platform_timer_config_t* config, platform_timer_handle_t* handle);

/**
 * @brief 删除定时器
 * 
 * @param handle 定时器句柄
 */
void platform_timer_delete(platform_timer_handle_t handle);

/**
 * @brief 启动定时器
 * 
 * @param handle 定时器句柄
 * @return true 启动成功
 * @return false 启动失败
 */
bool platform_timer_start(platform_timer_handle_t handle);

/**
 * @brief 停止定时器
 * 
 * @param handle 定时器句柄
 * @return true 停止成功
 * @return false 停止失败
 */
bool platform_timer_stop(platform_timer_handle_t handle);

/**
 * @brief 检查定时器是否正在运行
 * 
 * @param handle 定时器句柄
 * @return true 正在运行
 * @return false 未运行
 */
bool platform_timer_is_running(platform_timer_handle_t handle);

/**
 * @brief 获取系统时间（毫秒）
 * 
 * @return uint32_t 系统时间（毫秒）
 */
uint32_t platform_get_time_ms(void);

/**
 * @brief 延时（毫秒）
 * 
 * @param ms 延时时间（毫秒）
 */
void platform_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_TIMER_H */