#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "traits.h"
#include "utils.h"

#define TRAITS_PATH "/mnt/sdcard/.minime/traits"
#define NA "na"

static MinimeTraits traits;
static int initialized;
static int valid;

static void copyText(char *dst, size_t size, const char *src)
{
	if (!dst || !size)
		return;
	snprintf(dst, size, "%s", src ? src : "");
}


static int parseInt(const char *value)
{
	char *end;
	long parsed;

	if (!value || !strcmp(value, NA))
		return -1;
	parsed = strtol(value, &end, 10);
	return *end ? -1 : (int)parsed;
}

static void setValue(const char *key, const char *value)
{
#define STRING_TRAIT(name) \
	if (!strcmp(key, #name)) copyText(traits.name, sizeof(traits.name), value)
#define INT_TRAIT(name) \
	if (!strcmp(key, #name)) traits.name = parseInt(value)

	     STRING_TRAIT(device_id);
	else STRING_TRAIT(device_model);
	else STRING_TRAIT(video_device);
	else INT_TRAIT(screen_width);
	else INT_TRAIT(screen_height);
	else INT_TRAIT(screen_rotation);
	else STRING_TRAIT(backlight_path);
	else STRING_TRAIT(framebuffer_blank_path);
	else STRING_TRAIT(hdmi_state_path);
	else STRING_TRAIT(battery_capacity_path);
	else STRING_TRAIT(charger_online_path);
	else STRING_TRAIT(lid_switch_path);
	else STRING_TRAIT(rumble_path);
	else STRING_TRAIT(power_led_path);
	else STRING_TRAIT(cpu_governor_path);
	else STRING_TRAIT(sound_card);
	else STRING_TRAIT(sound_mixer);
	else STRING_TRAIT(jack_state_path);
	else STRING_TRAIT(wifi_interface);
	else STRING_TRAIT(bluetooth_adapter);
	else if (!strcmp(key, "input_gamepad_device_name"))
		copyText(traits.input_gamepad, sizeof(traits.input_gamepad), value);
	else if (!strcmp(key, "input_power_device_name"))
		copyText(traits.input_power, sizeof(traits.input_power), value);
	else if (!strcmp(key, "input_volume_device_name"))
		copyText(traits.input_volume, sizeof(traits.input_volume), value);
	else INT_TRAIT(key_up);
	else INT_TRAIT(key_down);
	else INT_TRAIT(key_left);
	else INT_TRAIT(key_right);
	else INT_TRAIT(key_a);
	else INT_TRAIT(key_b);
	else INT_TRAIT(key_c);
	else INT_TRAIT(key_x);
	else INT_TRAIT(key_y);
	else INT_TRAIT(key_z);
	else INT_TRAIT(key_l1);
	else INT_TRAIT(key_r1);
	else INT_TRAIT(key_l2);
	else INT_TRAIT(key_r2);
	else INT_TRAIT(key_l3);
	else INT_TRAIT(key_r3);
	else INT_TRAIT(key_start);
	else INT_TRAIT(key_select);
	else INT_TRAIT(key_menu);
	else INT_TRAIT(key_power);
	else INT_TRAIT(key_vol_up);
	else INT_TRAIT(key_vol_down);
	else INT_TRAIT(axis_lx);
	else INT_TRAIT(axis_ly);
	else INT_TRAIT(axis_rx);
	else INT_TRAIT(axis_ry);
	else INT_TRAIT(axis_min);
	else INT_TRAIT(axis_center);
	else INT_TRAIT(axis_max);
	else INT_TRAIT(axis_lx_invert);
	else INT_TRAIT(axis_ly_invert);
	else INT_TRAIT(axis_rx_invert);
	else INT_TRAIT(axis_ry_invert);
	else INT_TRAIT(undervolt_supported);

#undef STRING_TRAIT
#undef INT_TRAIT
}

int MINIME_traitAvailable(const char *value)
{
	return value && value[0] && strcmp(value, NA);
}

static int validate(void)
{
	if (!traits.device_id[0] || !traits.device_model[0] ||
		!MINIME_traitAvailable(traits.video_device) ||
		traits.screen_width <= 0 || traits.screen_height <= 0 ||
		traits.screen_rotation < 0 ||
		!MINIME_traitAvailable(traits.backlight_path) ||
		!MINIME_traitAvailable(traits.input_gamepad) ||
		!MINIME_traitAvailable(traits.input_power) ||
		!MINIME_traitAvailable(traits.input_volume) ||
		traits.key_up < 0 || traits.key_down < 0 ||
		traits.key_left < 0 || traits.key_right < 0 ||
		traits.key_a < 0 || traits.key_b < 0 ||
		traits.key_x < 0 || traits.key_y < 0 ||
		traits.key_start < 0 || traits.key_select < 0 ||
		traits.key_menu < 0 || traits.key_power < 0 ||
		traits.key_vol_up < 0 || traits.key_vol_down < 0) {
		fprintf(stderr, "Invalid required Minime traits in %s\n", TRAITS_PATH);
		return -1;
	}
	return 0;
}

int MINIME_traitsInit(void)
{
	FILE *file;
	char line[512];

	if (initialized)
		return valid ? 0 : -1;
	initialized = 1;
	memset(&traits, 0, sizeof(traits));
	traits.key_c = traits.key_z = -1;
	traits.key_l1 = traits.key_r1 = -1;
	traits.key_l2 = traits.key_r2 = -1;
	traits.key_l3 = traits.key_r3 = -1;
	traits.axis_lx = traits.axis_ly = -1;
	traits.axis_rx = traits.axis_ry = -1;
	traits.axis_min = traits.axis_center = traits.axis_max = -1;

	file = fopen(TRAITS_PATH, "r");
	if (!file) {
		fprintf(stderr, "Missing Minime traits: %s\n", TRAITS_PATH);
		return -1;
	}
	while (fgets(line, sizeof(line), file)) {
		char *key;
		char *value;

		key = trim(line);
		if (!key[0] || key[0] == '#' || key[0] == '[')
			continue;
		value = strchr(key, '=');
		if (!value)
			continue;
		*value++ = '\0';
		setValue(trim(key), trim(value));
	}
	fclose(file);
	valid = validate() == 0;
	return valid ? 0 : -1;
}

const MinimeTraits *MINIME_traits(void)
{
	return MINIME_traitsInit() == 0 ? &traits : NULL;
}
