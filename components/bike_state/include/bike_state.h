#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"


#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
* Timeout padrão para pegar o mutex. Não vamos usar portMax delay pois não queremos que o sistema trave.
* ------------------------------------------------------------------------- */
#define BIKE_STATE_TIMEOUT_MS   20U
/* Origem dos limites: -273.15 °C é o zero absoluto (nada pode estar abaixo).
* 150 °C é bem acima do que o NTC do KY-013/KY-028 mede de forma útil; acima
* disso a leitura é considerada defeito do sensor ou do ADC. */
#define TEMPERATURA_MIN_C   (-273.15f)
#define TEMPERATURA_MAX_C   (150.0f)

/* ---------------------------------------------------------------------------
 * Struct com tudo que o sistema sabe sobre a bicicleta.
 * ------------------------------------------------------------------------- */
typedef struct {
    float    velocidade_kmh;        /* Velocidade calculada pela Task Roda      */
    uint32_t pulsos_total;          /* Pulsos do Hall acumulados (odômetro)     */
    uint32_t contador_frenagens;    /* Conta a quantidade de frenagens          */
    bool     freio_acionado;        /* true = manete apertada                   */
    bool     carregador_ligado;     /* true = relé KY-019 do carregador fechado */
    bool     assistencia_solicitada;/* true = botão do guidão pressionado       */
    bool     assistencia_ativa;     /* true = motor efetivamente ligado         */
    bool     inclinada;             /* true = KY-020 indica inclinação perigosa */
    float    temperatura_c;         /* Temperatura do NTC                       */
    uint8_t  motor_duty_pct;        /* Duty do PWM do motor simulado, 0..100    */
    uint32_t atualizado_em_ms;      /* Instante da última escrita (ms desde boot)*/
} estado_bicicleta_t;

/**
 * @brief Cria o mutex e zera o estado. Chamar UMA vez, antes de criar tasks
 *        ou habilitar interrupções.
 * @return ESP_OK, ou ESP_ERR_INVALID_STATE se já foi chamada.
 */
esp_err_t bike_state_init(void);

/**
 * @brief Destrói o mutex e volta ao estado "não inicializado".
 *
 * Existe principalmente para permitir testes isolados (cada teste começa do
 * zero). Em produção o estado vive até o desligamento e esta função não é
 * chamada. NÃO chame com outras tasks ainda usando a API: elas passariam a
 * receber ESP_ERR_INVALID_STATE.
 *
 * @return ESP_OK, ou ESP_ERR_INVALID_STATE se não estava inicializado.
 */
esp_err_t bike_state_deinit(void);

/**
 * @brief Copia o estado atual para *saida (padrão "snapshot").
 * @return ESP_OK; ESP_ERR_INVALID_ARG (saida NULL);
 *         ESP_ERR_INVALID_STATE (init não chamado);
 *         ESP_ERR_TIMEOUT (mutex ocupado por mais de BIKE_STATE_TIMEOUT_MS).
 */
esp_err_t bike_state_snapshot(estado_bicicleta_t *saida);

/* Setters: um por campo escrito por alguém. Mesmos códigos de retorno acima. */
esp_err_t bike_state_set_velocidade(float velocidade_kmh, uint32_t pulsos_total);
esp_err_t bike_state_set_freio(bool freio_acionado, bool carregador_ligado);
esp_err_t bike_state_set_assistencia_solicitada(bool solicitada);
esp_err_t bike_state_set_assistencia(bool ativa, uint8_t motor_duty_pct);
esp_err_t bike_state_set_inclinada(bool inclinada);
esp_err_t bike_state_set_temperatura(float temperatura_c);
esp_err_t bike_state_incrementar_frenagens(void);

#ifdef __cplusplus
}
#endif