#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "traits.h"

int main(int argc, char **argv)
{
	const MinimeTraits *traits;
	SDL_Window *window;
	SDL_Renderer *renderer;
	SDL_Surface *image;
	SDL_Texture *texture;
	SDL_Rect dst;
	int delay;

	if (argc < 2) {
		fprintf(stderr, "Usage: minui-show <image.png> [seconds]\n");
		return 1;
	}
	if (access(argv[1], R_OK) != 0 || MINIME_traitsInit() != 0)
		return 1;
	traits = MINIME_traits();
	delay = argc > 2 ? atoi(argv[2]) : 2;

	if (SDL_Init(SDL_INIT_VIDEO) != 0)
		return 1;
	window = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED, traits->screen_width, traits->screen_height,
		SDL_WINDOW_SHOWN);
	renderer = window ? SDL_CreateRenderer(window, -1,
		SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
	image = renderer ? IMG_Load(argv[1]) : NULL;
	texture = image ? SDL_CreateTextureFromSurface(renderer, image) : NULL;
	if (!window || !renderer || !image || !texture) {
		if (image)
			SDL_FreeSurface(image);
		if (renderer)
			SDL_DestroyRenderer(renderer);
		if (window)
			SDL_DestroyWindow(window);
		SDL_Quit();
		return 1;
	}

	dst.w = image->w;
	dst.h = image->h;
	dst.x = (traits->screen_width - dst.w) / 2;
	dst.y = (traits->screen_height - dst.h) / 2;
	SDL_RenderClear(renderer);
	if (traits->screen_rotation)
		SDL_RenderCopyEx(renderer, texture, NULL, &dst,
			traits->screen_rotation, NULL, SDL_FLIP_NONE);
	else
		SDL_RenderCopy(renderer, texture, NULL, &dst);
	SDL_RenderPresent(renderer);
	sleep(delay);

	SDL_DestroyTexture(texture);
	SDL_FreeSurface(image);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
