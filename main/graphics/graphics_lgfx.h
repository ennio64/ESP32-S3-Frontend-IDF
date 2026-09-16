#ifndef GRAPHICS_LGFX_H
#define GRAPHICS_LGFX_H

#include <LovyanGFX.hpp>

class LGFX_ILI9488 : public lgfx::LGFX_Device
{
    lgfx::Panel_ILI9488 _panel_instance;
    lgfx::Bus_SPI _bus_instance;

public:
    LGFX_ILI9488();
};

// Ottieni l'istanza globale
LGFX_ILI9488& get_tft();

// Inizializza il display
void init_tft();
void tft_invert_display(bool invert);

#endif