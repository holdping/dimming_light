idf_component_register(
    SRCS "dimming_test_main.c"
    INCLUDE_DIRS "."
    REQUIRES dimming_lib driver freertos
)
