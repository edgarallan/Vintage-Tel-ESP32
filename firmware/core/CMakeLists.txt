# core/ come componente ESP-IDF. Nessun REQUIRES: questa logica non dipende
# da alcun componente di ESP-IDF, ed e' esattamente cio' che la rende
# compilabile anche dal banco di prova nativo sotto tests/.
idf_component_register(
    SRCS "phone_fsm.c" "dial_decode.c" "phonebook.c" "tones.c" "ring_pattern.c"
    INCLUDE_DIRS "."
)
