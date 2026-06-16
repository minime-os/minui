#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "traits.h"
#include "video.h"
#include "utils.h"

int MINIME_videoHDMIConnected(void)
{
	const MinimeTraits *traits = MINIME_traits();

	return (traits && MINIME_traitAvailable(traits->hdmi_state_path)) ? getInt((char*)traits->hdmi_state_path) : 0;
}

void MINIME_videoSetBacklight(int value)
{
	const MinimeTraits *traits = MINIME_traits();

	if (traits && MINIME_traitAvailable(traits->backlight_path))
		putInt((char*)traits->backlight_path, value);
}

void MINIME_videoBlank(int blank)
{
	const MinimeTraits *traits = MINIME_traits();

	if (traits && MINIME_traitAvailable(traits->framebuffer_blank_path))
		putInt((char*)traits->framebuffer_blank_path, blank ? 4 : 0);
}
