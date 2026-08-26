idf_component_register(
    SRCS "app_main.c" "diag_dial.c"
    INCLUDE_DIRS "."
    REQUIRES core phone_hal esp_timer nvs_flash
)

# Diagnostico del disco combinatore al posto dell'applicazione.
# Commentare questa riga per tornare al firmware normale.
# target_compile_definitions(${COMPONENT_LIB} PRIVATE VT_DIAG_DIAL=1)
