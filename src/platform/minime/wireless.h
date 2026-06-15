#ifndef MINIME_WIRELESS_H
#define MINIME_WIRELESS_H

#include "settings.h"

int MINIME_wirelessHasWifi(void);
int MINIME_wirelessHasBluetooth(void);

int MINIME_wirelessWifiInit(void);
int MINIME_wirelessWifiRefresh(struct settings_snapshot *snapshot);
int MINIME_wirelessWifiSetEnabled(int enabled);
int MINIME_wirelessWifiSetScanning(int enabled);
int MINIME_wirelessWifiConnect(const char *ssid, const char *passphrase,
	int hidden);
int MINIME_wirelessWifiDisconnect(void);
int MINIME_wirelessWifiForget(const char *ssid);

int MINIME_wirelessBluetoothInit(void);
void MINIME_wirelessBluetoothQuit(void);
int MINIME_wirelessBluetoothRefresh(struct settings_snapshot *snapshot);
int MINIME_wirelessBluetoothSetEnabled(int enabled);
int MINIME_wirelessBluetoothSetScanning(int enabled);
int MINIME_wirelessBluetoothToggleDevice(const char *addr);
int MINIME_wirelessBluetoothForgetDevice(const char *addr);
int MINIME_wirelessBluetoothConfirmDevice(const char *addr, int accept);

#endif
