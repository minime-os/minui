#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "input.h"
#include "traits.h"

#define INPUT_EVENT_LIMIT 32

int MINIME_inputOpenByName(const char *expected)
{
	char name[256];
	char path[64];

	if (!MINIME_traitAvailable(expected))
		return -1;
	for (int i = 0; i < INPUT_EVENT_LIMIT; i++) {
		int fd;

		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		memset(name, 0, sizeof(name));
		if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 &&
			!strcmp(name, expected))
			return fd;
		close(fd);
	}
	return -1;
}

int MINIME_inputOpenShortcutDevices(int *fds, size_t max_fds)
{
	const MinimeTraits *traits = MINIME_traits();
	const char *names[3];
	int count = 0;

	if (!traits || !fds)
		return 0;
	names[0] = traits->input_gamepad;
	names[1] = traits->input_power;
	names[2] = traits->input_volume;
	for (size_t i = 0; i < 3 && (size_t)count < max_fds; i++) {
		int fd = MINIME_inputOpenByName(names[i]);

		if (fd >= 0)
			fds[count++] = fd;
	}
	return count;
}

int MINIME_inputHasCZ(void)
{
	const MinimeTraits *traits = MINIME_traits();

	return traits && traits->key_c >= 0 && traits->key_z >= 0;
}

int MINIME_inputNormalizeAxis(int value, int invert)
{
	const MinimeTraits *traits = MINIME_traits();
	int normalized;

	if (!traits || traits->axis_min >= traits->axis_center ||
		traits->axis_center >= traits->axis_max)
		return 0;
	if (value < traits->axis_center) {
		normalized = -((traits->axis_center - value) * 32767) /
			(traits->axis_center - traits->axis_min);
	} else {
		normalized = ((value - traits->axis_center) * 32767) /
			(traits->axis_max - traits->axis_center);
	}
	return invert ? -normalized : normalized;
}
