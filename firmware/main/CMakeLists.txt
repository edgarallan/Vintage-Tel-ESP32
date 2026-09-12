idf_component_register(
    SRCS "app_main.c" "diag_input.c" "diag_bell.c" "diag_audio.c"
    INCLUDE_DIRS "."
    REQUIRES core phone_hal esp_timer nvs_flash
)

# Diagnostico dei contatti al posto dell'applicazione: misura i fronti grezzi
# e la durata delle raffiche di rimbalzo, da cui si ricavano gli assestamenti.
# Commentare questa riga per tornare al firmware normale.
# target_compile_definitions(${COMPONENT_LIB} PRIVATE VT_DIAG_INPUT=1)

# Taratura del campanello: spazza da 16 a 28 Hz per trovare la risonanza del
# martelletto. Scommentare, provare, poi rimettere il commento.
# target_compile_definitions(${COMPONENT_LIB} PRIVATE VT_DIAG_BELL=1)

# Compila senza Bluetooth. Il telefono resta senza linea, ma disco, gancio,
# campanello, LED, display e codec restano collaudabili. Scommentare quando
# l'alimentazione non regge il picco della radio e il chip cicla sul brownout.
# idf_build_set_property(COMPILE_OPTIONS "-DVT_NO_BT=1" APPEND)

# Prova della catena audio: suona il tono di libero sul jack cuffia della
# scheda WM8960. Scommentare, provare, poi rimettere il commento.
# target_compile_definitions(${COMPONENT_LIB} PRIVATE VT_DIAG_AUDIO=1)
