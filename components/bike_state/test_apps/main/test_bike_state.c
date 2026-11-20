/*
 * ============================================================================
 *  test_bike_state.c  -  Testes unitários do componente bike_state
 * ============================================================================
 *  ESTRUTURA DO ARQUIVO
 *    1. Fixture   : setUp/tearDown (isolamento entre testes)
 *    2. Ciclo de vida: init, deinit, uso antes do init
 *    3. Validação de argumentos
 *    4. Estado inicial
 *    5. Um teste por setter + "não altera os outros campos"
 *    6. Valores-limite (saturação do duty)
 *    7. Timestamp
 *    8. Timeout do mutex  (usa o test seam)
 *    9. Concorrência (leitura coerente / sem "tearing")
 * ============================================================================
 */

#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "unity.h"

#include "bike_state.h"
#include "bike_state_private.h"

#define TAG_TESTE "[bike_state]"   /* tag usada no menu para rodar o grupo todo */

/* ---------------------------------------------------------------------------
 * Constantes do teste
 * ------------------------------------------------------------------------- */

/* Tolerância do timeout: o mutex usa timeout em TICKS (1 tick = 10 ms com
 * FreeRTOS a 100 Hz). Ao pedir 20 ms a espera real fica entre ~1 tick a menos
 * (começamos no meio de um tick) e alguns ticks a mais (escalonador). */
#define TOLERANCIA_ABAIXO_MS   (portTICK_PERIOD_MS)
#define TOLERANCIA_ACIMA_MS    50

/* Teste de concorrência */
#define ITERACOES_CONCORRENCIA   2000   /* suficiente para muitas disputas, termina em ms */
#define STACK_ESCRITOR_BYTES     3072   /* mesma ordem de grandeza das tasks reais */
#define PRIO_ESCRITOR            5      /* >= prioridade do unity (main=1): força disputa real */
#define ESPERA_ESCRITOR_MS       2000   /* teto para o escritor terminar; se estourar = bug */

/* Pausa entre duas escritas para provar que o timestamp avança.
 * 2 ticks garante diferença visível em ms mesmo com a granularidade do tick. */
#define PAUSA_TIMESTAMP_MS     (2 * portTICK_PERIOD_MS)

/* ---------------------------------------------------------------------------
 * Helpers de asserção para esp_err_t.
 * Por que não usar TEST_ASSERT_EQUAL direto? Falharia com "Expected 0 Was 259"
 * (número cru). Com o nome ("ESP_ERR_TIMEOUT") o diagnóstico fica legível.
 * ------------------------------------------------------------------------- */
#define ASSERT_ESP(esperado, expressao)                                        \
    do {                                                                       \
        esp_err_t err_assert_ = (expressao);   /* avaliada UMA vez */          \
        TEST_ASSERT_EQUAL_MESSAGE((esperado), err_assert_,                     \
                                  esp_err_to_name(err_assert_));               \
    } while (0)
#define ASSERT_ESP_OK(expressao)  ASSERT_ESP(ESP_OK, (expressao))

/* ============================================================================
 *  1. FIXTURE - garante ISOLAMENTO
 * ============================================================================
 * Unity chama setUp() ANTES e tearDown() DEPOIS de cada teste.
 * Resultado: todo teste começa com o bike_state recém-inicializado e zerado,
 * independentemente do que o teste anterior fez (ou de ter falhado no meio).
 * Se os testes compartilhassem estado, um teste que falha poderia causar
 * falhas em cascata em testes inocentes: o pesadelo de depurar.
 * ========================================================================== */
void setUp(void)
{
    /* Garante ponto de partida limpo mesmo se um teste anterior deixou o
     * módulo inicializado. O retorno é ignorado de propósito: ESP_ERR_INVALID_STATE
     * aqui só significa "já estava limpo". */
    (void)bike_state_deinit();
    ESP_ERROR_CHECK(bike_state_init());
}

void tearDown(void)
{
    (void)bike_state_deinit();   /* idem: pode já ter sido desfeito pelo teste */
    esp_log_level_set("bike_state", ESP_LOG_INFO);  /* restaura se um teste silenciou */
}

/* Alguns testes provocam erros DE PROPÓSITO e o módulo loga ESP_LOGE/W.
 * Silenciamos o módulo nesses testes para o relatório não parecer falha real. */
static void silenciar_log_bike_state(void)
{
    esp_log_level_set("bike_state", ESP_LOG_NONE);
}

/* ============================================================================
 *  2. CICLO DE VIDA
 * ========================================================================== */

TEST_CASE("init duplo retorna INVALID_STATE e nao quebra o estado", TAG_TESTE)
{
    /* ARRANGE: setUp já chamou init uma vez. Gravamos um valor reconhecível. */
    silenciar_log_bike_state();
    ASSERT_ESP_OK(bike_state_set_temperatura(25.0f));

    /* ACT */
    esp_err_t err = bike_state_init();

    /* ASSERT: recusou, E o estado anterior sobreviveu (init duplo não zera). */
    ASSERT_ESP(ESP_ERR_INVALID_STATE, err);
    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 25.0f, s.temperatura_c);
}

TEST_CASE("deinit seguido de init permite reinicializar", TAG_TESTE)
{
    /* ARRANGE + ACT */
    ASSERT_ESP_OK(bike_state_deinit());
    esp_err_t err = bike_state_init();

    /* ASSERT */
    ASSERT_ESP_OK(err);
}

TEST_CASE("deinit sem init retorna INVALID_STATE", TAG_TESTE)
{
    /* ARRANGE: desfaz o init do setUp */
    silenciar_log_bike_state();
    ASSERT_ESP_OK(bike_state_deinit());

    /* ACT + ASSERT: segundo deinit seguido */
    ASSERT_ESP(ESP_ERR_INVALID_STATE, bike_state_deinit());
}

TEST_CASE("snapshot antes do init retorna INVALID_STATE", TAG_TESTE)
{
    /* ARRANGE */
    silenciar_log_bike_state();
    ASSERT_ESP_OK(bike_state_deinit());
    estado_bicicleta_t s;

    /* ACT + ASSERT */
    ASSERT_ESP(ESP_ERR_INVALID_STATE, bike_state_snapshot(&s));
}

TEST_CASE("setter antes do init retorna INVALID_STATE", TAG_TESTE)
{
    /* Prova que o erro de "esqueci o init" aparece em QUALQUER entrada da API,
     * não só no snapshot. */
    silenciar_log_bike_state();
    ASSERT_ESP_OK(bike_state_deinit());

    ASSERT_ESP(ESP_ERR_INVALID_STATE, bike_state_set_inclinada(true));
}

/* ============================================================================
 *  3. VALIDAÇÃO DE ARGUMENTOS
 * ========================================================================== */

TEST_CASE("snapshot com ponteiro NULL retorna INVALID_ARG", TAG_TESTE)
{
    ASSERT_ESP(ESP_ERR_INVALID_ARG, bike_state_snapshot(NULL));
}

/* ============================================================================
 *  4. ESTADO INICIAL
 * ========================================================================== */

TEST_CASE("estado inicial tem todos os campos zerados", TAG_TESTE)
{
    /* ARRANGE: setUp acabou de inicializar. */
    estado_bicicleta_t s;

    /* ACT */
    ASSERT_ESP_OK(bike_state_snapshot(&s));

    /* ASSERT: um campo por linha. Se algum falhar, a linha do Unity aponta
     * exatamente qual campo veio com lixo. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.velocidade_kmh);
    TEST_ASSERT_EQUAL_UINT32(0, s.pulsos_total);
    TEST_ASSERT_FALSE(s.freio_acionado);
    TEST_ASSERT_FALSE(s.carregador_ligado);
    TEST_ASSERT_FALSE(s.assistencia_solicitada);
    TEST_ASSERT_FALSE(s.assistencia_ativa);
    TEST_ASSERT_FALSE(s.inclinada);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, s.temperatura_c);
    TEST_ASSERT_EQUAL_UINT8(0, s.motor_duty_pct);
    TEST_ASSERT_EQUAL_UINT32(0, s.atualizado_em_ms);
}

/* ============================================================================
 *  5. UM TESTE POR SETTER
 * ========================================================================== */

TEST_CASE("set_velocidade grava velocidade e pulsos", TAG_TESTE)
{
    /* ARRANGE: valores distintos e reconhecíveis (evite 0/1, que coincidem
     * com o valor inicial e escondem bugs de "não gravou"). */
    const float     vel_esperada     = 17.5f;
    const uint32_t  pulsos_esperados = 1234;

    /* ACT */
    ASSERT_ESP_OK(bike_state_set_velocidade(vel_esperada, pulsos_esperados));

    /* ASSERT */
    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, vel_esperada, s.velocidade_kmh);
    TEST_ASSERT_EQUAL_UINT32(pulsos_esperados, s.pulsos_total);
}

TEST_CASE("set_freio grava freio e carregador juntos", TAG_TESTE)
{
    ASSERT_ESP_OK(bike_state_set_freio(true, true));

    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_TRUE(s.freio_acionado);
    TEST_ASSERT_TRUE(s.carregador_ligado);
}

TEST_CASE("set_freio aceita combinacoes independentes", TAG_TESTE)
{
    /* O módulo NÃO impõe a regra "carregador só liga com freio": essa regra é
     * da Task Freio / Assistência. bike_state só guarda dados. Este teste
     * documenta essa fronteira de responsabilidade. */
    ASSERT_ESP_OK(bike_state_set_freio(false, true));

    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_FALSE(s.freio_acionado);
    TEST_ASSERT_TRUE(s.carregador_ligado);
}

TEST_CASE("set_assistencia_solicitada grava o pedido do botao", TAG_TESTE)
{
    ASSERT_ESP_OK(bike_state_set_assistencia_solicitada(true));

    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_TRUE(s.assistencia_solicitada);
}

TEST_CASE("set_assistencia grava ativa e duty", TAG_TESTE)
{
    ASSERT_ESP_OK(bike_state_set_assistencia(true, 60));

    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_TRUE(s.assistencia_ativa);
    TEST_ASSERT_EQUAL_UINT8(60, s.motor_duty_pct);
}

TEST_CASE("set_inclinada grava o estado de inclinacao", TAG_TESTE)
{
    ASSERT_ESP_OK(bike_state_set_inclinada(true));

    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_TRUE(s.inclinada);
}

TEST_CASE("set_temperatura grava a temperatura", TAG_TESTE)
{
    ASSERT_ESP_OK(bike_state_set_temperatura(36.6f));

    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 36.6f, s.temperatura_c);
}

TEST_CASE("set_incrementa_frenagens grava duas frenagens", TAG_TESTE) 
{
    ASSERT_ESP_OK(bike_state_incrementar_frenagens());
    ASSERT_ESP_OK(bike_state_incrementar_frenagens());

    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_EQUAL_UINT32(2, s.contador_frenagens);
}

TEST_CASE("um setter nao altera campos que nao sao dele", TAG_TESTE)
{
    /* Teste de NÃO-REGRESSÃO de efeito colateral: o bug clássico de copiar e
     * colar um setter e gravar no campo errado. */
    /* ARRANGE: deixa três campos com valores conhecidos */
    ASSERT_ESP_OK(bike_state_set_velocidade(10.0f, 500));
    ASSERT_ESP_OK(bike_state_set_temperatura(30.0f));
    ASSERT_ESP_OK(bike_state_set_inclinada(true));

    /* ACT: escreve SÓ o freio */
    ASSERT_ESP_OK(bike_state_set_freio(true, true));

    /* ASSERT: os outros campos continuam como estavam */
    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, s.velocidade_kmh);
    TEST_ASSERT_EQUAL_UINT32(500, s.pulsos_total);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f, s.temperatura_c);
    TEST_ASSERT_TRUE(s.inclinada);
}

/* ============================================================================
 *  6. VALORES-LIMITE: saturação do duty em 100 %
 *  Bugs vivem nas fronteiras: testamos abaixo, na, e acima do limite.
 * ========================================================================== */

TEST_CASE("duty 0 e preservado", TAG_TESTE)
{
    ASSERT_ESP_OK(bike_state_set_assistencia(false, 0));
    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_EQUAL_UINT8(0, s.motor_duty_pct);
}

TEST_CASE("duty 100 e preservado (limite exato)", TAG_TESTE)
{
    ASSERT_ESP_OK(bike_state_set_assistencia(true, 100));
    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_EQUAL_UINT8(100, s.motor_duty_pct);
}

TEST_CASE("duty 101 satura em 100 (primeiro valor invalido)", TAG_TESTE)
{
    ASSERT_ESP_OK(bike_state_set_assistencia(true, 101));
    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_EQUAL_UINT8(100, s.motor_duty_pct);
}

TEST_CASE("duty 255 satura em 100 (maximo do uint8)", TAG_TESTE)
{
    ASSERT_ESP_OK(bike_state_set_assistencia(true, UINT8_MAX));
    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_EQUAL_UINT8(100, s.motor_duty_pct);
}

/* ============================================================================
 *  7. TIMESTAMP
 * ========================================================================== */

TEST_CASE("escrita carimba o tempo e ele avanca entre escritas", TAG_TESTE)
{
    /* ARRANGE: primeira escrita */
    ASSERT_ESP_OK(bike_state_set_inclinada(true));
    estado_bicicleta_t antes;
    ASSERT_ESP_OK(bike_state_snapshot(&antes));

    /* ACT: espera um tempo CONHECIDO e escreve de novo.
     * vTaskDelay bloqueia a task (sem busy-wait) e é a forma correta de
     * "deixar o tempo passar" em um teste. */
    vTaskDelay(pdMS_TO_TICKS(PAUSA_TIMESTAMP_MS));
    ASSERT_ESP_OK(bike_state_set_inclinada(false));
    estado_bicicleta_t depois;
    ASSERT_ESP_OK(bike_state_snapshot(&depois));

    /* ASSERT: o relógio andou. Comparamos "maior que", não valor exato:
     * tempo real nunca é exato, e teste que exige exatidão fica instável. */
    TEST_ASSERT_GREATER_THAN_UINT32(antes.atualizado_em_ms, depois.atualizado_em_ms);
}

/* ============================================================================
 *  8. TIMEOUT DO MUTEX  (usa o test seam bike_state_test_get_mutex)
 * ========================================================================== */

TEST_CASE("mutex ocupado gera ESP_ERR_TIMEOUT dentro do prazo", TAG_TESTE)
{
    /* ARRANGE: o teste PEGA o mutex e o segura. Mutex padrão não é
     * recursivo: se a mesma task tentar pegá-lo de novo, bloqueia até o
     * timeout. Isso simula perfeitamente "outra task travada segurando-o",
     * sem precisar criar outra task. */
    silenciar_log_bike_state();   /* o módulo loga ESP_LOGE de propósito aqui */
    SemaphoreHandle_t mutex = bike_state_test_get_mutex();
    TEST_ASSERT_NOT_NULL(mutex);
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(mutex, 0));

    /* ACT: mede quanto tempo a chamada levou para desistir */
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = bike_state_set_inclinada(true);
    int32_t decorrido_ms = (int32_t)((esp_timer_get_time() - t0) / 1000);

    /* LIMPEZA ANTES DOS ASSERTS: se um assert falhar, ele aborta o teste
     * (longjmp) e o código seguinte NÃO roda. Devolver o mutex depois dos
     * asserts deixaria o recurso preso. O tearDown também protege, mas
     * este é o hábito certo: limpe antes de verificar. */
    xSemaphoreGive(mutex);

    /* ASSERT 1: retornou o erro certo */
    ASSERT_ESP(ESP_ERR_TIMEOUT, err);
    /* ASSERT 2: esperou ~BIKE_STATE_TIMEOUT_MS (não retornou na hora, não
     * ficou esperando para sempre). */
    TEST_ASSERT_GREATER_OR_EQUAL_INT32(BIKE_STATE_TIMEOUT_MS - TOLERANCIA_ABAIXO_MS, decorrido_ms);
    TEST_ASSERT_LESS_OR_EQUAL_INT32(BIKE_STATE_TIMEOUT_MS + TOLERANCIA_ACIMA_MS, decorrido_ms);
}

TEST_CASE("depois de um timeout o modulo volta a funcionar", TAG_TESTE)
{
    /* Verifica RECUPERAÇÃO: um timeout não pode deixar o módulo "quebrado". */
    silenciar_log_bike_state();
    SemaphoreHandle_t mutex = bike_state_test_get_mutex();
    TEST_ASSERT_EQUAL(pdTRUE, xSemaphoreTake(mutex, 0));
    ASSERT_ESP(ESP_ERR_TIMEOUT, bike_state_set_inclinada(true));
    xSemaphoreGive(mutex);

    /* ACT + ASSERT: agora que o mutex foi liberado, a escrita funciona */
    ASSERT_ESP_OK(bike_state_set_inclinada(true));
    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_TRUE(s.inclinada);
}

/* ============================================================================
 *  9. CONCORRÊNCIA: leitura coerente (sem "tearing")
 * ========================================================================== */

/* Contexto compartilhado entre o teste e a task escritora.
 * Só a task escritora grava 'erros'; o teste só lê DEPOIS de receber o
 * semáforo 'terminou' (o give/take cria a ordenação de memória necessária),
 * então não há corrida sobre este campo. */
typedef struct {
    SemaphoreHandle_t terminou;   /* escritor dá "give" ao acabar */
    uint32_t          erros;      /* quantas escritas retornaram erro */
} contexto_escritor_t;

/* Task escritora: alterna (freio=1, carregador=1) e (freio=0, carregador=0)
 * ITERACOES_CONCORRENCIA vezes, SEMPRE com os dois campos iguais. */
static void task_escritora(void *arg)
{
    contexto_escritor_t *ctx = (contexto_escritor_t *)arg;
    bool valor = false;

    for (uint32_t i = 0; i < ITERACOES_CONCORRENCIA; i++) {
        valor = !valor;
        if (bike_state_set_freio(valor, valor) != ESP_OK) {
            ctx->erros++;
        }
    }
    xSemaphoreGive(ctx->terminou);
    /* vTaskDelete(NULL): task se auto-remove. NUNCA retorne de uma task
     * FreeRTOS: sair da função sem deletar causa abort. */
    vTaskDelete(NULL);
}

TEST_CASE("leitura concorrente nunca enxerga freio e carregador discordando", TAG_TESTE)
{
    /* ARRANGE */
    contexto_escritor_t ctx = { .terminou = xSemaphoreCreateBinary(), .erros = 0 };
    TEST_ASSERT_NOT_NULL(ctx.terminou);
    uint32_t leituras_incoerentes = 0;
    uint32_t leituras_com_erro    = 0;

    /* tskNO_AFFINITY: deixa o escalonador escolher o núcleo (no ESP32 de dois
     * núcleos a task pode rodar em PARALELO real com o teste, o que aumenta
     * a chance de disputa). */
    BaseType_t ok = xTaskCreatePinnedToCore(task_escritora, "escritora_teste",
                                            STACK_ESCRITOR_BYTES, &ctx,
                                            PRIO_ESCRITOR, NULL, tskNO_AFFINITY);
    TEST_ASSERT_EQUAL(pdPASS, ok);

    /* ACT: lê o estado enquanto o escritor escreve */
    for (uint32_t i = 0; i < ITERACOES_CONCORRENCIA; i++) {
        estado_bicicleta_t s;
        if (bike_state_snapshot(&s) != ESP_OK) {
            leituras_com_erro++;
        } else if (s.freio_acionado != s.carregador_ligado) {
            leituras_incoerentes++;   /* o escritor SEMPRE grava os dois iguais */
        }
    }

    /* Espera o escritor terminar (bloqueando, sem busy-wait) com um TETO.
     * Se estourar, o escritor travou: isso por si só é falha do teste. */
    BaseType_t terminou = xSemaphoreTake(ctx.terminou, pdMS_TO_TICKS(ESPERA_ESCRITOR_MS));
    /* Dá tempo à IDLE task de limpar a task já deletada (libera TCB e stack)
     * antes de apagarmos o semáforo; evita race ao destruir recursos. */
    vTaskDelay(pdMS_TO_TICKS(portTICK_PERIOD_MS * 2));
    vSemaphoreDelete(ctx.terminou);

    /* ASSERT: contamos no loop e verificamos só no fim (um assert dentro do
     * loop abortaria o teste com o escritor ainda rodando). */
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, terminou, "escritor nao terminou no prazo");
    TEST_ASSERT_EQUAL_UINT32(0, ctx.erros);
    TEST_ASSERT_EQUAL_UINT32(0, leituras_com_erro);
    TEST_ASSERT_EQUAL_UINT32(0, leituras_incoerentes);
}

/* Task que só incrementa o contador, ITERACOES_CONCORRENCIA vezes. */
static void task_incrementadora(void *arg)
{
    contexto_escritor_t *ctx = (contexto_escritor_t *)arg;

    for (uint32_t i = 0; i < ITERACOES_CONCORRENCIA; i++) {
        if (bike_state_incrementar_frenagens() != ESP_OK) {
            ctx->erros++;
        }
    }
    xSemaphoreGive(ctx->terminou);
    vTaskDelete(NULL);
}

TEST_CASE("incrementar_frenagens nao perde contagens entre duas tasks", TAG_TESTE)
{
    /* ARRANGE */
    contexto_escritor_t ctx = { .terminou = xSemaphoreCreateBinary(), .erros = 0 };
    TEST_ASSERT_NOT_NULL(ctx.terminou);
    uint32_t erros_do_teste = 0;

    BaseType_t ok = xTaskCreatePinnedToCore(task_incrementadora, "incrementadora_teste",
                                            STACK_ESCRITOR_BYTES, &ctx,
                                            PRIO_ESCRITOR, NULL, tskNO_AFFINITY);
    TEST_ASSERT_EQUAL(pdPASS, ok);

    /* ACT: a task do teste também incrementa, disputando com a outra */
    for (uint32_t i = 0; i < ITERACOES_CONCORRENCIA; i++) {
        if (bike_state_incrementar_frenagens() != ESP_OK) {
            erros_do_teste++;
        }
    }
    BaseType_t terminou = xSemaphoreTake(ctx.terminou, pdMS_TO_TICKS(ESPERA_ESCRITOR_MS));
    vTaskDelay(pdMS_TO_TICKS(portTICK_PERIOD_MS * 2));  /* IDLE limpa a task deletada */
    vSemaphoreDelete(ctx.terminou);

    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));

    /* ASSERT: cada incremento conta; 2 tasks x N = 2N, nem um a menos */
    TEST_ASSERT_EQUAL_MESSAGE(pdTRUE, terminou, "incrementadora nao terminou no prazo");
    TEST_ASSERT_EQUAL_UINT32(0, ctx.erros);
    TEST_ASSERT_EQUAL_UINT32(0, erros_do_teste);
    TEST_ASSERT_EQUAL_UINT32(2U * ITERACOES_CONCORRENCIA, s.contador_frenagens);
}

TEST_CASE("set_temperatura rejeita NaN e nao altera o estado", TAG_TESTE)
{
    /* ARRANGE: valor conhecido para provar que NÃO foi sobrescrito */
    ASSERT_ESP_OK(bike_state_set_temperatura(30.0f));

    /* ACT + ASSERT */
    ASSERT_ESP(ESP_ERR_INVALID_ARG, bike_state_set_temperatura(NAN));

    estado_bicicleta_t s;
    ASSERT_ESP_OK(bike_state_snapshot(&s));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 30.0f, s.temperatura_c);
}

TEST_CASE("set_temperatura rejeita infinito", TAG_TESTE)
{
    ASSERT_ESP(ESP_ERR_INVALID_ARG, bike_state_set_temperatura(INFINITY));
}

TEST_CASE("set_temperatura rejeita abaixo do zero absoluto", TAG_TESTE)
{
    ASSERT_ESP(ESP_ERR_INVALID_ARG, bike_state_set_temperatura(-300.0f));
}

TEST_CASE("set_temperatura aceita o limite minimo exato", TAG_TESTE)
{
    ASSERT_ESP_OK(bike_state_set_temperatura(TEMPERATURA_MIN_C));
}