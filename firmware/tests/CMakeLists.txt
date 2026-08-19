# Banco di prova nativo: gira sul PC dello sviluppatore, senza ESP-IDF e senza
# ESP32 collegato. E' il motivo per cui core/ non include mai esp_*.h.
#
#   cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure

cmake_minimum_required(VERSION 3.16)
project(vintage_tel_core_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Werror)

option(COVERAGE "Compila con strumentazione per gcov" OFF)
if(COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC unity)
# Unity e' codice di terze parti: non applichiamo -Werror ai suoi warning.
target_compile_options(unity PRIVATE -w)

enable_testing()

# Ogni test e' un eseguibile a se': un crash isola il modulo colpevole
# invece di portarsi giu' tutta la suite.
function(add_core_test name)
    add_executable(${name} ${name}.c ${ARGN})
    target_link_libraries(${name} PRIVATE unity)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../core)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_core_test(test_dial_decode ../core/dial_decode.c)
add_core_test(test_phonebook ../core/phonebook.c)
add_core_test(test_ring_pattern ../core/ring_pattern.c)
add_core_test(test_tones ../core/tones.c)
add_core_test(test_phone_fsm ../core/phone_fsm.c ../core/phonebook.c ../core/ring_pattern.c fake_hw.c)
