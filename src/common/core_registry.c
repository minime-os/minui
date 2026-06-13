#define _GNU_SOURCE
/*
 * Minime core registry: parses .minime/config/cores.cfg and provides core
 * resolution for the UI and minarch.  This replaces Simon's original registry
 * that read systems.cfg + core-*.cfg manifests.
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "defines.h"
#include "core_registry.h"
#include "utils.h"

#define CORE_REGISTRY_CFG "/.minime/config/cores.cfg"
#define CORE_REGISTRY_DEFAULT_CORES_CFG USERDATA_PATH "/emulation/default-cores.cfg"

///////////////////////////////////////
static void core_registry_trim(char* s) {
	char* start;
	char* end;
	if (!s) return;
	start = s;
	while (*start && isspace((unsigned char)*start)) start++;
	if (start != s) memmove(s, start, strlen(start) + 1);
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)*(end - 1))) end--;
	*end = '\0';
}

///////////////////////////////////////
int CORE_REGISTRY_loadRegistry(CoreRegistry* out) {
	FILE* file;
	char line[512];
	char path[MAX_PATH];
	const char* sdcard;
	if (!out) return -1;
	memset(out, 0, sizeof(*out));

	sdcard = getenv("SDCARD_PATH");
	if (!sdcard || !sdcard[0]) sdcard = SDCARD_PATH;
	snprintf(path, sizeof(path), "%s%s", sdcard, CORE_REGISTRY_CFG);

	file = fopen(path, "r");
	if (!file) return 0; // empty registry if no cores.cfg

	while (fgets(line, sizeof(line), file) && out->core_count < CORE_REGISTRY_MAX_CORES) {
		char* p;
		char* folder;
		char* core_file;
		char* core_name;
		char* system_name;
		CoreRegistryCore* core;
		CoreRegistrySystem* sys;

		normalizeNewline(line);
		trimTrailingNewlines(line);

		p = line;
		while (*p && isspace((unsigned char)*p)) p++;
		if (!*p || *p == '#') continue;

		folder = p;
		p = strchr(p, '|');
		if (!p) continue;
		*p++ = '\0';
		core_file = p;
		p = strchr(p, '|');
		if (!p) continue;
		*p++ = '\0';
		core_name = p;
		p = strchr(p, '|');
		if (!p) continue;
		*p++ = '\0';
		system_name = p;

		core_registry_trim(folder);
		core_registry_trim(core_file);
		core_registry_trim(core_name);
		core_registry_trim(system_name);
		if (!folder[0] || !core_file[0]) continue;

		// Register system display entry
		if (out->system_count < CORE_REGISTRY_MAX_SYSTEMS) {
			sys = &out->systems[out->system_count++];
			snprintf(sys->id, sizeof(sys->id), "%s", folder);
			snprintf(sys->name, sizeof(sys->name), "%s",
				system_name[0] ? system_name : folder);
		}

		// Register core entry
		core = &out->cores[out->core_count++];
		snprintf(core->id, sizeof(core->id), "%s", folder);
		snprintf(core->name, sizeof(core->name), "%s",
			core_name[0] ? core_name : folder);
		snprintf(core->launch_tag, sizeof(core->launch_tag), "%s", folder);
		snprintf(core->save_id, sizeof(core->save_id), "%s", folder);
		snprintf(core->libretro_path, sizeof(core->libretro_path),
			"%s/.cores/%s", sdcard, core_file);

		core->systems[0][0] = '\0';
		strncpy(core->systems[0], folder, sizeof(core->systems[0]) - 1);
		core->system_count = 1;
		core->default_priority = 10;
	}
	fclose(file);
	return 0;
}

///////////////////////////////////////
int CORE_REGISTRY_findSystemIndex(const CoreRegistry* reg, const char* system_id) {
	if (!reg || !system_id || !system_id[0]) return -1;
	for (int i = 0; i < reg->system_count; i++) {
		if (exactMatch((char*)reg->systems[i].id, (char*)system_id)) return i;
	}
	return -1;
}

int CORE_REGISTRY_coreSupportsSystem(const CoreRegistryCore* core, const char* system_id) {
	if (!core || !system_id || !system_id[0]) return 0;
	for (int i = 0; i < core->system_count; i++) {
		if (exactMatch((char*)core->systems[i], (char*)system_id)) return 1;
	}
	return 0;
}

const CoreRegistryCore* CORE_REGISTRY_resolveCore(const CoreRegistry* reg, const char* system_id, const char* override_core_id) {
	if (!reg || !system_id || !system_id[0]) return NULL;
	(void)override_core_id; // default-core overrides not supported in v1
	for (int i = 0; i < reg->core_count; i++) {
		if (CORE_REGISTRY_coreSupportsSystem(&reg->cores[i], system_id)) return &reg->cores[i];
	}
	return NULL;
}

const CoreRegistryCore* CORE_REGISTRY_findCoreByLaunchTag(const CoreRegistry* reg, const char* launch_tag) {
	if (!reg || !launch_tag || !launch_tag[0]) return NULL;
	for (int i = 0; i < reg->core_count; i++) {
		if (!strcasecmp(reg->cores[i].launch_tag, launch_tag)) return &reg->cores[i];
	}
	return NULL;
}

///////////////////////////////////////
static int core_registry_fsync_file(FILE* file) {
	int fd;
	if (!file) return -1;
	if (fflush(file) != 0) return -1;
	fd = fileno(file);
	if (fd < 0) return -1;
	if (fsync(fd) != 0) return -1;
	return 0;
}

static void core_registry_fsync_dir(const char* path) {
	int fd;
	if (!path || !path[0]) return;
	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0) return;
	fsync(fd);
	close(fd);
}

int CORE_REGISTRY_getDefaultCoreOverride(const char* system_id, char* core_id, size_t core_id_len) {
	FILE* file;
	char line[256];
	if (!core_id || core_id_len == 0) return -1;
	core_id[0] = '\0';
	if (!system_id || !system_id[0]) return -1;

	file = fopen(CORE_REGISTRY_DEFAULT_CORES_CFG, "r");
	if (!file) return -1;
	while (fgets(line, sizeof(line), file)) {
		char* key;
		char* val;
		normalizeNewline(line);
		trimTrailingNewlines(line);
		key = line;
		while (*key && isspace((unsigned char)*key)) key++;
		if (!*key || *key == '#') continue;
		val = strchr(key, '=');
		if (!val) continue;
		*val++ = '\0';
		core_registry_trim(key);
		core_registry_trim(val);
		if (exactMatch(key, (char*)system_id)) {
			snprintf(core_id, core_id_len, "%s", val);
			fclose(file);
			return 0;
		}
	}
	fclose(file);
	return -1;
}

int CORE_REGISTRY_setDefaultCoreOverride(const char* system_id, const char* core_id) {
	FILE* in;
	FILE* out;
	char line[256];
	char tmp_path[MAX_PATH];
	if (!system_id || !system_id[0]) return -1;

	mkdir(USERDATA_PATH, 0755);
	mkdir(USERDATA_PATH "/emulation", 0755);

	snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", CORE_REGISTRY_DEFAULT_CORES_CFG);
	out = fopen(tmp_path, "w");
	if (!out) return -1;

	in = fopen(CORE_REGISTRY_DEFAULT_CORES_CFG, "r");
	if (in) {
		while (fgets(line, sizeof(line), in)) {
			char keep_line[256];
			char* key;
			char* val;
			snprintf(keep_line, sizeof(keep_line), "%s", line);
			normalizeNewline(line);
			trimTrailingNewlines(line);
			key = line;
			while (*key && isspace((unsigned char)*key)) key++;
			if (!*key || *key == '#') {
				fputs(keep_line, out);
				continue;
			}
			val = strchr(key, '=');
			if (!val) {
				fputs(keep_line, out);
				continue;
			}
			*val++ = '\0';
			core_registry_trim(key);
			core_registry_trim(val);
			if (exactMatch(key, (char*)system_id)) continue;
			fputs(keep_line, out);
		}
		fclose(in);
	}

	if (core_id && core_id[0]) {
		fprintf(out, "%s=%s\n", system_id, core_id);
	}
	if (core_registry_fsync_file(out) != 0) {
		fclose(out);
		unlink(tmp_path);
		return -1;
	}
	if (fclose(out) != 0) {
		unlink(tmp_path);
		return -1;
	}
	if (rename(tmp_path, CORE_REGISTRY_DEFAULT_CORES_CFG) != 0) {
		unlink(tmp_path);
		return -1;
	}
	core_registry_fsync_dir(USERDATA_PATH "/emulation");
	return 0;
}

///////////////////////////////////////
int CORE_REGISTRY_resolveLaunchForSystem(const char* system_name, char* out_core_id, size_t out_core_id_len, char* out_emu_path, size_t out_emu_path_len) {
	CoreRegistry reg;
	const CoreRegistryCore* core;
	char system_id[64];
	char override_core[64] = {0};
	if (out_core_id && out_core_id_len) out_core_id[0] = '\0';
	if (out_emu_path && out_emu_path_len) out_emu_path[0] = '\0';
	if (!system_name || !*system_name) return -1;

	getCanonicalEmuId(system_name, system_id);
	CORE_REGISTRY_loadRegistry(&reg);
	CORE_REGISTRY_getDefaultCoreOverride(system_id, override_core, sizeof(override_core));
	core = CORE_REGISTRY_resolveCore(&reg, system_id, override_core);
	if (!core) return -1;

	if (out_core_id && out_core_id_len) {
		snprintf(out_core_id, out_core_id_len, "%s", core->save_id[0] ? core->save_id : core->id);
	}
	if (out_emu_path && out_emu_path_len) {
		snprintf(out_emu_path, out_emu_path_len, "%s", core->libretro_path);
		if (!exists(out_emu_path)) return -1;
	}
	return 0;
}

int CORE_REGISTRY_resolveSaveIdForSystem(const char* system_name, char* out_save_id, size_t out_save_id_len) {
	CoreRegistry reg;
	const CoreRegistryCore* core;
	char system_id[64];
	char override_core[64] = {0};
	if (!out_save_id || out_save_id_len == 0) return -1;
	out_save_id[0] = '\0';
	if (!system_name || !system_name[0]) return -1;

	getCanonicalEmuId(system_name, system_id);
	CORE_REGISTRY_loadRegistry(&reg);
	CORE_REGISTRY_getDefaultCoreOverride(system_id, override_core, sizeof(override_core));
	core = CORE_REGISTRY_resolveCore(&reg, system_id, override_core);
	if (!core) return -1;

	snprintf(out_save_id, out_save_id_len, "%s", core->save_id[0] ? core->save_id : core->id);
	return 0;
}

int CORE_REGISTRY_resolveSaveIdForLaunchTag(const char* launch_tag, char* out_save_id, size_t out_save_id_len) {
	CoreRegistry reg;
	const CoreRegistryCore* core;
	if (!out_save_id || out_save_id_len == 0) return -1;
	out_save_id[0] = '\0';
	if (!launch_tag || !launch_tag[0]) return -1;

	CORE_REGISTRY_loadRegistry(&reg);
	core = CORE_REGISTRY_findCoreByLaunchTag(&reg, launch_tag);
	if (!core) return -1;
	snprintf(out_save_id, out_save_id_len, "%s", core->save_id[0] ? core->save_id : core->id);
	return 0;
}

///////////////////////////////////////
/* BIOS checks are disabled in v1; cores report missing BIOS at runtime. */
int CORE_REGISTRY_checkBios(const CoreRegistryCore* core, const char* system_id, const char* bios_root, char missing[][128], int max_missing) {
	(void)core;
	(void)system_id;
	(void)bios_root;
	(void)missing;
	(void)max_missing;
	return 0;
}

int CORE_REGISTRY_listBiosFiles(const CoreRegistryCore* core, const char* system_id, const char* bios_root, CoreRegistryBiosFileStatus* out, int max_out) {
	(void)core;
	(void)system_id;
	(void)bios_root;
	(void)out;
	(void)max_out;
	return 0;
}
