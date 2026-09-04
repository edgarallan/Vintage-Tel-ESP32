idf_component_register(
    SRCS "app_main.c" "diag_input.c"
    INCLUDE_DIRS "."
    REQUIRES core phone_hal esp_timer nvs_flash
)

# Diagnostico dei contatti al posto dell'applicazione: misura i fronti grezzi
# e la durata delle raffiche di rimbalzo, da cui si ricavano gli assestamenti.
# Commentare questa riga per tornare al firmware normale.
# target_compile_definitions(${COMPONENT_LIB} PRIVATE VT_DIAG_INPUT=1)
