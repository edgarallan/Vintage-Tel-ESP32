# NOTA SUL NOME: questo componente NON puo' chiamarsi "hal".
# ESP-IDF ha un proprio componente "hal", ed e' nei common_component_reqs:
# ogni altro componente ne dipende. Una cartella chiamata hal/ nel progetto
# lo sostituisce silenziosamente, e la build muore altrove con un oscuro
# "hal/efuse_ll.h: No such file or directory" dentro esp_https_ota.
idf_component_register(
    SRCS "hal.c" "hal_input.c" "hal_bell.c" "hal_out.c" "hal_bt.c"
    INCLUDE_DIRS "."
    REQUIRES core esp_driver_gpio esp_driver_pcnt esp_timer
)
