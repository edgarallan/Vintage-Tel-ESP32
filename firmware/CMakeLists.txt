# Progetto ESP-IDF del telefono. Il banco di prova nativo sotto tests/ resta
# indipendente da qui: si compila con il proprio cmake e non richiede ESP-IDF.
#
#   idf.py set-target esp32
#   idf.py build flash monitor
cmake_minimum_required(VERSION 3.16)

# core/ e hal/ sono componenti veri e propri, ma stanno fuori da components/
# per non spostarli: tests/CMakeLists.txt li referenzia come ../core/ e la
# struttura e' documentata in CLAUDE.md. ESP-IDF accetta un percorso che punti
# direttamente a una cartella con CMakeLists.txt (project.cmake, __project_component_dir).
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/core"
    "${CMAKE_CURRENT_LIST_DIR}/phone_hal"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vintage_tel)
