#include <stdio.h>
#include <stdlib.h>

#include "audio.h"
#include "traits.h"

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

int MINIME_audioJackConnected(void)
{
	const MinimeTraits *traits = MINIME_traits();

	return traits ? readInt(traits->jack_state_path) : 0;
}

void MINIME_audioSetRawVolume(int value)
{
	const MinimeTraits *traits = MINIME_traits();
	char command[512];

	if (!traits)
		return;
	snprintf(command, sizeof(command),
		"amixer -q -c '%s' sset '%s' %d%% >/dev/null 2>&1",
		traits->sound_card, traits->sound_mixer, value);
	(void)system(command);
}
