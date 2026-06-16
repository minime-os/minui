#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/stat.h>
#include <string.h>

#include "msettings.h"
#include "audio.h"
#include "video.h"

///////////////////////////////////////

#define SETTINGS_VERSION 2
typedef struct Settings {
	int version; // future proofing
	int brightness;
	int headphones;
	int speaker;
	int unused[2]; // for future use
	// NOTE: doesn't really need to be persisted but still needs to be shared
	int jack; 
	int hdmi; 
} Settings;
static Settings DefaultSettings = {
	.version = SETTINGS_VERSION,
	.brightness = 2,
	.headphones = 4,
	.speaker = 8,
	.jack = 0,
	.hdmi = 0,
};
static Settings* settings;

#define SHM_KEY "/SharedSettings"
static char SettingsPath[256];
static int shm_fd = -1;
static int is_host = 0;
static int shm_size = sizeof(Settings);

void InitSettings(void) {	
	sprintf(SettingsPath, "%s/msettings.bin", getenv("USERDATA_PATH"));
	
	shm_fd = shm_open(SHM_KEY, O_RDWR | O_CREAT | O_EXCL, 0644); // see if it exists
	if (shm_fd==-1 && errno==EEXIST) { // already exists
		shm_fd = shm_open(SHM_KEY, O_RDWR, 0644);
		
		// Wait for host to ftruncate the shared memory to prevent mapping a 0-size SHM
		struct stat st;
		int retry = 0;
		while (retry < 10000) { // up to 10 seconds under heavy boot load
			if (fstat(shm_fd, &st) == 0 && st.st_size >= shm_size) {
				break;
			}
			usleep(1000); // sleep 1ms
			retry++;
		}
		
		settings = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
	}
	else { // host
		is_host = 1;
		// we created it so set initial size and populate
		ftruncate(shm_fd, shm_size);
		settings = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
		
		int fd = open(SettingsPath, O_RDONLY);
		if (fd>=0) {
			read(fd, settings, shm_size);
			// TODO: use settings->version for future proofing?
			close(fd);
		}
		else {
			// load defaults
			memcpy(settings, &DefaultSettings, shm_size);
		}
		
	}
	
	// both of these set volume
	SetJack(MINIME_audioJackConnected());
	SetHDMI(MINIME_videoHDMIConnected());
	
	SetBrightness(GetBrightness());
}
void QuitSettings(void) {
	munmap(settings, shm_size);
	if (is_host) shm_unlink(SHM_KEY);
}
static inline void SaveSettings(void) {
	int fd = open(SettingsPath, O_CREAT|O_WRONLY, 0644);
	if (fd>=0) {
		write(fd, settings, shm_size);
		close(fd);
		sync();
	}
}

int GetBrightness(void) { // 0-10
	return settings->brightness;
}
void SetBrightness(int value) {
	if (settings->hdmi) return;
	
	int raw;
	switch (value) {
		case  0: raw=  4; break;	//  0
		case  1: raw=  6; break;	//  2
		case  2: raw= 10; break;	//  4
		case  3: raw= 16; break;	//  6
		case  4: raw= 32; break;	// 16
		case  5: raw= 48; break;	// 16
		case  6: raw= 64; break;	// 16
		case  7: raw= 96; break;	// 32
		case  8: raw=128; break;	// 32
		case  9: raw=192; break;	// 64
		case 10: raw=255; break;	// 64
	}
	
	SetRawBrightness(raw);
	settings->brightness = value;
	SaveSettings();
}

int GetVolume(void) { // 0-20
	return settings->jack ? settings->headphones : settings->speaker;
}
void SetVolume(int value) {
	if (settings->hdmi) return;
	
	if (settings->jack) settings->headphones = value;
	else settings->speaker = value;
	
	int raw = value * 5;
	SetRawVolume(raw);
	SaveSettings();
}

void SetRawBrightness(int val) { // 0 - 255
	if (settings->hdmi) return;
	MINIME_videoSetBacklight(val);
}

void SetRawVolume(int val) { // 0 - 100
	MINIME_audioSetRawVolume(val);
}

// monitored and set by thread in keymon
int GetJack(void) {
	return settings->jack;
}
void SetJack(int value) {
	settings->jack = value;
	SetVolume(GetVolume());
}

int GetHDMI(void) {
	return settings->hdmi;
}
void SetHDMI(int value) {
	settings->hdmi = value;
	if (value) SetRawVolume(100); // max
	else SetVolume(GetVolume()); // restore
}
