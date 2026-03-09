idf_component_register(
    SRCS "src/dimming_lib_new.c" "src/platform_timer_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer freertos
)
