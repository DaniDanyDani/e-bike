# ============================================================================
# CMakeLists.txt do componente bike_state
#
#  SRCS             -> arquivos .c compilados neste componente
#  INCLUDE_DIRS     -> headers VISÍVEIS para quem depender de nós
#  PRIV_INCLUDE_DIRS-> headers visíveis só DENTRO deste componente.
#                      Aqui: private_include/ (ponto de acesso para testes).
#                      Outros componentes NÃO enxergam esta pasta; o app de
#                      teste a adiciona explicitamente de propósito.
#  PRIV_REQUIRES    -> dependências usadas só dentro dos .c
# ============================================================================
idf_component_register(
    SRCS "bike_state.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    PRIV_REQUIRES freertos log esp_timer
)