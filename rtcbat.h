#pragma once
#include <Arduino.h>

// RTC PCF85063 (or internal ESP32-S3 RTC fallback on 1.75C)
bool rtcBegin();
uint32_t rtcEpoch();             // segundos unix; 0 si el RTC no es valido
void rtcSetEpoch(uint32_t e);

// PMU AXP2101: estado de la bateria
bool batBegin();
void pmuEnablePanel();           // enables 3.3V rails (DC1 + ALDO1 on 1.75C); call before gfx->begin()
int batPercent();                // 0-100, -1 si no hay bateria conectada
bool batCharging();
bool usbPresent();

// boton PWR del AXP2101: pulsacion larga 4s = apagado fisico (RTC sigue vivo);
// la pulsacion corta la captura el firmware (pantalla on/off)
void pwrSetup();
bool pwrShortPressed();  // sondear en el loop
