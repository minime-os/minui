#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "power.h"
#include "traits.h"

static int readInt(const char *path, int *value)
{
	FILE *file;

	if (!MINIME_traitAvailable(path) || !value)
		return -1;
	file = fopen(path, "r");
	if (!file)
		return -1;
	if (fscanf(file, "%d", value) != 1) {
		fclose(file);
		return -1;
	}
	fclose(file);
	return 0;
}

static void writeInt(const char *path, int value)
{
	FILE *file;

	if (!MINIME_traitAvailable(path))
		return;
	file = fopen(path, "w");
	if (!file)
		return;
	fprintf(file, "%d\n", value);
	fclose(file);
}

static void writeText(const char *path, const char *value)
{
	FILE *file;

	if (!MINIME_traitAvailable(path))
		return;
	file = fopen(path, "w");
	if (!file)
		return;
	fputs(value, file);
	fclose(file);
}

static int readCapacity(const char *root, int *capacity)
{
	char path[MINIME_TRAIT_PATH_MAX + 32];

	snprintf(path, sizeof(path), "%s/capacity", root);
	return readInt(path, capacity);
}

static int readCharging(const MinimeTraits *traits, int *charging)
{
	char path[MINIME_TRAIT_PATH_MAX + 32];
	char status[32];
	FILE *file;

	if (readInt(traits->charger_online_path, charging) == 0)
		return 0;
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

	if (traits)
		(void)readInt(traits->lid_switch_path, &value);
	return value;
}

void MINIME_powerSetLED(int enabled)
{
	const MinimeTraits *traits = MINIME_traits();

	if (traits)
		writeInt(traits->power_led_path, enabled);
}

void MINIME_powerSetRumble(int enabled)
{
	const MinimeTraits *traits = MINIME_traits();

	if (traits)
		writeInt(traits->rumble_path, enabled);
}

void MINIME_powerSetCPUSpeed(int speed)
{
	const MinimeTraits *traits = MINIME_traits();
	const char *governor;

	if (!traits)
		return;
	if (speed <= 1)
		governor = "powersave";
	else if (speed >= 3)
		governor = "performance";
	else
		governor = "schedutil";
	writeText(traits->cpu_governor_path, governor);
}
