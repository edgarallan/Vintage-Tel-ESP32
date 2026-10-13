#include "alimentazione.h"

uint8_t alim_serie(uint8_t serie, bool calo_di_tensione)
{
    if (!calo_di_tensione) {
        return 0;
    }
    return serie < UINT8_MAX ? (uint8_t)(serie + 1) : UINT8_MAX;
}

bool alim_deve_dormire(uint8_t serie)
{
    return serie >= ALIM_SERIE_MAX;
}
