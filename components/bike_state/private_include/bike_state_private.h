#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @brief Devolve o handle do mutex interno (NULL se não inicializado).
 * @warning SOMENTE PARA TESTES. Quem segurar este mutex bloqueia todo o
 *          bike_state; sempre devolva com xSemaphoreGive.
 */
SemaphoreHandle_t bike_state_test_get_mutex(void);