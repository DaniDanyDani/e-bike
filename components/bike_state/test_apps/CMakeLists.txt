# ============================================================================
# CMakeLists.txt (RAIZ do app de teste) - é um projeto ESP-IDF completo,
# independente do projeto principal. Ele tem seu próprio build/ e sdkconfig.
# ============================================================================
cmake_minimum_required(VERSION 3.16)

# EXTRA_COMPONENT_DIRS: diz ao build "procure componentes também aqui".
# Apontamos para a pasta PAI (components/bike_state), que é o componente sob
# teste. Sem esta linha o app de teste não encontraria o bike_state, pois ele
# não está numa pasta "components/" desta raiz.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/..")

# COMPONENTS main: compila só "main" e o que ele REQUISITAR (bike_state,
# unity, freertos...). Sem isto o IDF compilaria dezenas de componentes
# desnecessários (Wi-Fi, Bluetooth...) e o build demoraria bem mais.
# Precisa vir ANTES do include do project.cmake.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_bike_state)