/*
 * main.c - Ponto de entrada do app real da e-bike (versão parcial).
 *
 * Os testes NÃO ficam mais aqui: vivem em components/<comp>/test_apps/.
 * Ordem de inicialização completa e integração das tasks virão no item C.
 */
#include "esp_log.h"
#include "bike_state.h"

static const char *TAG = "main";

void app_main(void)
{
    /* ESP_ERROR_CHECK: sem estado compartilhado nada funciona; abortar com
     * mensagem clara é melhor que continuar quebrado. */
    ESP_ERROR_CHECK(bike_state_init());
    ESP_LOGI(TAG, "e-bike: estado inicializado (tasks serao adicionadas nos proximos modulos)");
}