#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core_registry.h"
#include "defines.h"
#include "utils.h"

#define CORES_CONFIG "/.minime/config/cores.cfg"

typedef struct CoreEntry {
	char system[64];
	char filename[128];
} CoreEntry;


static int findCore(const char *system, CoreEntry *entry)
{
	const char *sdcard = getenv("SDCARD_PATH");
	char canonical[64];
	char path[MAX_PATH];
	char line[512];
	FILE *file;

	if (!system || !entry)
		return -1;
	if (!sdcard || !sdcard[0])
		sdcard = SDCARD_PATH;
	getCanonicalEmuId(system, canonical);
	snprintf(path, sizeof(path), "%s%s", sdcard, CORES_CONFIG);
	file = fopen(path, "r");
	if (!file)
		return -1;
	while (fgets(line, sizeof(line), file)) {
		char *folder;
		char *filename;
		char *separator;
		char candidate[64];

		folder = trim(line);
		if (!folder[0] || folder[0] == '#')
			continue;
		separator = strchr(folder, '|');
		if (!separator)
			continue;
		*separator++ = '\0';
		filename = trim(separator);
		separator = strchr(filename, '|');
		if (separator)
			*separator = '\0';
		folder = trim(folder);
		filename = trim(filename);
		getCanonicalEmuId(folder, candidate);
		if (strcmp(candidate, canonical))
			continue;
		snprintf(entry->system, sizeof(entry->system), "%s", canonical);
		snprintf(entry->filename, sizeof(entry->filename), "%s", filename);
		fclose(file);
		return filename[0] ? 0 : -1;
	}
	fclose(file);
	return -1;
}

int CORE_REGISTRY_resolveLaunchForSystem(const char *system,
	char *out_core_id, size_t out_core_id_len,
	char *out_emu_path, size_t out_emu_path_len)
{
	const char *sdcard = getenv("SDCARD_PATH");
	CoreEntry entry;

	if (findCore(system, &entry) != 0)
		return -1;
	if (!sdcard || !sdcard[0])
		sdcard = SDCARD_PATH;
	if (out_core_id && out_core_id_len)
		snprintf(out_core_id, out_core_id_len, "%s", entry.system);
	if (out_emu_path && out_emu_path_len) {
		snprintf(out_emu_path, out_emu_path_len, "%s/.cores/%s",
			sdcard, entry.filename);
		if (!exists(out_emu_path))
			return -1;
	}
	return 0;
}

int CORE_REGISTRY_resolveSaveIdForSystem(const char *system,
	char *out_save_id, size_t out_save_id_len)
{
	CoreEntry entry;

	if (!out_save_id || !out_save_id_len || findCore(system, &entry) != 0)
		return -1;
	snprintf(out_save_id, out_save_id_len, "%s", entry.system);
	return 0;
}

int CORE_REGISTRY_resolveSaveIdForLaunchTag(const char *launch_tag,
	char *out_save_id, size_t out_save_id_len)
{
	return CORE_REGISTRY_resolveSaveIdForSystem(launch_tag, out_save_id,
		out_save_id_len);
}
