#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "traits.h"
#include "video.h"

static int readInt(const char *path)
{
	FILE *file;
	int value = 0;

	if (!MINIME_traitAvailable(path))
		return 0;
	file = fopen(path, "r");
	if (file) {
		(void)fscanf(file, "%d", &value);
		fclose(file);
	}
	return value;
}

static void writeInt(const char *path, int value)
{
	char text[16];
	int fd;
	int length;

	if (!MINIME_traitAvailable(path))
		return;
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return;
	length = snprintf(text, sizeof(text), "%d\n", value);
	(void)write(fd, text, (size_t)length);
	close(fd);
}

int MINIME_videoHDMIConnected(void)
{
	const MinimeTraits *traits = MINIME_traits();

	return traits ? readInt(traits->hdmi_state_path) : 0;
}

void MINIME_videoSetBacklight(int value)
{
	const MinimeTraits *traits = MINIME_traits();

	if (traits)
		writeInt(traits->backlight_path, value);
}

void MINIME_videoBlank(int blank)
{
	const MinimeTraits *traits = MINIME_traits();

	if (traits)
		writeInt(traits->framebuffer_blank_path, blank ? 4 : 0);
}
