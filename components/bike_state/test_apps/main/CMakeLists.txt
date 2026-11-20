# ============================================================================
# CMakeLists.txt do "main" do app de teste.
#
# SRC_DIRS "."      : compila todo .c da pasta. Para testes isso é aceitável
#                     (a doc recomenda), pois queremos que novos test_*.c
#                     entrem sem editar este arquivo.
# PRIV_INCLUDE_DIRS : "../../private_include" é o header de acesso a testes
#                     do bike_state (relativo a ESTA pasta main/).
# PRIV_REQUIRES     : unity (framework) e bike_state (alvo do teste).
#
# WHOLE_ARCHIVE  <<< A ARMADILHA MAIS COMUM DE TESTES UNITÁRIOS NO ESP-IDF >>>
#   TEST_CASE registra cada teste por meio de um "construtor" estático: ninguém
#   chama as funções de teste diretamente. O linker, ao montar a biblioteca
#   estática, descarta arquivos que nenhum outro código referencia. Resultado
#   sem WHOLE_ARCHIVE: o build passa, o menu aparece VAZIO e você acha que o
#   Unity está quebrado. WHOLE_ARCHIVE manda o linker manter tudo.
# ============================================================================
idf_component_register(
    SRC_DIRS "."
    PRIV_INCLUDE_DIRS "." "../../private_include"
    PRIV_REQUIRES unity bike_state freertos esp_timer
    WHOLE_ARCHIVE
)