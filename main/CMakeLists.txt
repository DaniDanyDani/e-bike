# O componente "main" é especial no ESP-IDF: sempre existe e contém app_main.
# REQUIRES bike_state: sem esta linha o #include "bike_state.h" não é encontrado.
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bike_state
    PRIV_REQUIRES freertos
)