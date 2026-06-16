#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <linux/input.h>
#include <pthread.h>
#include <time.h>

#include <msettings.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "input.h"
#include "traits.h"
#include "video.h"

#define VOLUME_MIN 		0
#define VOLUME_MAX 		20
#define BRIGHTNESS_MIN 	0
#define BRIGHTNESS_MAX 	10

//	for ev.value
#define RELEASED	0
#define PRESSED		1
#define REPEAT		2

static int input_fds[4] = {0};
static int input_count = 0;

static uint32_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static pthread_t hdmi_pt;

static void* watchHDMI(void *arg) {
	int has_hdmi,had_hdmi;
	
	has_hdmi = had_hdmi = MINIME_videoHDMIConnected();
	SetHDMI(has_hdmi);
	
	while(1) {
		sleep(1);
		
		has_hdmi = MINIME_videoHDMIConnected();
		if (had_hdmi!=has_hdmi) {
			had_hdmi = has_hdmi;
			SetHDMI(has_hdmi);
		}
	}
	
	return 0;
}

int main (int argc, char *argv[]) {
	const MinimeTraits *traits;

	(void)argc;
	(void)argv;
	if (MINIME_traitsInit() != 0)
		return 1;
	traits = MINIME_traits();
	InitSettings();
	pthread_create(&hdmi_pt, NULL, &watchHDMI, NULL);
	input_count = MINIME_inputOpenShortcutDevices(input_fds,
		sizeof(input_fds) / sizeof(input_fds[0]));

	uint32_t val;
	uint32_t menu_pressed = 0;
	
	uint32_t up_pressed = 0;
	uint32_t up_just_pressed = 0;
	uint32_t up_repeat_at = 0;
	
	uint32_t down_pressed = 0;
	uint32_t down_just_pressed = 0;
	uint32_t down_repeat_at = 0;
	
	uint8_t ignore;
	uint32_t then;
	uint32_t now;
	
	then = now_ms(); // essential SDL_GetTicks()
	ignore = 0;
	
	while (1) {
		now = now_ms();
		// TODO: check if if necessary
		if (now-then>1000) ignore = 1; // ignore input that arrived during sleep
		
		struct input_event ev;
		for (int i=0; i<input_count; i++) {
			if (input_fds[i]<=0) continue;
			while(read(input_fds[i], &ev, sizeof(ev))==sizeof(ev)) {
				if (ignore) continue;
				val = ev.value;

				if (( ev.type != EV_KEY ) || ( val > REPEAT )) continue;
				if (ev.code == traits->key_menu) {
					menu_pressed = val;
				} else if (ev.code == traits->key_vol_up) {
					up_pressed = up_just_pressed = val;
					if (val) up_repeat_at = now + 300;
				} else if (ev.code == traits->key_vol_down) {
					down_pressed = down_just_pressed = val;
					if (val) down_repeat_at = now + 300;
				}
			}
		}
		
		if (ignore) {
			menu_pressed = 0;
			up_pressed = up_just_pressed = 0;
			down_pressed = down_just_pressed = 0;
			up_repeat_at = 0;
			down_repeat_at = 0;
		}
		
		if (up_just_pressed || (up_pressed && now>=up_repeat_at)) {
			if (menu_pressed) {
				val = GetBrightness();
				if (val<BRIGHTNESS_MAX) SetBrightness(++val);
			}
			else {
				val = GetVolume();
				if (val<VOLUME_MAX) SetVolume(++val);
			}
			
			if (up_just_pressed) up_just_pressed = 0;
			else up_repeat_at += 100;
		}
		
		if (down_just_pressed || (down_pressed && now>=down_repeat_at)) {
			if (menu_pressed) {
				val = GetBrightness();
				if (val>BRIGHTNESS_MIN) SetBrightness(--val);
			}
			else {
				val = GetVolume();
				if (val>VOLUME_MIN) SetVolume(--val);
			}
			
			if (down_just_pressed) down_just_pressed = 0;
			else down_repeat_at += 100;
		}
		
		then = now;
		ignore = 0;
		
		usleep(16666); // 60fps
	}
}
