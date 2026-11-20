#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "bike_state.h"
#include "bike_state_private.h"

/* ---------------------------------------------------------------------------
 * TAG do módulo para o sistema de log.
 * ------------------------------------------------------------------------- */
static const char *TAG = "bike_state";

/* ---------------------------------------------------------------------------
 * Ninguém acessa sem o mutex.
 * ------------------------------------------------------------------------- */
static estado_bicicleta_t s_estado;

/* ---------------------------------------------------------------------------
 * MUTEX COM ALOCAÇÃO ESTÁTICA
 *   Estática  : memória reservada em tempo de compilação, aparece no mapa de
 *               memória, nunca falha em runtime, sem fragmentação de heap.
 *   Dinâmica  : mais simples de escrever, mas pode retornar NULL se o heap
 *               estiver cheio e fragmenta o heap em sistemas que rodam meses.
 * ------------------------------------------------------------------------- */
static StaticSemaphore_t s_mutex_buffer;
static SemaphoreHandle_t s_mutex = NULL;

/* ---------------------------------------------------------------------------
 * Conversão do timeout em ms para ticks do FreeRTOS.
 * ------------------------------------------------------------------------- */
#define TIMEOUT_TICKS  pdMS_TO_TICKS(BIKE_STATE_TIMEOUT_MS)

/* ---------------------------------------------------------------------------
 * Funções internas: tomar e devolver o mutex.
 *
 * xSemaphoreTake: tenta pegar o mutex; se estiver ocupado, a task vai para
 *   o estado BLOCKED (não gasta CPU) até o mutex liberar ou o timeout vencer.
 *   Se a Telemetria (prio 1) segura o mutex e a Freio (prio 4) o pede,
 *   a Telemetria é promovida temporariamente a prio 4 para terminar rápido.
 * ------------------------------------------------------------------------- */
static esp_err_t lock(void)
{
    if (s_mutex == NULL) {
        /* ESP_LOGE: é erro de PROGRAMAÇÃO (esqueceram o init). */
        ESP_LOGE(TAG, "bike_state_init() nao foi chamado");
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_mutex, TIMEOUT_TICKS) != pdTRUE) {
        /* ESP_LOGE: mutex preso por > 20 ms indica deadlock ou task
         * travada. Em operação normal isso NUNCA deve aparecer. */
        ESP_LOGE(TAG, "timeout ao pegar o mutex (deadlock?)");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

// xSemaphoreGive: devolve o mutex. Só pode ser chamado por quem o tomou
static void unlock(void)
{
    xSemaphoreGive(s_mutex);
}

/* Carimbo de tempo: esp_timer_get_time() devolve microssegundos desde o boot,
 * em 64 bits. Dividimos por 1000 para ms. Estourar 32 bits leva ~49 dias. */
static inline uint32_t agora_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ============================================================================
 *  API pública
 * ========================================================================== */

esp_err_t bike_state_init(void)
{
    if (s_mutex != NULL) {
        /* ESP_LOGW: chamar duas vezes não quebra nada, mas indica
         * desorganização na inicialização. Aviso, não erro. */
        ESP_LOGW(TAG, "init chamado mais de uma vez");
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_estado, 0, sizeof(s_estado));

    /* xSemaphoreCreateMutexStatic: cria o mutex usando o buffer que nós
     * fornecemos. Não pode falhar por falta de memória (o buffer já existe); */
    s_mutex = xSemaphoreCreateMutexStatic(&s_mutex_buffer);
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "falha ao criar mutex");
        return ESP_FAIL;
    }

    // ESP_LOGI: evento normal e raro, útil para ver a ordem de boot.
    ESP_LOGI(TAG, "inicializado (sizeof estado = %u bytes)",
             (unsigned)sizeof(s_estado));
    return ESP_OK;
}

esp_err_t bike_state_deinit(void)
{
    if (s_mutex == NULL) {
        ESP_LOGW(TAG, "deinit chamado sem init");
        return ESP_ERR_INVALID_STATE;
    }

    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;
    return ESP_OK;
}

SemaphoreHandle_t bike_state_test_get_mutex(void)
{
    return s_mutex;
}

esp_err_t bike_state_snapshot(estado_bicicleta_t *saida)
{
    if (saida == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = lock();
    if (err != ESP_OK) {
        return err;
    }
    /* PADRÃO SNAPSHOT: copiamos tudo de uma vez sob o mutex e soltamos.
     * Quem chamou processa a cópia à vontade, fora da seção crítica.
     */
    *saida = s_estado;
    unlock();
    return ESP_OK;
}

/* Os setters seguem sempre o mesmo ritmo:
 *   1) pegar o mutex  2) escrever  3) carimbar o tempo  4) soltar. 
 */

esp_err_t bike_state_set_velocidade(float velocidade_kmh, uint32_t pulsos_total)
{
    esp_err_t err = lock();
    if (err != ESP_OK) { return err; }
    s_estado.velocidade_kmh   = velocidade_kmh;
    s_estado.pulsos_total     = pulsos_total;
    s_estado.atualizado_em_ms = agora_ms();
    unlock();
    return ESP_OK;
}

esp_err_t bike_state_set_freio(bool freio_acionado, bool carregador_ligado)
{
    esp_err_t err = lock();
    if (err != ESP_OK) { return err; }
    // Freio e carregador são gravados juntos de propósito.
    s_estado.freio_acionado    = freio_acionado;
    s_estado.carregador_ligado = carregador_ligado;
    s_estado.atualizado_em_ms  = agora_ms();
    unlock();
    return ESP_OK;
}

esp_err_t bike_state_set_assistencia_solicitada(bool solicitada)
{
    esp_err_t err = lock();
    if (err != ESP_OK) { return err; }
    s_estado.assistencia_solicitada = solicitada;
    s_estado.atualizado_em_ms       = agora_ms();
    unlock();
    return ESP_OK;
}

esp_err_t bike_state_set_assistencia(bool ativa, uint8_t motor_duty_pct)
{
    esp_err_t err = lock();
    if (err != ESP_OK) { return err; }
    s_estado.assistencia_ativa = ativa;
    s_estado.motor_duty_pct    = (motor_duty_pct > 100U) ? 100U : motor_duty_pct;
    s_estado.atualizado_em_ms  = agora_ms();
    unlock();
    return ESP_OK;
}

esp_err_t bike_state_set_inclinada(bool inclinada)
{
    esp_err_t err = lock();
    if (err != ESP_OK) { return err; }
    s_estado.inclinada        = inclinada;
    s_estado.atualizado_em_ms = agora_ms();
    unlock();
    return ESP_OK;
}

esp_err_t bike_state_set_temperatura(float temperatura_c)
{
    /* isnan() primeiro: NaN faria TODAS as comparações de faixa dar falso
     * e passaria como válido. isinf() cobre +/- infinito (ex.: divisão por zero
     * na conversão do ADC). */
    if (isnan(temperatura_c) || isinf(temperatura_c) ||
        temperatura_c < TEMPERATURA_MIN_C || temperatura_c > TEMPERATURA_MAX_C) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = lock();
    if (err != ESP_OK) { return err; }
    s_estado.temperatura_c    = temperatura_c;
    s_estado.atualizado_em_ms = agora_ms();
    unlock();
    return ESP_OK;
}

esp_err_t bike_state_incrementar_frenagens(void) 
{
    esp_err_t err = lock();
    if (err != ESP_OK) {return err; }
    s_estado.contador_frenagens++;
    s_estado.atualizado_em_ms = agora_ms();
    unlock();
    return ESP_OK;
}