#pragma once

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
private:

    lgfx::Panel_ST7789  _panel;
    // lgfx::Panel_ILI9341 _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;
    lgfx::Touch_XPT2046     _touch;
public:

    LGFX();
};

extern LGFX lcd;
