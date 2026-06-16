#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "power.h"
#include "traits.h"
#include "utils.h"

static int readCapacity(const char *root, int *capacity)
{
	char path[MINIME_TRAIT_PATH_MAX + 32];

	snprintf(path, sizeof(path), "%s/capacity", root);
	if (!MINIME_traitAvailable(path) || !capacity)
		return -1;
	*capacity = getInt(path);
	return 0;
}

static int readCharging(const MinimeTraits *traits, int *charging)
{
	char path[MINIME_TRAIT_PATH_MAX + 32];
	char status[32];
	FILE *file;

	if (MINIME_traitAvailable(traits->charger_online_path)) {
		*charging = getInt((char*)traits->charger_online_path);
		return 0;
	}
	if (!MINIME_traitAvailable(traits->battery_capacity_path))
		return -1;
	snprintf(path, sizeof(path), "%s/status", traits->battery_capacity_path);
	file = fopen(path, "r");
	if (!file)
		return -1;
	status[0] = '\0';
	(void)fgets(status, sizeof(status), file);
	fclose(file);
	*charging = !strncmp(status, "Charging", 8) || !strncmp(status, "Full", 4);
	return 0;
}

int MINIME_powerGetBattery(int *charging, int *capacity)
{
	const MinimeTraits *traits = MINIME_traits();

	if (!traits || !charging || !capacity ||
		!MINIME_traitAvailable(traits->battery_capacity_path))
		return -1;
	if (readCapacity(traits->battery_capacity_path, capacity) != 0)
		return -1;
	if (readCharging(traits, charging) != 0)
		*charging = 0;
	return 0;
}

int MINIME_powerReadLid(void)
{
	const MinimeTraits *traits = MINIME_traits();
	int value = 1;

	if (traits && MINIME_traitAvailable(traits->lid_switch_path))
		value = getInt((char*)traits->lid_switch_path);
	return value;
}

void MINIME_powerSetLED(int enabled)
{
	const MinimeTraits *traits = MINIME_traits();

	if (traits && MINIME_traitAvailable(traits->power_led_path))
		putInt((char*)traits->power_led_path, enabled);
}

void MINIME_powerSetRumble(int enabled)
{
	const MinimeTraits *traits = MINIME_traits();

	if (traits && MINIME_traitAvailable(traits->rumble_path))
		putInt((char*)traits->rumble_path, enabled);
}

void MINIME_powerSetCPUSpeed(int speed)
{
	const MinimeTraits *traits = MINIME_traits();
	const char *governor;

	if (!traits || !MINIME_traitAvailable(traits->cpu_governor_path))
		return;
	if (speed <= 1)
		governor = "powersave";
	else if (speed >= 3)
		governor = "performance";
	else
		governor = "schedutil";
	putFile((char*)traits->cpu_governor_path, (char*)governor);
}
