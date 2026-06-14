// rg35xxplus
#include <stdio.h>
#include <unistd.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <string.h>

#define FIXED_BPP		2
#define FIXED_DEPTH		(FIXED_BPP * 8)

#define RGBA_MASK_565	0xF800, 0x07E0, 0x001F, 0x0000

static int get_screen_rotation(void) {
	FILE* file = fopen("/mnt/sdcard/.minime/traits", "r");
	if (!file) return -1;
	char line[256];
	int rot = -1;
	while (fgets(line, sizeof(line), file)) {
		if (line[0] == '#' || line[0] == '\n') continue;
		char* sep = strchr(line, '=');
		if (!sep) continue;
		*sep = '\0';
		char* key = line;
		char* val = sep + 1;
		char* end = key + strlen(key) - 1;
		while (end >= key && (*end == ' ' || *end == '\n' || *end == '\r')) {
			*end = '\0';
			end--;
		}
		end = val + strlen(val) - 1;
		while (end >= val && (*end == ' ' || *end == '\n' || *end == '\r')) {
			*end = '\0';
			end--;
		}
		if (strcmp(key, "screen_rotation") == 0) {
			rot = atoi(val);
			break;
		}
	}
	fclose(file);
	return rot;
}

int main(int argc , char* argv[]) {
	if (argc<2) {
		puts("Usage: show.elf image.png delay");
		return 0;
	}
	
	char path[256];
	strncpy(path,argv[1],256);
	if (access(path, F_OK)!=0) return 0; // nothing to show :(
	
	int delay = argc>2 ? atoi(argv[2]) : 2;
	
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		fprintf(stderr, "SDL video init failed: %s\n", SDL_GetError());
		return 1;
	}
	fprintf(stderr, "SDL video driver: %s\n", SDL_GetCurrentVideoDriver());
	SDL_ShowCursor(0);
	
	int w = 0;
	int h = 0;
	int p = 0;
	SDL_Window* window = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w,h, SDL_WINDOW_SHOWN);
	if (!window) {
		fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
		return 1;
	}
	
	int rotate = 0;
	int traits_rot = get_screen_rotation();
	SDL_DisplayMode mode;
	SDL_GetCurrentDisplayMode(0, &mode);
	if (traits_rot == -1) {
		if (mode.h>mode.w) rotate = 3;
	} else {
		rotate = traits_rot / 90;
	}
	w = mode.w;
	h = mode.h;
	p = mode.w * FIXED_BPP;
	
	SDL_Renderer* renderer = SDL_CreateRenderer(window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) {
		fprintf(stderr, "SDL renderer creation failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_RendererInfo info;
	if (SDL_GetRendererInfo(renderer, &info) == 0)
		fprintf(stderr, "SDL render driver: %s\n", info.name);
	fprintf(stderr, "Creating SDL texture: RGB565 %ix%i\n", w,h);
	SDL_Texture* texture = SDL_CreateTexture(renderer,SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w,h);
	if (!texture) {
		fprintf(stderr, "SDL texture creation failed: %s\n", SDL_GetError());
		return 1;
	}
	fprintf(stderr, "SDL texture created\n");
	void* pixels;
	if (SDL_LockTexture(texture, NULL, &pixels, &p) != 0) {
		fprintf(stderr, "SDL texture lock failed: %s\n", SDL_GetError());
		return 1;
	}
	fprintf(stderr, "SDL texture locked\n");
	SDL_Surface* screen = SDL_CreateRGBSurfaceFrom(pixels, w,h, FIXED_DEPTH, p, RGBA_MASK_565);
	if (!screen) {
		fprintf(stderr, "SDL screen surface creation failed: %s\n", SDL_GetError());
		return 1;
	}
	fprintf(stderr, "SDL screen surface created\n");
	SDL_FillRect(screen, NULL, 0);
	fprintf(stderr, "SDL screen cleared\n");
	SDL_Surface* img = IMG_Load(path);
	if (!img) {
		fprintf(stderr, "SDL image load failed: %s\n", IMG_GetError());
		return 1;
	}
	fprintf(stderr, "SDL image loaded: %ix%i\n", img->w,img->h);
	SDL_BlitSurface(img, NULL, screen, &(SDL_Rect){(screen->w-img->w)/2,(screen->h-img->h)/2});
	fprintf(stderr, "SDL image blitted\n");
	SDL_FreeSurface(img);
	SDL_FreeSurface(screen);
	SDL_UnlockTexture(texture);
	fprintf(stderr, "SDL texture unlocked\n");

	if (rotate) {
		int dx = 0;
		int dy = 0;
		if (rotate == 1) {
			dx = h;
			dy = 0;
		} else if (rotate == 2) {
			dx = w;
			dy = h;
		} else if (rotate == 3) {
			dx = 0;
			dy = w;
		}
		SDL_RenderCopyEx(renderer,texture,NULL,&(SDL_Rect){dx,dy,w,h},rotate*90,&(SDL_Point){0,0},SDL_FLIP_NONE);
	}
	else SDL_RenderCopy(renderer, texture, NULL,NULL);
	fprintf(stderr, "SDL texture copied\n");
	SDL_RenderPresent(renderer);
	fprintf(stderr, "SDL frame presented\n");
	
	sleep(delay);
	
	SDL_DestroyTexture(texture);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
