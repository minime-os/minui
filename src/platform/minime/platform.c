// minime platform
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include <msettings.h>

#include "defines.h"
#include "platform.h"
#include "api.h"
#include "utils.h"

#include "scaler.h"
#include "input.h"
#include "power.h"
#include "traits.h"
#include "video.h"

int plat_fixed_width = 640;
int plat_fixed_height = 480;
int plat_has_hdmi = 0;
int plat_main_row_count = 6;
int plat_padding = 10;
int plat_screen_rotation = -1;
int on_hdmi = 0;
int is_cubexx = 0;
int is_rg34xx = 0;
static int rotate = 0;
static const MinimeTraits *traits;

// Raw keycodes
static int k_up = 544;
static int k_down = 545;
static int k_left = 546;
static int k_right = 547;
static int k_a = 305;
static int k_b = 304;
static int k_x = 308;
static int k_y = 307;
static int k_c = 306;
static int k_z = 309;
static int k_l1 = 310;
static int k_r1 = 311;
static int k_l2 = 312;
static int k_r2 = 313;
static int k_l3 = 317;
static int k_r3 = 318;
static int k_start = 315;
static int k_select = 314;
static int k_menu = 316;
static int k_power = 116;
static int k_vol_up = 115;
static int k_vol_down = 114;

static void load_traits(void) {
	if (MINIME_traitsInit() != 0)
		exit(1);
	traits = MINIME_traits();
	plat_fixed_width = traits->screen_width;
	plat_fixed_height = traits->screen_height;
	plat_screen_rotation = traits->screen_rotation;
	plat_has_hdmi = MINIME_traitAvailable(traits->hdmi_state_path);
	k_up = traits->key_up;
	k_down = traits->key_down;
	k_left = traits->key_left;
	k_right = traits->key_right;
	k_a = traits->key_a;
	k_b = traits->key_b;
	k_c = traits->key_c;
	k_x = traits->key_x;
	k_y = traits->key_y;
	k_z = traits->key_z;
	k_l1 = traits->key_l1;
	k_r1 = traits->key_r1;
	k_l2 = traits->key_l2;
	k_r2 = traits->key_r2;
	k_l3 = traits->key_l3;
	k_r3 = traits->key_r3;
	k_start = traits->key_start;
	k_select = traits->key_select;
	k_menu = traits->key_menu;
	k_power = traits->key_power;
	k_vol_up = traits->key_vol_up;
	k_vol_down = traits->key_vol_down;

    // derive layout properties
    plat_padding = (plat_fixed_width >= 720) ? 40 : 10;
    plat_main_row_count = (plat_fixed_width >= 720) ? 8 : 6;
    if (plat_screen_rotation != -1) {
        rotate = plat_screen_rotation / 90;
    }
    is_cubexx = (plat_fixed_width == 720 && plat_fixed_height == 720);
    is_rg34xx = (plat_fixed_width == 720 && plat_fixed_height == 480);
}

///////////////////////////////

#define RAW_HATY	17
#define RAW_HATX	16

#define INPUT_COUNT 3
static int inputs[INPUT_COUNT];
static uint32_t local_pad_pressed = 0;

#define kRawIndex 1
#define kVolumeIndex 2
static void drainInputFd(int input);
static void drainAllInputs(void);

static int anyButtonSourcePressed(int btn) {
	return (local_pad_pressed & btn) != 0;
}

static void updateButtonState(uint32_t *source_pressed, int btn, int pressed,
	int id, uint32_t tick)
{
	if (!source_pressed || btn == BTN_NONE || id < 0 || id >= BTN_ID_COUNT)
		return;

	if (pressed)
		*source_pressed |= btn;
	else
		*source_pressed &= ~btn;

	if (anyButtonSourcePressed(btn)) {
		if ((pad.is_pressed & btn) == BTN_NONE) {
			pad.just_pressed |= btn;
			pad.just_repeated |= btn;
			pad.is_pressed |= btn;
			pad.repeat_at[id] = tick + PAD_REPEAT_DELAY;
		}
	}
	else if (pad.is_pressed & btn) {
		pad.is_pressed &= ~btn;
		pad.just_released |= btn;
		pad.just_repeated &= ~btn;
	}
}

static void clearStaleAggregateButtons(void)
{
	for (int i = 0; i < BTN_ID_COUNT; i++) {
		int btn = 1 << i;
		if ((pad.is_pressed & btn) && !anyButtonSourcePressed(btn)) {
			pad.is_pressed &= ~btn;
			pad.just_released |= btn;
			pad.just_repeated &= ~btn;
		}
	}
}

void PLAT_initLid(void) {
	lid.has_lid = traits &&
		MINIME_traitAvailable(traits->lid_switch_path);
}
int PLAT_lidChanged(int* state) {
	if (lid.has_lid) {
		int lid_open = getInt((char *)traits->lid_switch_path);
		if (lid_open!=lid.is_open) {
			lid.is_open = lid_open;
			if (state) *state = lid_open;
			return 1;
		}
	}
	return 0;
}

void PLAT_initInput(void) {
	inputs[0] = MINIME_inputOpenByName(traits->input_power);
	inputs[kRawIndex] = MINIME_inputOpenByName(traits->input_gamepad);
	inputs[kVolumeIndex] = MINIME_inputOpenByName(traits->input_volume);
	local_pad_pressed = 0;
	drainAllInputs();
}
void PLAT_quitInput(void) {
	local_pad_pressed = 0;
	for (int i=0; i<INPUT_COUNT; i++) {
		if (inputs[i] >= 0)
			close(inputs[i]);
	}
}

// from <linux/input.h> which has BTN_ constants that conflict with platform.h
struct input_event {
	struct timeval time;
	__u16 type;
	__u16 code;
	__s32 value;
};
#define EV_KEY			0x01
#define EV_ABS			0x03

static void drainInputFd(int input) {
	struct input_event event;
	if (input<0) return;
	while (read(input, &event, sizeof(event))==sizeof(event)) {}
}
static void drainAllInputs(void) {
	for (int i=0; i<INPUT_COUNT; i++) {
		drainInputFd(inputs[i]);
	}
}

void PLAT_pollInput(void) {
	// reset transient state
	pad.just_pressed = BTN_NONE;
	pad.just_released = BTN_NONE;
	pad.just_repeated = BTN_NONE;

	uint32_t tick = SDL_GetTicks();
	for (int i=0; i<BTN_ID_COUNT; i++) {
		int btn = 1 << i;
		if ((pad.is_pressed & btn) && (tick>=pad.repeat_at[i])) {
			pad.just_repeated |= btn; // set
			pad.repeat_at[i] += PAD_REPEAT_INTERVAL;
		}
	}
	
	clearStaleAggregateButtons();
	
	// the actual poll
	int input;
	static struct input_event event;
	for (int i=0; i<INPUT_COUNT; i++) {
		input = inputs[i];
		if (input<0) continue;
		while (read(input, &event, sizeof(event))==sizeof(event)) {
			if (event.type!=EV_KEY && event.type!=EV_ABS) continue;

			int btn = BTN_NONE;
			int pressed = 0; // 0=up,1=down
			int id = -1;
			int type = event.type;
			int code = event.code;
			int value = event.value;
			
			// TODO: tmp, hardcoded, missing some buttons
			if (type==EV_KEY) {
				uint32_t *source_pressed = &local_pad_pressed;
				if (value>1) continue; // ignore repeats
			
				pressed = value;
						 if (code==k_up) 		{ btn = BTN_DPAD_UP; 	id = BTN_ID_DPAD_UP; }
		 			else if (code==k_down)	{ btn = BTN_DPAD_DOWN; 	id = BTN_ID_DPAD_DOWN; }
					else if (code==k_left)	{ btn = BTN_DPAD_LEFT; 	id = BTN_ID_DPAD_LEFT; }
					else if (code==k_right)	{ btn = BTN_DPAD_RIGHT; id = BTN_ID_DPAD_RIGHT; }
					else if (code==k_a)		{ btn = BTN_A; 			id = BTN_ID_A; }
					else if (code==k_b)		{ btn = BTN_B; 			id = BTN_ID_B; }
					else if (code==k_x)		{ btn = BTN_X; 			id = BTN_ID_X; }
					else if (code==k_y)		{ btn = BTN_Y; 			id = BTN_ID_Y; }
					else if (code==k_c)		{ btn = BTN_C; 			id = BTN_ID_C; }
					else if (code==k_z)		{ btn = BTN_Z; 			id = BTN_ID_Z; }
					else if (code==k_start)	{ btn = BTN_START; 		id = BTN_ID_START; }
					else if (code==k_select)	{ btn = BTN_SELECT; 	id = BTN_ID_SELECT; }
					else if (code==k_menu)	{ btn = BTN_MENU; 		id = BTN_ID_MENU; }
					else if (code==k_l1)		{ btn = BTN_L1; 		id = BTN_ID_L1; }
					else if (code==k_l2)		{ btn = BTN_L2; 		id = BTN_ID_L2; }
					else if (code==k_l3)		{ btn = BTN_L3; 		id = BTN_ID_L3; }
					else if (code==k_r1)		{ btn = BTN_R1; 		id = BTN_ID_R1; }
					else if (code==k_r2)		{ btn = BTN_R2; 		id = BTN_ID_R2; }
					else if (code==k_r3)		{ btn = BTN_R3; 		id = BTN_ID_R3; }
					else if (code==k_vol_up)	{ btn = BTN_PLUS; 		id = BTN_ID_PLUS; }
					else if (code==k_vol_down)	{ btn = BTN_MINUS; 		id = BTN_ID_MINUS; }
					else if (code==k_power)	{ btn = BTN_POWER; 		id = BTN_ID_POWER; }

				if (btn != BTN_NONE) {
					updateButtonState(source_pressed, btn, pressed, id, tick);
				}
				continue;
			}
			else if (type==EV_ABS) {
				// LOG_info("abs event: %i (%i)\n", code,value);
				// { up, down, left, right }
				if (code==RAW_HATY || code==RAW_HATX) {
					if (value>1) continue; // ignore repeats
			
					int hats[4] = {-1,-1,-1,-1}; // -1=no change,1=pressed,0=released
					if (code==RAW_HATY) {
						hats[0] = value==-1; // up
						hats[1] = value==1; // down
					}
					else if (code==RAW_HATX) { // left/right
						hats[2] = value==-1; // left
						hats[3] = value==1; // right
					}
				
					for (id=0; id<4; id++) {
						int state = hats[id];
						btn = 1 << id;
						if (state==0) {
							pad.is_pressed		&= ~btn; // unset
							pad.just_repeated	&= ~btn; // unset
							pad.just_released	|= btn; // set
						}
						else if (state==1 && (pad.is_pressed & btn)==BTN_NONE) {
							pad.just_pressed	|= btn; // set
							pad.just_repeated	|= btn; // set
							pad.is_pressed		|= btn; // set
							pad.repeat_at[id]	= tick + PAD_REPEAT_DELAY;
						}
					}
					
					btn = BTN_NONE; // already handled, force continue
				}
						 if (code==traits->axis_lx) { pad.laxis.x = MINIME_inputNormalizeAxis(value, traits->axis_lx_invert); PAD_setAnalog(BTN_ID_ANALOG_LEFT, BTN_ID_ANALOG_RIGHT, pad.laxis.x, tick+PAD_REPEAT_DELAY); }
					else if (code==traits->axis_ly) { pad.laxis.y = MINIME_inputNormalizeAxis(value, traits->axis_ly_invert); PAD_setAnalog(BTN_ID_ANALOG_UP, BTN_ID_ANALOG_DOWN, pad.laxis.y, tick+PAD_REPEAT_DELAY); }
					else if (code==traits->axis_rx) pad.raxis.x = MINIME_inputNormalizeAxis(value, traits->axis_rx_invert);
					else if (code==traits->axis_ry) pad.raxis.y = MINIME_inputNormalizeAxis(value, traits->axis_ry_invert);
			}
			
			if (btn==BTN_NONE) continue;
		
			if (!pressed) {
				pad.is_pressed		&= ~btn; // unset
				pad.just_repeated	&= ~btn; // unset
				pad.just_released	|= btn; // set
			}
			else if ((pad.is_pressed & btn)==BTN_NONE) {
				pad.just_pressed	|= btn; // set
				pad.just_repeated	|= btn; // set
				pad.is_pressed		|= btn; // set
				pad.repeat_at[id]	= tick + PAD_REPEAT_DELAY;
			}
		}
	}
	
	if (lid.has_lid && PLAT_lidChanged(NULL) && !lid.is_open)
		PWR_requestLidAction();
}

int PLAT_shouldWake(void) {
	int lid_open = 1; // assume open by default
	if (lid.has_lid && PLAT_lidChanged(&lid_open) && lid_open) return 1;
	
	int input;
	static struct input_event event;
	for (int i=0; i<INPUT_COUNT; i++) {
		input = inputs[i];
		while (read(input, &event, sizeof(event))==sizeof(event)) {
			if (event.type==EV_KEY && event.code==k_power && event.value==0) {
				// ignore input while lid is closed
				if (lid.has_lid && !lid.is_open) return 0;  // do it here so we eat the input
				return 1;
			}
		}
	}
	return 0;
}

///////////////////////////////

// based on rgb30 + tg5040 + m17
static struct VID_Context {
	SDL_Window* window;
	SDL_Renderer* renderer;
	SDL_Texture* texture;
	SDL_Texture* target;
	SDL_Texture* effect;

	SDL_Surface* screen;
	
	GFX_Renderer* blit; // yeesh
	
	int width;
	int height;
	int pitch;
	int sharpness;
	int use_direct_fb;
	int fb_fd;
	uint8_t* fb_map;
	size_t fb_map_len;
	int fb_stride;
	int fb_xres;
	int fb_yres;
	int fb_bpp;
	int fb_red_offset;
	int fb_green_offset;
	int fb_blue_offset;
} vid;

static int device_width;
static int device_height;
static int device_pitch;

static int PLAT_initDirectFB(void)
{
	struct fb_fix_screeninfo finfo;
	struct fb_var_screeninfo vinfo;

	vid.fb_fd = open(traits->video_device, O_RDWR | O_CLOEXEC);
	if (vid.fb_fd < 0)
		return -1;

	if (ioctl(vid.fb_fd, FBIOGET_FSCREENINFO, &finfo) != 0)
		goto fail;
	if (ioctl(vid.fb_fd, FBIOGET_VSCREENINFO, &vinfo) != 0)
		goto fail;
	if ((int)vinfo.bits_per_pixel != 32)
		goto fail;
	if ((int)vinfo.xres < FIXED_WIDTH || (int)vinfo.yres < FIXED_HEIGHT)
		goto fail;

	vid.fb_map_len = finfo.smem_len;
	vid.fb_map = mmap(NULL, vid.fb_map_len, PROT_READ | PROT_WRITE,
		MAP_SHARED, vid.fb_fd, 0);
	if (vid.fb_map == MAP_FAILED) {
		vid.fb_map = NULL;
		goto fail;
	}

	vid.use_direct_fb = 1;
	vid.fb_stride = finfo.line_length;
	vid.fb_xres = (int)vinfo.xres;
	vid.fb_yres = (int)vinfo.yres;
	vid.fb_bpp = (int)vinfo.bits_per_pixel;
	vid.fb_red_offset = (int)vinfo.red.offset;
	vid.fb_green_offset = (int)vinfo.green.offset;
	vid.fb_blue_offset = (int)vinfo.blue.offset;
	return 0;

fail:
	if (vid.fb_map) {
		munmap(vid.fb_map, vid.fb_map_len);
		vid.fb_map = NULL;
	}
	if (vid.fb_fd >= 0) {
		close(vid.fb_fd);
		vid.fb_fd = -1;
	}
	vid.use_direct_fb = 0;
	return -1;
}

static void PLAT_quitDirectFB(void)
{
	if (vid.fb_map) {
		munmap(vid.fb_map, vid.fb_map_len);
		vid.fb_map = NULL;
	}
	if (vid.fb_fd >= 0) {
		close(vid.fb_fd);
		vid.fb_fd = -1;
	}
	vid.use_direct_fb = 0;
}

static inline uint32_t rgb565_to_fb32(uint16_t px)
{
	uint32_t r = (px >> 11) & 0x1f;
	uint32_t g = (px >> 5) & 0x3f;
	uint32_t b = px & 0x1f;
	r = (r << 3) | (r >> 2);
	g = (g << 2) | (g >> 4);
	b = (b << 3) | (b >> 2);
	return (r << vid.fb_red_offset) | (g << vid.fb_green_offset) |
		(b << vid.fb_blue_offset);
}

static void PLAT_presentSurfaceDirectFB(SDL_Surface* surface)
{
	int copy_w;
	int copy_h;

	if (!vid.use_direct_fb || !vid.fb_map || !surface || !surface->pixels)
		return;
	if (surface->format->BitsPerPixel != 16)
		return;

	copy_w = surface->w < vid.fb_xres ? surface->w : vid.fb_xres;
	copy_h = surface->h < vid.fb_yres ? surface->h : vid.fb_yres;
	if (copy_w <= 0 || copy_h <= 0)
		return;

	if (copy_w != vid.fb_xres || copy_h != vid.fb_yres)
		memset(vid.fb_map, 0, vid.fb_map_len);

	for (int y = 0; y < copy_h; y++) {
		const uint16_t* src = (const uint16_t*)((uint8_t*)surface->pixels +
			(y * surface->pitch));
		uint32_t* dst = (uint32_t*)(vid.fb_map + (y * vid.fb_stride));
		for (int x = 0; x < copy_w; x++) {
			dst[x] = rgb565_to_fb32(src[x]);
		}
	}
}

static void PLAT_computeRendererRects(const GFX_Renderer *renderer,
	SDL_Rect *src_rect, SDL_Rect *dst_rect)
{
	int x;
	int y;
	int w;
	int h;

	if (!renderer || !src_rect || !dst_rect)
		return;

	x = renderer->src_x;
	y = renderer->src_y;
	w = renderer->src_w;
	h = renderer->src_h;

	*src_rect = (SDL_Rect){x, y, w, h};
	*dst_rect = (SDL_Rect){0, 0, device_width, device_height};

	if (renderer->aspect == 0) {
		if (renderer->scale > 0) {
			w = renderer->src_w * renderer->scale;
			h = renderer->src_h * renderer->scale;
			x = (device_width - w) / 2;
			y = (device_height - h) / 2;
		}
		else {
			w = renderer->src_w - (renderer->src_x * 2);
			h = renderer->src_h - (renderer->src_y * 2);
			if (w <= 0)
				w = MIN(renderer->src_w, device_width);
			if (h <= 0)
				h = MIN(renderer->src_h, device_height);
			x = renderer->dst_x;
			y = renderer->dst_y;
			src_rect->w = w;
			src_rect->h = h;
		}

		dst_rect->x = x;
		dst_rect->y = y;
		dst_rect->w = w;
		dst_rect->h = h;
		return;
	}

	if (renderer->aspect > 0) {
		h = device_height;
		w = h * renderer->aspect;
		if (w > device_width) {
			double ratio = 1 / renderer->aspect;
			w = device_width;
			h = w * ratio;
		}
		x = (device_width - w) / 2;
		y = (device_height - h) / 2;

		dst_rect->x = x;
		dst_rect->y = y;
		dst_rect->w = w;
		dst_rect->h = h;
	}
}

static void PLAT_blitRendererDirectFB(const GFX_Renderer *renderer)
{
	const uint16_t *src_pixels;
	uint16_t *dst_pixels;
	SDL_Rect src_rect;
	SDL_Rect dst_rect;
	int src_pitch_px;
	int dst_pitch_px;
	int clip_x0;
	int clip_y0;
	int clip_x1;
	int clip_y1;
	int dx;
	int dy;

	if (!renderer || !renderer->src || !vid.screen || !vid.screen->pixels)
		return;

	PLAT_computeRendererRects(renderer, &src_rect, &dst_rect);
	if (src_rect.w <= 0 || src_rect.h <= 0 || dst_rect.w <= 0 ||
			dst_rect.h <= 0)
		return;

	SDL_FillRect(vid.screen, NULL, 0);

	src_pixels = (const uint16_t *)renderer->src;
	dst_pixels = (uint16_t *)vid.screen->pixels;
	src_pitch_px = renderer->src_p / FIXED_BPP;
	dst_pitch_px = vid.screen->pitch / FIXED_BPP;

	clip_x0 = MAX(0, -dst_rect.x);
	clip_y0 = MAX(0, -dst_rect.y);
	clip_x1 = MIN(dst_rect.w, device_width - dst_rect.x);
	clip_y1 = MIN(dst_rect.h, device_height - dst_rect.y);
	if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1)
		return;

	for (dy = clip_y0; dy < clip_y1; dy++) {
		int sy = src_rect.y + ((dy * src_rect.h) / dst_rect.h);
		const uint16_t *src_row = src_pixels + (sy * src_pitch_px);
		uint16_t *dst_row = dst_pixels +
			((dst_rect.y + dy) * dst_pitch_px);

		for (dx = clip_x0; dx < clip_x1; dx++) {
			int sx = src_rect.x + ((dx * src_rect.w) / dst_rect.w);
			dst_row[dst_rect.x + dx] = src_row[sx];
		}
	}
}

SDL_Surface* PLAT_initVideo(void) {
	load_traits();
	
	int w = FIXED_WIDTH;
	int h = FIXED_HEIGHT;
	int p = FIXED_PITCH;
	if (MINIME_videoHDMIConnected()) {
		w = HDMI_WIDTH;
		h = HDMI_HEIGHT;
		p = HDMI_PITCH;
		on_hdmi = 1;
	}

	vid.use_direct_fb = 0;
	vid.fb_fd = -1;
	vid.fb_map = NULL;
	vid.fb_map_len = 0;
	if (!on_hdmi) {
		(void)PLAT_initDirectFB();
	}
	
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
		LOG_error("SDL video init failed: %s\n", SDL_GetError());
		exit(1);
	}
	SDL_ShowCursor(0);
	
	vid.window   = SDL_CreateWindow("", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w,h, SDL_WINDOW_SHOWN);
	if (!vid.window) {
		LOG_error("SDL window creation failed: %s\n", SDL_GetError());
		exit(1);
	}
	vid.renderer = SDL_CreateRenderer(vid.window,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC);
	if (!vid.renderer) {
		LOG_error("SDL renderer creation failed: %s\n", SDL_GetError());
		exit(1);
	}
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"1"); // linear
	vid.texture = SDL_CreateTexture(vid.renderer,SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w,h);
	if (!vid.texture) {
		LOG_error("SDL texture creation failed: %s\n", SDL_GetError());
		exit(1);
	}
	vid.target	= NULL; // only needed for non-native sizes
	
	vid.screen	= SDL_CreateRGBSurface(SDL_SWSURFACE, w,h, FIXED_DEPTH, RGBA_MASK_565);
	if (!vid.screen) {
		LOG_error("SDL screen surface creation failed: %s\n", SDL_GetError());
		exit(1);
	}
	vid.width	= w;
	vid.height	= h;
	vid.pitch	= p;
	
	device_width	= w;
	device_height	= h;
	device_pitch	= p;
	
	vid.sharpness = SHARPNESS_SOFT;
	
	return vid.screen;
}

void PLAT_quitVideo(void) {
	SDL_FreeSurface(vid.screen);
	if (vid.target) SDL_DestroyTexture(vid.target);
	if (vid.effect) SDL_DestroyTexture(vid.effect);
	SDL_DestroyTexture(vid.texture);
	SDL_DestroyRenderer(vid.renderer);
	SDL_DestroyWindow(vid.window);
	PLAT_quitDirectFB();

	SDL_Quit();
}

void PLAT_clearVideo(SDL_Surface* screen) {
	SDL_FillRect(screen, NULL, 0); // TODO: revisit
}
void PLAT_clearAll(void) {
	PLAT_clearVideo(vid.screen); // TODO: revist
	SDL_RenderClear(vid.renderer);
}

void PLAT_setVsync(int vsync) {
	(void)vsync;
}

static int hard_scale = 4; // TODO: base src size, eg. 160x144 can be 4

static void resizeVideo(int w, int h, int p) {
	if (w==vid.width && h==vid.height && p==vid.pitch) return;
	
	// TODO: minarch disables crisp (and nn upscale before linear downscale) when native
	
	if (w>=device_width && h>=device_height) hard_scale = 1;
	else if (h>=160) hard_scale = 2; // limits gba and up to 2x (seems sufficient for 640x480)
	else hard_scale = 4;

	SDL_DestroyTexture(vid.texture);
	if (vid.target) SDL_DestroyTexture(vid.target);
	
	SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, vid.sharpness==SHARPNESS_SOFT?"1":"0", SDL_HINT_OVERRIDE);
	vid.texture = SDL_CreateTexture(vid.renderer,SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w,h);
	
	if (vid.sharpness==SHARPNESS_CRISP) {
		SDL_SetHintWithPriority(SDL_HINT_RENDER_SCALE_QUALITY, "1", SDL_HINT_OVERRIDE);
		vid.target = SDL_CreateTexture(vid.renderer,SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_TARGET, w * hard_scale,h * hard_scale);
	}
	else {
		vid.target = NULL;
	}

	vid.width	= w;
	vid.height	= h;
	vid.pitch	= p;
}

SDL_Surface* PLAT_resizeVideo(int w, int h, int p) {
	resizeVideo(w,h,p);
	return vid.screen;
}

void PLAT_setVideoScaleClip(int x, int y, int width, int height) {
	(void)x;
	(void)y;
	(void)width;
	(void)height;
}
void PLAT_setNearestNeighbor(int enabled) {
	(void)enabled;
}
void PLAT_setSharpness(int sharpness) {
	if (vid.sharpness==sharpness) return;
	int p = vid.pitch;
	vid.pitch = 0;
	vid.sharpness = sharpness;
	resizeVideo(vid.width,vid.height,p);
}

static struct FX_Context {
	int scale;
	int type;
	int color;
	int next_scale;
	int next_type;
	int next_color;
	int live_type;
} effect = {
	.scale = 1,
	.next_scale = 1,
	.type = EFFECT_NONE,
	.next_type = EFFECT_NONE,
	.live_type = EFFECT_NONE,
	.color = 0,
	.next_color = 0,
};
static void rgb565_to_rgb888(uint32_t rgb565, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t red = (rgb565 >> 11) & 0x1F;
    uint8_t green = (rgb565 >> 5) & 0x3F;
    uint8_t blue = rgb565 & 0x1F;

    *r = (red << 3) | (red >> 2);
    *g = (green << 2) | (green >> 4);
    *b = (blue << 3) | (blue >> 2);
}
static void updateEffect(void) {
	if (effect.next_scale==effect.scale && effect.next_type==effect.type && effect.next_color==effect.color) return; // unchanged
	
	int live_scale = effect.scale;
	int live_color = effect.color;
	effect.scale = effect.next_scale;
	effect.type = effect.next_type;
	effect.color = effect.next_color;
	
	if (effect.type==EFFECT_NONE) return; // disabled
	if (effect.type==effect.live_type && effect.scale==live_scale && effect.color==live_color) return; // already loaded
	
	char* effect_path;
	int opacity = 128; // 1 - 1/2 = 50%
	if (effect.type==EFFECT_LINE) {
		if (effect.scale<3) {
			effect_path = RES_PATH "/line-2.png";
		}
		else if (effect.scale<4) {
			effect_path = RES_PATH "/line-3.png";
		}
		else if (effect.scale<5) {
			effect_path = RES_PATH "/line-4.png";
		}
		else if (effect.scale<6) {
			effect_path = RES_PATH "/line-5.png";
		}
		else if (effect.scale<8) {
			effect_path = RES_PATH "/line-6.png";
		}
		else {
			effect_path = RES_PATH "/line-8.png";
		}
	}
	else if (effect.type==EFFECT_GRID) {
		if (effect.scale<3) {
			effect_path = RES_PATH "/grid-2.png";
			opacity = 64; // 1 - 3/4 = 25%
		}
		else if (effect.scale<4) {
			effect_path = RES_PATH "/grid-3.png";
			opacity = 112; // 1 - 5/9 = ~44%
		}
		else if (effect.scale<5) {
			effect_path = RES_PATH "/grid-4.png";
			opacity = 144; // 1 - 7/16 = ~56%
		}
		else if (effect.scale<6) {
			effect_path = RES_PATH "/grid-5.png";
			opacity = 160; // 1 - 9/25 = ~64%
		}
		else if (effect.scale<8) {
			effect_path = RES_PATH "/grid-6.png";
			opacity = 112; // 1 - 5/9 = ~44%
		}
		else if (effect.scale<11) {
			effect_path = RES_PATH "/grid-8.png";
			opacity = 144; // 1 - 7/16 = ~56%
		}
		else {
			effect_path = RES_PATH "/grid-11.png";
			opacity = 136; // 1 - 57/121 = ~52%
		}
	}
	
	// LOG_info("effect: %s opacity: %i\n", effect_path, opacity);
	SDL_Surface* tmp = IMG_Load(effect_path);
	if (tmp) {
		if (effect.type==EFFECT_GRID) {
			if (effect.color) {
				// LOG_info("dmg color grid...\n");
			
				uint8_t r,g,b;
				rgb565_to_rgb888(effect.color,&r,&g,&b);
				// LOG_info("rgb %i,%i,%i\n",r,g,b);
				
				uint32_t* pixels = (uint32_t*)tmp->pixels;
				int width = tmp->w;
				int height = tmp->h;
				for (int y = 0; y < height; ++y) {
				    for (int x = 0; x < width; ++x) {
				        uint32_t pixel = pixels[y * width + x];
				        uint8_t _,a;
				        SDL_GetRGBA(pixel, tmp->format, &_, &_, &_, &a);
				        if (a) pixels[y * width + x] = SDL_MapRGBA(tmp->format, r,g,b, a);
				    }
				}
			}
		}
		
		if (vid.effect) SDL_DestroyTexture(vid.effect);
		vid.effect = SDL_CreateTextureFromSurface(vid.renderer, tmp);
		SDL_SetTextureAlphaMod(vid.effect, opacity);
		SDL_FreeSurface(tmp);
		effect.live_type = effect.type;
	}
	}
void PLAT_setEffect(int next_type) {
	effect.next_type = next_type;
}
void PLAT_setEffectColor(int next_color) {
	effect.next_color = next_color;
}
void PLAT_vsync(int remaining) {
	if (remaining>0) SDL_Delay(remaining);
}

scaler_t PLAT_getScaler(GFX_Renderer* renderer) {
	// LOG_info("getScaler for scale: %i\n", renderer->scale);
	effect.next_scale = renderer->scale;
	return scale1x1_c16;
}

void PLAT_blitRenderer(GFX_Renderer* renderer) {
	vid.blit = renderer;
	SDL_RenderClear(vid.renderer);
	resizeVideo(vid.blit->true_w,vid.blit->true_h,vid.blit->src_p);
}

void PLAT_flip(SDL_Surface* IGNORED, int ignored) {
	
	on_hdmi = GetHDMI(); // use settings instead of getInt(HDMI_STATE_PATH)
	
	if (!vid.blit) {
		resizeVideo(device_width,device_height,FIXED_PITCH); // !!!???
		if (vid.use_direct_fb && !on_hdmi) {
			PLAT_presentSurfaceDirectFB(vid.screen);
			return;
		}
		SDL_UpdateTexture(vid.texture,NULL,vid.screen->pixels,vid.screen->pitch);
		if (rotate && !on_hdmi) {
			int dx = 0;
			int dy = 0;
			if (rotate == 1) {
				dx = device_height;
				dy = 0;
			} else if (rotate == 2) {
				dx = device_width;
				dy = device_height;
			} else if (rotate == 3) {
				dx = 0;
				dy = device_width;
			}
			SDL_RenderCopyEx(vid.renderer,vid.texture,NULL,&(SDL_Rect){dx,dy,device_width,device_height},rotate*90,&(SDL_Point){0,0},SDL_FLIP_NONE);
		}
		else SDL_RenderCopy(vid.renderer, vid.texture, NULL,NULL);
		SDL_RenderPresent(vid.renderer);
		return;
	}

	if (vid.use_direct_fb && !on_hdmi) {
		PLAT_blitRendererDirectFB(vid.blit);
		PLAT_presentSurfaceDirectFB(vid.screen);
		vid.blit = NULL;
		return;
	}
	
	// uint32_t then = SDL_GetTicks();
	SDL_UpdateTexture(vid.texture,NULL,vid.blit->src,vid.blit->src_p);
	// LOG_info("blit blocked for %ims\n", SDL_GetTicks()-then);
	
	SDL_Texture* target = vid.texture;
	int x = vid.blit->src_x;
	int y = vid.blit->src_y;
	int w = vid.blit->src_w;
	int h = vid.blit->src_h;
	if (vid.sharpness==SHARPNESS_CRISP) {
		SDL_SetRenderTarget(vid.renderer,vid.target);
		SDL_RenderCopy(vid.renderer, vid.texture, NULL,NULL);
		SDL_SetRenderTarget(vid.renderer,NULL);
		x *= hard_scale;
		y *= hard_scale;
		w *= hard_scale;
		h *= hard_scale;
		target = vid.target;
	}
	
	SDL_Rect* src_rect = &(SDL_Rect){x,y,w,h};
	SDL_Rect* dst_rect = &(SDL_Rect){0,0,device_width,device_height};
	PLAT_computeRendererRects(vid.blit, src_rect, dst_rect);
	
	int ox,oy;
	oy = (device_width-device_height)/2;
	ox = -oy;
	if (rotate && !on_hdmi) SDL_RenderCopyEx(vid.renderer,target,src_rect,&(SDL_Rect){ox+dst_rect->x,oy+dst_rect->y,dst_rect->w,dst_rect->h},rotate*90,NULL,SDL_FLIP_NONE);
	else SDL_RenderCopy(vid.renderer, target, src_rect, dst_rect);
	
	updateEffect();
	if (vid.blit && effect.type!=EFFECT_NONE && vid.effect) {
		// ox = effect.scale - (dst_rect->x % effect.scale);
		// oy = effect.scale - (dst_rect->y % effect.scale);
		// if (ox==effect.scale) ox = 0;
		// if (oy==effect.scale) oy = 0;
		// LOG_info("rotate: %i ox: %i oy: %i\n", rotate, ox,oy);
		if (rotate && !on_hdmi) SDL_RenderCopyEx(vid.renderer,vid.effect,&(SDL_Rect){0,0,dst_rect->w,dst_rect->h},&(SDL_Rect){ox+dst_rect->x,oy+dst_rect->y,dst_rect->w,dst_rect->h},rotate*90,NULL,SDL_FLIP_NONE);
		else SDL_RenderCopy(vid.renderer, vid.effect, &(SDL_Rect){0,0,dst_rect->w,dst_rect->h},dst_rect);
	}
	
	// uint32_t then = SDL_GetTicks();
	SDL_RenderPresent(vid.renderer);
	// LOG_info("SDL_RenderPresent blocked for %ims\n", SDL_GetTicks()-then);
	vid.blit = NULL;
}

int PLAT_supportsOverscan(void) { return is_cubexx; }

///////////////////////////////

// TODO: 
#define OVERLAY_WIDTH PILL_SIZE // unscaled
#define OVERLAY_HEIGHT PILL_SIZE // unscaled
#define OVERLAY_BPP 4
#define OVERLAY_DEPTH 16
#define OVERLAY_PITCH (OVERLAY_WIDTH * OVERLAY_BPP) // unscaled
#define OVERLAY_RGBA_MASK 0x00ff0000,0x0000ff00,0x000000ff,0xff000000 // ARGB
static struct OVL_Context {
	SDL_Surface* overlay;
} ovl;

SDL_Surface* PLAT_initOverlay(void) {
	ovl.overlay = SDL_CreateRGBSurface(SDL_SWSURFACE, SCALE2(OVERLAY_WIDTH,OVERLAY_HEIGHT),OVERLAY_DEPTH,OVERLAY_RGBA_MASK);
	return ovl.overlay;
}
void PLAT_quitOverlay(void) {
	if (ovl.overlay) SDL_FreeSurface(ovl.overlay);
}
void PLAT_enableOverlay(int enable) {
	(void)enable;
}

///////////////////////////////

static int online = 0;
void PLAT_getBatteryStatus(int* is_charging, int* charge) {
	int i = -1;

	if (!is_charging || !charge)
		return;

	if (MINIME_powerGetBattery(is_charging, &i) != 0) {
		*is_charging = 0;
		i = 0;
	}

	// worry less about battery and more about the game you're playing
	     if (i>80) *charge = 100;
	else if (i>60) *charge =  80;
	else if (i>40) *charge =  60;
	else if (i>20) *charge =  40;
	else if (i>10) *charge =  20;
	else           *charge =  10;

	// wifi status, just hooking into the regular PWR polling
	if (MINIME_traitAvailable(traits->wifi_interface)) {
		char path[256];
		char status[16] = "";
		snprintf(path, sizeof(path), "/sys/class/net/%s/operstate",
			traits->wifi_interface);
		getFile(path, status, sizeof(status));
		online = prefixMatch("up", status);
	} else {
		online = 0;
	}
}

void PLAT_enableBacklight(int enable) {
	if (enable) {
		MINIME_videoBlank(0);
		SetBrightness(GetBrightness());
		MINIME_powerSetLED(0);
	}
	else {
		MINIME_videoBlank(1);
		SetRawBrightness(0);
		MINIME_powerSetLED(1);
	}
}

void PLAT_powerOff(void) {
	system("rm -f /tmp/minui_exec && sync");
	sleep(2);

	SetRawVolume(MUTE_VOLUME_RAW);
	PLAT_enableBacklight(0);
	MINIME_powerSetLED(1);
	SND_quit();
	VIB_quit();
	PWR_quit();
	GFX_quit();

	exit(0);
}

///////////////////////////////

void PLAT_setCPUSpeed(int speed) {
	MINIME_powerSetCPUSpeed(speed);
}

void PLAT_setRumble(int strength) {
	if (GetHDMI()) return; // assume we're using a controller?
	MINIME_powerSetRumble(strength ? 1 : 0);
}

int PLAT_pickSampleRate(int requested, int max) {
	return MIN(requested, max);
}

char* PLAT_getModel(void) {
	return traits ? (char *)traits->device_model : "Minime Handheld";
}

int PLAT_isOnline(void) {
	return online;
}

const char *PLAT_getDeviceId(void) {
	return traits ? traits->device_id : NULL;
}

int PLAT_hasButtonCZ(void) {
	return MINIME_inputHasCZ();
}
