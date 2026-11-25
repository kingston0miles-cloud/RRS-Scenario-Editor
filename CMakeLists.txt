cmake_minimum_required(VERSION 3.20)

project(rrs-scenario-editor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)

# Исходники в кодировке UTF-8 (русские строки)
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(RSE_BUILD_TESTS "Собирать тесты" ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

#------------------------------------------------------------------------------
# Библиотека чтения маршрута RRS (только Qt Core, без графики)
#------------------------------------------------------------------------------
add_library(rse-route STATIC
    src/route/route-data.h
    src/route/route-data.cpp
    src/route/route.h
    src/route/route.cpp
)

target_include_directories(rse-route PUBLIC src)
target_link_libraries(rse-route PUBLIC Qt6::Core)

#------------------------------------------------------------------------------
# Каталог подвижного состава игры (только Qt Core)
#------------------------------------------------------------------------------
add_library(rse-catalog STATIC
    src/catalog/catalog-data.h
    src/catalog/vehicle-catalog.h
    src/catalog/vehicle-catalog.cpp
)

target_include_directories(rse-catalog PUBLIC src)
target_link_libraries(rse-catalog PUBLIC Qt6::Core)

#------------------------------------------------------------------------------
# Расстановка поездов на путях и проверки (только Qt Core)
#------------------------------------------------------------------------------
add_library(rse-scenario STATIC
    src/scenario/footprint.h
    src/scenario/footprint.cpp
    src/scenario/scenario.h
    src/scenario/scenario.cpp
    src/scenario/consist.h
    src/scenario/consist.cpp
    src/scenario/pathfinder.h
    src/scenario/pathfinder.cpp
    src/scenario/dynamics.h
    src/scenario/dynamics.cpp
    src/scenario/autotriggers.h
    src/scenario/autotriggers.cpp
    src/scenario/stationroute.h
    src/scenario/stationroute.cpp
    src/scenario/trip.h
    src/scenario/trip.cpp
    src/scenario/events.h
    src/scenario/events.cpp
    src/scenario/project.h
    src/scenario/project.cpp
    src/scenario/export.h
    src/scenario/export.cpp
    src/scenario/importer.h
    src/scenario/importer.cpp
    src/scenario/formation.h
    src/scenario/formation.cpp
    src/scenario/checker.h
    src/scenario/checker.cpp
)

target_include_directories(rse-scenario PUBLIC src)
target_link_libraries(rse-scenario PUBLIC rse-route rse-catalog Qt6::Core)

#------------------------------------------------------------------------------
# Интерфейс (Qt Widgets)
#------------------------------------------------------------------------------
add_library(rse-ui STATIC
    src/ui/check-dialog.h
    src/ui/check-dialog.cpp
    src/ui/continuation-dialog.h
    src/ui/continuation-dialog.cpp
    src/ui/main-window.h
    src/ui/main-window.cpp
    src/ui/map-widget.h
    src/ui/map-widget.cpp
    src/ui/rolling-stock-panel.h
    src/ui/rolling-stock-panel.cpp
    src/ui/consist-editor-dialog.h
    src/ui/consist-editor-dialog.cpp
    src/ui/place-train-dialog.h
    src/ui/place-train-dialog.cpp
    src/ui/trains-panel.h
    src/ui/workflow-bar.h
    src/ui/trains-panel.cpp
    src/ui/workflow-bar.cpp
    src/ui/timetable-dialog.h
    src/ui/timetable-dialog.cpp
    src/ui/export-dialog.h
    src/ui/export-dialog.cpp
    src/ui/event-dialog.h
    src/ui/event-dialog.cpp
    src/ui/auto-events-panel.h
    src/ui/auto-events-panel.cpp
    src/ui/events-panel.h
    src/ui/events-panel.cpp
)

target_include_directories(rse-ui PUBLIC src)
target_link_libraries(rse-ui PUBLIC rse-route rse-catalog rse-scenario Qt6::Widgets)

#------------------------------------------------------------------------------
# Приложение
#------------------------------------------------------------------------------
add_executable(rrs-scenario-editor src/main.cpp)

target_link_libraries(rrs-scenario-editor PRIVATE rse-ui)

if(WIN32)
    # Без консольного окна в релизной сборке
    set_target_properties(rrs-scenario-editor PROPERTIES WIN32_EXECUTABLE ON)
endif()

# Шаг "install" (его запускает Qt Creator после сборки)
install(TARGETS rrs-scenario-editor RUNTIME DESTINATION bin)

#------------------------------------------------------------------------------
# Тесты
#------------------------------------------------------------------------------
if(RSE_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()

    add_executable(test-route tests/test-route.cpp)
    target_link_libraries(test-route PRIVATE rse-route Qt6::Test)
    add_test(NAME route COMMAND test-route)

    add_executable(test-scenario tests/test-scenario.cpp)
    target_link_libraries(test-scenario PRIVATE rse-scenario Qt6::Test)
    add_test(NAME scenario COMMAND test-scenario)

    add_executable(test-checker tests/test-checker.cpp)
    target_link_libraries(test-checker PRIVATE rse-scenario Qt6::Test)
    add_test(NAME checker COMMAND test-checker)

    add_executable(test-stationroute tests/test-stationroute.cpp)
    target_link_libraries(test-stationroute PRIVATE rse-scenario Qt6::Test)
    add_test(NAME stationroute COMMAND test-stationroute)

    add_executable(test-trip tests/test-trip.cpp)
    target_link_libraries(test-trip PRIVATE rse-scenario Qt6::Test)
    add_test(NAME trip COMMAND test-trip)

    add_executable(test-timetable-dialog tests/test-timetable-dialog.cpp)
    target_link_libraries(test-timetable-dialog PRIVATE rse-ui Qt6::Test)
    add_test(NAME timetable-dialog COMMAND test-timetable-dialog)
    set_tests_properties(timetable-dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test-export tests/test-export.cpp)
    target_link_libraries(test-export PRIVATE rse-scenario Qt6::Test)
    add_test(NAME export COMMAND test-export)

    add_executable(test-import tests/test-import.cpp)
    target_link_libraries(test-import PRIVATE rse-scenario Qt6::Test)
    add_test(NAME import COMMAND test-import)

    add_executable(test-consist tests/test-consist.cpp)
    target_link_libraries(test-consist PRIVATE rse-ui Qt6::Test)
    add_test(NAME consist COMMAND test-consist)
    set_tests_properties(consist PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(test-catalog tests/test-catalog.cpp)
    target_link_libraries(test-catalog PRIVATE rse-catalog Qt6::Test)
    add_test(NAME catalog COMMAND test-catalog)

    # Главное окно целиком без дисплея (платформа offscreen)
    add_executable(test-main-window tests/test-main-window.cpp)
    target_link_libraries(test-main-window PRIVATE rse-ui Qt6::Test)
    add_test(NAME main-window COMMAND test-main-window)
    set_tests_properties(main-window PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Панель подвижного состава без дисплея (платформа offscreen)
    add_executable(test-panel tests/test-panel.cpp)
    target_link_libraries(test-panel PRIVATE rse-ui Qt6::Test)
    add_test(NAME panel COMMAND test-panel)
    set_tests_properties(panel PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Проверка отрисовки схемы без дисплея (платформа offscreen)
    add_executable(test-map-render tests/test-map-render.cpp)
    target_link_libraries(test-map-render PRIVATE rse-ui Qt6::Test)
    add_test(NAME map-render COMMAND test-map-render)
    set_tests_properties(map-render PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    # Путь к каталогу реального маршрута для проверки на живых данных
    # (необязательно): -DRRS_ROUTE_DIR=.../routes/experimental-polygon_v2.0
    if(DEFINED RRS_ROUTE_DIR)
        set_tests_properties(route catalog scenario import PROPERTIES
            ENVIRONMENT "RRS_ROUTE_DIR=${RRS_ROUTE_DIR}")
        set_tests_properties(map-render panel main-window PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RRS_ROUTE_DIR=${RRS_ROUTE_DIR}")
    endif()
endif()
