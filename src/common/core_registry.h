#ifndef CORE_REGISTRY_H
#define CORE_REGISTRY_H

#include <stddef.h>

int CORE_REGISTRY_resolveLaunchForSystem(const char *system_id,
	char *out_core_id, size_t out_core_id_len,
	char *out_emu_path, size_t out_emu_path_len);
int CORE_REGISTRY_resolveSaveIdForSystem(const char *system_id,
	char *out_save_id, size_t out_save_id_len);
int CORE_REGISTRY_resolveSaveIdForLaunchTag(const char *launch_tag,
	char *out_save_id, size_t out_save_id_len);

#endif
