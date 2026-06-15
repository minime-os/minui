#ifndef MINIME_POWER_H
#define MINIME_POWER_H

int MINIME_powerGetBattery(int *charging, int *capacity);
int MINIME_powerReadLid(void);
void MINIME_powerSetLED(int enabled);
void MINIME_powerSetRumble(int enabled);
void MINIME_powerSetCPUSpeed(int speed);

#endif
