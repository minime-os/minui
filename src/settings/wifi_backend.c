#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "wifi_backend.h"

#define WIFI_CONFIG_PATH "/mnt/sdcard/.minime/config/wifi.cfg"

static int wifi_enabled = 0;
static int wifi_scanning = 0;
static char connected_ssid[64] = {0};

struct wifi_network {
	char ssid[64];
	int secure;
	int known;
	int signal;
	int connected;
};

#define MAX_NETWORKS 16
static struct wifi_network scanned_networks[MAX_NETWORKS];
static int scanned_count = 0;

static int is_wifi_interface_present(void) {
	return access("/sys/class/net/wlan0", F_OK) == 0;
}

static int is_wifi_interface_up(void) {
	FILE* f = fopen("/sys/class/net/wlan0/operstate", "r");
	if (!f) return 0;
	char state[16] = {0};
	if (fgets(state, sizeof(state), f)) {
		fclose(f);
		if (strncmp(state, "up", 2) == 0) {
			return 1;
		}
	}
	fclose(f);
	return 0;
}

static void get_connected_ssid(char* ssid_out, size_t max_len) {
	ssid_out[0] = '\0';
	FILE* f = popen("wpa_cli -i wlan0 status 2>/dev/null", "r");
	if (!f) return;
	char line[128];
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "ssid=", 5) == 0) {
			char* ssid = line + 5;
			size_t len = strlen(ssid);
			while (len > 0 && (ssid[len-1] == '\n' || ssid[len-1] == '\r')) {
				ssid[len-1] = '\0';
				len--;
			}
			strncpy(ssid_out, ssid, max_len);
			break;
		}
	}
	pclose(f);
}

static int signal_to_percent(int dbm) {
	if (dbm >= -45) return 100;
	if (dbm <= -90) return 0;
	return (dbm + 90) * 100 / 45;
}

static int is_ssid_known(const char* ssid) {
	FILE* f = fopen(WIFI_CONFIG_PATH, "r");
	if (!f) return 0;
	char line[128];
	int known = 0;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "SSID=", 5) == 0) {
			char* val = line + 5;
			size_t len = strlen(val);
			while (len > 0 && (val[len-1] == '\n' || val[len-1] == '\r')) {
				val[len-1] = '\0';
				len--;
			}
			if (strcmp(val, ssid) == 0) {
				known = 1;
				break;
			}
		}
	}
	fclose(f);
	return known;
}

static void remove_network_from_config(const char* ssid) {
	FILE* f = fopen(WIFI_CONFIG_PATH, "r");
	if (!f) return;
	
	FILE* out = fopen(WIFI_CONFIG_PATH ".tmp", "w");
	if (!out) {
		fclose(f);
		return;
	}
	
	char line[256];
	char current_ssid[128] = {0};
	char current_pass[128] = {0};
	
	while (fgets(line, sizeof(line), f)) {
		char* key = line;
		char* val = strchr(line, '=');
		if (!val) {
			fprintf(out, "%s", line);
			continue;
		}
		*val = '\0';
		val++;
		
		size_t len = strlen(val);
		while (len > 0 && (val[len-1] == '\n' || val[len-1] == '\r')) {
			val[len-1] = '\0';
			len--;
		}
		
		if (strcmp(key, "SSID") == 0) {
			if (current_ssid[0] != '\0') {
				if (strcmp(current_ssid, ssid) != 0) {
					fprintf(out, "SSID=%s\n", current_ssid);
					if (current_pass[0] != '\0') {
						fprintf(out, "Passphrase=%s\n", current_pass);
					}
					fprintf(out, "\n");
				}
				current_pass[0] = '\0';
			}
			strncpy(current_ssid, val, sizeof(current_ssid));
		} else if (strcmp(key, "Passphrase") == 0) {
			strncpy(current_pass, val, sizeof(current_pass));
		} else {
			*(val - 1) = '=';
			fprintf(out, "%s=%s\n", key, val);
		}
	}
	
	if (current_ssid[0] != '\0' && strcmp(current_ssid, ssid) != 0) {
		fprintf(out, "SSID=%s\n", current_ssid);
		if (current_pass[0] != '\0') {
			fprintf(out, "Passphrase=%s\n", current_pass);
		}
	}
	
	fclose(f);
	fclose(out);
	rename(WIFI_CONFIG_PATH ".tmp", WIFI_CONFIG_PATH);
}

static void add_network_to_config(const char* ssid, const char* passphrase) {
	remove_network_from_config(ssid);
	
	FILE* f = fopen(WIFI_CONFIG_PATH, "a");
	if (!f) return;
	fprintf(f, "\nSSID=%s\nPassphrase=%s\n", ssid, passphrase);
	fclose(f);
}

static void parse_scan_results(void) {
	scanned_count = 0;
	FILE* f = popen("wpa_cli -i wlan0 scan_results 2>/dev/null", "r");
	if (!f) return;
	char line[256];
	
	if (!fgets(line, sizeof(line), f)) {
		pclose(f);
		return;
	}
	
	while (fgets(line, sizeof(line), f) && scanned_count < MAX_NETWORKS) {
		char bssid[32];
		int freq;
		int sig;
		char flags[128];
		char ssid[128];
		
		char* p = line;
		
		char* b = strchr(p, '\t');
		if (!b) b = strchr(p, ' ');
		if (!b) continue;
		*b = '\0';
		strncpy(bssid, p, sizeof(bssid));
		p = b + 1;
		while (*p == '\t' || *p == ' ') p++;
		
		char* fr = strchr(p, '\t');
		if (!fr) fr = strchr(p, ' ');
		if (!fr) continue;
		*fr = '\0';
		freq = atoi(p);
		p = fr + 1;
		while (*p == '\t' || *p == ' ') p++;
		
		char* s = strchr(p, '\t');
		if (!s) s = strchr(p, ' ');
		if (!s) continue;
		*s = '\0';
		sig = atoi(p);
		p = s + 1;
		while (*p == '\t' || *p == ' ') p++;
		
		char* fl = strchr(p, '\t');
		if (!fl) fl = strchr(p, ' ');
		if (!fl) continue;
		*fl = '\0';
		strncpy(flags, p, sizeof(flags));
		p = fl + 1;
		while (*p == '\t' || *p == ' ') p++;
		
		size_t len = strlen(p);
		while (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r')) {
			p[len-1] = '\0';
			len--;
		}
		strncpy(ssid, p, sizeof(ssid));
		
		if (ssid[0] == '\0') continue;
		
		int exists = 0;
		for (int i=0; i<scanned_count; i++) {
			if (strcmp(scanned_networks[i].ssid, ssid) == 0) {
				exists = 1;
				if (sig > scanned_networks[i].signal) {
					scanned_networks[i].signal = sig;
				}
				break;
			}
		}
		if (exists) continue;
		
		struct wifi_network* net = &scanned_networks[scanned_count++];
		strncpy(net->ssid, ssid, sizeof(net->ssid));
		net->signal = sig;
		net->secure = (strstr(flags, "WPA") != NULL || strstr(flags, "WEP") != NULL);
		net->known = 0;
		net->connected = 0;
	}
	pclose(f);
}

///////////////////////////////////////

int SETTINGS_WIFI_BACKEND_init(void) {
	wifi_enabled = is_wifi_interface_present() && is_wifi_interface_up();
	wifi_scanning = 0;
	connected_ssid[0] = '\0';
	scanned_count = 0;
	return 0;
}

void SETTINGS_WIFI_BACKEND_quit(void) {
	// No-op
}

int SETTINGS_WIFI_BACKEND_refresh(struct settings_snapshot *snapshot) {
	if (!snapshot) return -1;
	
	wifi_enabled = is_wifi_interface_present() && is_wifi_interface_up();
	snapshot->wifi_enabled = wifi_enabled;
	snapshot->wifi_busy = 0; // S45wifi triggers quickly
	snapshot->wifi_scanning = wifi_scanning;
	
	if (wifi_enabled) {
		get_connected_ssid(connected_ssid, sizeof(connected_ssid));
		strncpy(snapshot->wifi_connected_ssid, connected_ssid, sizeof(snapshot->wifi_connected_ssid));
		
		parse_scan_results();
		
		snapshot->wifi_network_count = scanned_count;
		for (int i = 0; i < scanned_count; i++) {
			struct settings_wifi_network* dst = &snapshot->wifi_networks[i];
			struct wifi_network* src = &scanned_networks[i];
			
			strncpy(dst->ssid, src->ssid, sizeof(dst->ssid));
			dst->secure = src->secure;
			dst->known = is_ssid_known(src->ssid);
			dst->signal = signal_to_percent(src->signal);
			dst->connected = (connected_ssid[0] != '\0' && strcmp(connected_ssid, src->ssid) == 0);
			
			strncpy(dst->security, src->secure ? "WPA2" : "Open", sizeof(dst->security));
			strncpy(dst->state, dst->connected ? "Connected" : "", sizeof(dst->state));
		}
	} else {
		snapshot->wifi_connected_ssid[0] = '\0';
		snapshot->wifi_network_count = 0;
	}
	
	return 0;
}

int SETTINGS_WIFI_BACKEND_set_enabled(int enabled) {
	if (enabled) {
		system("/etc/init.d/S45wifi start");
		wifi_enabled = 1;
	} else {
		system("/etc/init.d/S45wifi stop");
		wifi_enabled = 0;
	}
	return 0;
}

int SETTINGS_WIFI_BACKEND_set_scanning(int enabled) {
	if (enabled && wifi_enabled) {
		system("wpa_cli -i wlan0 scan >/dev/null 2>&1");
		wifi_scanning = 1;
	} else {
		wifi_scanning = 0;
	}
	return 0;
}

int SETTINGS_WIFI_BACKEND_connect(const char *ssid, const char *passphrase, int hidden) {
	(void)hidden;
	if (!ssid) return -1;
	
	if (passphrase && passphrase[0] != '\0') {
		add_network_to_config(ssid, passphrase);
	}
	
	// Trigger wpa_supplicant configuration reload / connection attempt
	system("/etc/init.d/S45wifi restart");
	return 0;
}

int SETTINGS_WIFI_BACKEND_disconnect(void) {
	system("wpa_cli -i wlan0 disconnect >/dev/null 2>&1");
	connected_ssid[0] = '\0';
	return 0;
}

int SETTINGS_WIFI_BACKEND_forget(const char *ssid) {
	if (!ssid) return -1;
	remove_network_from_config(ssid);
	system("/etc/init.d/S45wifi restart");
	return 0;
}
