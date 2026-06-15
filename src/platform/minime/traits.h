#ifndef MINIME_TRAITS_H
#define MINIME_TRAITS_H

#define MINIME_TRAIT_PATH_MAX 256
#define MINIME_TRAIT_NAME_MAX 64

typedef struct MinimeTraits {
	char device_id[MINIME_TRAIT_NAME_MAX];
	char device_model[MINIME_TRAIT_PATH_MAX];
	char video_device[MINIME_TRAIT_PATH_MAX];
	int screen_width;
	int screen_height;
	int screen_rotation;
	char backlight_path[MINIME_TRAIT_PATH_MAX];
	char framebuffer_blank_path[MINIME_TRAIT_PATH_MAX];
	char hdmi_state_path[MINIME_TRAIT_PATH_MAX];
	char battery_capacity_path[MINIME_TRAIT_PATH_MAX];
	char charger_online_path[MINIME_TRAIT_PATH_MAX];
	char lid_switch_path[MINIME_TRAIT_PATH_MAX];
	char rumble_path[MINIME_TRAIT_PATH_MAX];
	char power_led_path[MINIME_TRAIT_PATH_MAX];
	char cpu_governor_path[MINIME_TRAIT_PATH_MAX];
	char sound_card[MINIME_TRAIT_NAME_MAX];
	char sound_mixer[MINIME_TRAIT_NAME_MAX];
	char jack_state_path[MINIME_TRAIT_PATH_MAX];
	char wifi_interface[MINIME_TRAIT_NAME_MAX];
	char bluetooth_adapter[MINIME_TRAIT_NAME_MAX];
	char input_gamepad[MINIME_TRAIT_NAME_MAX];
	char input_power[MINIME_TRAIT_NAME_MAX];
	char input_volume[MINIME_TRAIT_NAME_MAX];
	int key_up;
	int key_down;
	int key_left;
	int key_right;
	int key_a;
	int key_b;
	int key_c;
	int key_x;
	int key_y;
	int key_z;
	int key_l1;
	int key_r1;
	int key_l2;
	int key_r2;
	int key_l3;
	int key_r3;
	int key_start;
	int key_select;
	int key_menu;
	int key_power;
	int key_vol_up;
	int key_vol_down;
	int axis_lx;
	int axis_ly;
	int axis_rx;
	int axis_ry;
	int axis_min;
	int axis_center;
	int axis_max;
	int axis_lx_invert;
	int axis_ly_invert;
	int axis_rx_invert;
	int axis_ry_invert;
} MinimeTraits;

int MINIME_traitsInit(void);
const MinimeTraits *MINIME_traits(void);
int MINIME_traitAvailable(const char *value);

#endif
