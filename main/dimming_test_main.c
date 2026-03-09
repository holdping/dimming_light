/**
 * @file dimming_test_main.c
 * @brief 调光库测试主程序
 * 
 * 演示如何使用通用调光库控制RGB LED
 */

#include <stdio.h>
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "dimming_lib.h"
#include "platform_timer.h"

// LEDC配置
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT
#define LEDC_FREQUENCY          (4000)

// GPIO引脚定义
#define LED_RED_GPIO            (5)
#define LED_GREEN_GPIO          (6)
#define LED_BLUE_GPIO           (7)

// 通道定义
#define CHANNEL_RED             0
#define CHANNEL_GREEN           1
#define CHANNEL_BLUE            2
#define CHANNEL_COUNT           3

// 调光库句柄
static dimming_handle_t g_dimming_handle = NULL;

/**
 * @brief LEDC PWM初始化
 */
static void ledc_pwm_init(void)
{
    // 配置LEDC定时器
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    
    // 配置红色通道
    ledc_channel_config_t red_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LED_RED_GPIO,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&red_channel));
    
    // 配置绿色通道
    ledc_channel_config_t green_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_1,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LED_GREEN_GPIO,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&green_channel));
    
    // 配置蓝色通道
    ledc_channel_config_t blue_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL_2,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LED_BLUE_GPIO,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&blue_channel));
}

/**
 * @brief 调光库驱动更新回调
 * 
 * 将调光库的输出值转换为PWM占空比
 */
static void dimming_driver_callback(uint8_t channel, uint32_t value)
{
    // 将0-255的亮度值转换为PWM占空比
    // 假设最大PWM值为8191（13位分辨率）
    uint32_t duty = (value * 8191) / 255;
    
    ledc_channel_t ledc_channel;
    switch (channel) {
        case CHANNEL_RED:
            ledc_channel = LEDC_CHANNEL_0;
            break;
        case CHANNEL_GREEN:
            ledc_channel = LEDC_CHANNEL_1;
            break;
        case CHANNEL_BLUE:
            ledc_channel = LEDC_CHANNEL_2;
            break;
        default:
            return;
    }
    
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, ledc_channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, ledc_channel));
    
    printf("Channel %d: value=%lu, duty=%lu\n", channel, value, duty);
}

/**
 * @brief 初始化调光系统
 */
static void dimming_system_init(void)
{
    // 初始化PWM
    ledc_pwm_init();
    
    // 配置调光库
    dimming_config_t config = {
        .channel_count = CHANNEL_COUNT,
        .max_value = 255,          // 8位亮度值
        .timer_period_ms = 12      // 12ms定时器周期
    };
    
    // 初始化调光库
    g_dimming_handle = dimming_init(&config, dimming_driver_callback);
    if (!g_dimming_handle) {
        printf("Failed to initialize dimming library\n");
        return;
    }
    
    printf("Dimming system initialized successfully\n");
}

/**
 * @brief 演示RGB渐变效果
 */
static void demo_rgb_fade(void)
{
    if (!g_dimming_handle) {
        return;
    }
    
    printf("\n=== RGB Fade Demo ===\n");
    
    // 1. 渐变到红色（2秒）
    printf("1. Fade to red (2 seconds)\n");
    dimming_set_rgb(g_dimming_handle, 255, 0, 0, 2000);
    while (dimming_is_fading(g_dimming_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 2. 渐变到绿色（1.5秒）
    printf("2. Fade to green (1.5 seconds)\n");
    dimming_set_rgb(g_dimming_handle, 0, 255, 0, 1500);
    while (dimming_is_fading(g_dimming_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 3. 渐变到蓝色（1秒）
    printf("3. Fade to blue (1 second)\n");
    dimming_set_rgb(g_dimming_handle, 0, 0, 255, 1000);
    while (dimming_is_fading(g_dimming_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 4. 渐变到白色（2秒）
    printf("4. Fade to white (2 seconds)\n");
    dimming_set_rgb(g_dimming_handle, 255, 255, 255, 2000);
    while (dimming_is_fading(g_dimming_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 5. 渐变到紫色（1秒）
    printf("5. Fade to purple (1 second)\n");
    dimming_set_rgb(g_dimming_handle, 255, 0, 255, 1000);
    while (dimming_is_fading(g_dimming_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 6. 渐变关闭（3秒）
    printf("6. Fade to off (3 seconds)\n");
    dimming_set_rgb(g_dimming_handle, 0, 0, 0, 3000);
    while (dimming_is_fading(g_dimming_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    printf("RGB fade demo completed\n");
}

/**
 * @brief 演示单个通道控制
 */
static void demo_single_channel(void)
{
    if (!g_dimming_handle) {
        return;
    }
    
    printf("\n=== Single Channel Demo ===\n");
    
    // 1. 立即设置红色通道
    printf("1. Set red channel immediately to 128\n");
    dimming_set_immediate(g_dimming_handle, CHANNEL_RED, 128);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 2. 渐变红色通道到255（2秒）
    printf("2. Fade red channel to 255 (2 seconds)\n");
    dimming_set_with_fade(g_dimming_handle, CHANNEL_RED, 255, 2000);
    while (dimming_is_fading(g_dimming_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 3. 渐变红色通道到0（1秒）
    printf("3. Fade red channel to 0 (1 second)\n");
    dimming_set_with_fade(g_dimming_handle, CHANNEL_RED, 0, 1000);
    while (dimming_is_fading(g_dimming_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    printf("Single channel demo completed\n");
}

/**
 * @brief 演示CCT控制（暖白/冷白）
 */
static void demo_cct_control(void)
{
    if (!g_dimming_handle) {
        return;
    }
    
    printf("\n=== CCT Control Demo ===\n");
    
    // 假设通道0=暖白，通道1=冷白
    
    // 1. 渐变到暖白（2秒）
    printf("1. Fade to warm white (2 seconds)\n");
    dimming_set_cct(g_dimming_handle, 255, 0, 2000);
    while (dimming_is_fading(g_dimming_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 2. 渐变到冷白（2秒）
    printf("2. Fade to cool white (2 seconds)\n");
    dimming_set_cct(g_dimming_handle, 0, 255, 2000);
    while (dimming_is_fading(g_dimming_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 3. 渐变到中性白（2秒）
    printf("3. Fade to neutral white (2 seconds)\n");
    dimming_set_cct(g_dimming_handle, 128, 128, 2000);
    while (dimming_is_fading(g_dimming_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 4. 渐变关闭（2秒）
    printf("4. Fade to off (2 seconds)\n");
    dimming_set_cct(g_dimming_handle, 0, 0, 2000);
    while (dimming_is_fading(g_dimming_handle)) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    printf("CCT control demo completed\n");
}

/**
 * @brief 演示停止渐变功能
 */
static void demo_stop_fade(void)
{
    if (!g_dimming_handle) {
        return;
    }
    
    printf("\n=== Stop Fade Demo ===\n");
    
    // 开始渐变到红色（5秒）
    printf("Starting fade to red (5 seconds)...\n");
    dimming_set_rgb(g_dimming_handle, 255, 0, 0, 2500);
    dimming_set_rgb(g_dimming_handle, 0, 0, 0, 2500);
    // 等待2秒后停止渐变
    vTaskDelay(pdMS_TO_TICKS(5000));
    printf("Stopping fade after 5 seconds...\n");
    dimming_stop_all_fades(g_dimming_handle);
    
    // 获取当前值
    uint32_t red = dimming_get_current_value(g_dimming_handle, CHANNEL_RED);
    uint32_t green = dimming_get_current_value(g_dimming_handle, CHANNEL_GREEN);
    uint32_t blue = dimming_get_current_value(g_dimming_handle, CHANNEL_BLUE);
    
    printf("Stopped at: R=%lu, G=%lu, B=%lu\n", red, green, blue);
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    printf("Stop fade demo completed\n");
}

/**
 * @brief 主应用程序
 */
void app_main(void)
{
    printf("\n=== Dimming Library Test ===\n");
    
    // 初始化调光系统
    dimming_system_init();
    
    if (!g_dimming_handle) {
        printf("Failed to initialize dimming system. Exiting.\n");
        return;
    }
    
    // 运行演示
    demo_single_channel();
    demo_rgb_fade();
    demo_cct_control();
    demo_stop_fade();
    
    // 清理
    dimming_deinit(g_dimming_handle);
    g_dimming_handle = NULL;
    
    printf("\n=== All tests completed ===\n");
}