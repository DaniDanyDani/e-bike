/*
 * ============================================================================
 *  test_app_main.c  -  Ponto de entrada do app de testes do bike_state
 * ============================================================================
 *
 *  PAPEL: no app de teste, app_main não executa a bicicleta. Ele apenas
 *  entrega o controle ao menu interativo do Unity.
 *
 *  COMO USAR: depois de "idf.py flash monitor", aperte Enter no monitor.
 *  O menu lista todos os TEST_CASE. Digite:
 *      *             -> roda todos
 *      [bike_state]  -> roda todos com essa tag
 *      3             -> roda o teste de índice 3
 *      "nome"        -> roda o teste com esse nome entre aspas
 * ============================================================================
 */
#include "unity.h"

void app_main(void)
{
    /* unity_run_menu: imprime o menu e fica esperando comandos pela serial.
     * Quem bloqueia aqui é a leitura da UART (não há busy-wait). */
    unity_run_menu();
}