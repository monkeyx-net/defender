#include "platform/video.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#define SCANLINE_SHADE 150 /* brightness of a scanline's dark rows, /255 */
#define AUTO_FILL 0.95     /* AUTO keeps INTEGER if it is at least this much of FIT */
#define MAX_SCANLINE_ROWS 16 /* output rows per arcade row in the scanline texture; stretched beyond */

static const char *const scale_names[VIDEO_SCALES] = { "auto", "integer", "fit", "stretch" };
static const char *const aspect_names[VIDEO_ASPECTS] = { "4:3", "square" };

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture, *scanlines_tex, *target;
static int tex_w, tex_h, scanlines_rows, target_w, target_h;
static int scale_mode = VIDEO_AUTO, aspect_mode = VIDEO_4_3;

/* The picture's width in square pixels at scale 1. */
static double picture_width(void)
{
    return aspect_mode == VIDEO_4_3 ? FB_HEIGHT * 4.0 / 3.0 : FB_WIDTH;
}

/* Whether AUTO picks INTEGER on this output. */
static bool auto_integer(int out_w, int out_h)
{
    double fit = out_w / picture_width() < (double)out_h / FB_HEIGHT ? out_w / picture_width()
                                                                    : (double)out_h / FB_HEIGHT;
    int whole = (int)fit;
    return whole >= 1 && whole >= fit * AUTO_FILL;
}

/* The picture's rectangle on an output of out_w x out_h. */
static SDL_Rect picture(int out_w, int out_h)
{
    if (scale_mode == VIDEO_STRETCH) return (SDL_Rect){ 0, 0, out_w, out_h };
    double pw = picture_width();
    double s = out_w / pw < (double)out_h / FB_HEIGHT ? out_w / pw : (double)out_h / FB_HEIGHT;
    bool whole = scale_mode == VIDEO_INTEGER || (scale_mode == VIDEO_AUTO && auto_integer(out_w, out_h));
    if (whole && s >= 1) s = (int)s;
    int w = (int)(pw * s + 0.5), h = (int)(FB_HEIGHT * s + 0.5);
    if (w > out_w) w = out_w;
    if (h > out_h) h = out_h;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    return (SDL_Rect){ (out_w - w) / 2, (out_h - h) / 2, w, h };
}

bool video_init(bool windowed)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* nearest */

    Uint32 flags = windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN_DESKTOP;
    window = SDL_CreateWindow("Defender", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              (int)(picture_width() * 3 + 0.5), FB_HEIGHT * 3, flags);
    if (!window) {
        SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
        return false;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) /* KMSDRM without GLES, or a dummy driver */
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
        return false;
    }
    SDL_ShowCursor(SDL_DISABLE);
    return true;
}

void video_set_mode(int scale, int aspect)
{
    if (scale >= 0 && scale < VIDEO_SCALES) scale_mode = scale;
    if (aspect >= 0 && aspect < VIDEO_ASPECTS) aspect_mode = aspect;
}

void video_get_mode(int *scale, int *aspect)
{
    *scale = scale_mode;
    *aspect = aspect_mode;
}

static int parse(const char *name, const char *const *names, int count)
{
    for (int i = 0; i < count; i++)
        if (!strcmp(names[i], name)) return i;
    return -1;
}

int video_parse_scale(const char *name)
{
    return parse(name, scale_names, VIDEO_SCALES);
}

int video_parse_aspect(const char *name)
{
    if (!strcmp(name, "4x3") || !strcmp(name, "43")) return VIDEO_4_3;
    return parse(name, aspect_names, VIDEO_ASPECTS);
}

static void output_size(int *w, int *h)
{
    *w = FB_WIDTH;
    *h = FB_HEIGHT;
    if (renderer) SDL_GetRendererOutputSize(renderer, w, h);
}

const char *video_describe(void)
{
    static char text[40];
    static const char *const upper[VIDEO_SCALES] = { "AUTO", "INTEGER", "FIT", "STRETCH" };
    int out_w, out_h;
    output_size(&out_w, &out_h);
    const char *aspect = scale_mode == VIDEO_STRETCH ? "" : aspect_mode == VIDEO_4_3 ? " 4:3" : " SQUARE";
    if (scale_mode == VIDEO_AUTO)
        snprintf(text, sizeof text, "AUTO (%s)%s", auto_integer(out_w, out_h) ? "INTEGER" : "FIT", aspect);
    else
        snprintf(text, sizeof text, "%s%s", upper[scale_mode], aspect);
    return text;
}

int video_display_scale(void)
{
    int out_w, out_h;
    output_size(&out_w, &out_h);
    SDL_Rect dst = picture(out_w, out_h);
    int scale = dst.h / FB_HEIGHT; /* the rest is scaled sharp bilinear */
    return scale < 1 ? 1 : scale;
}

/* A 1-px-wide column, `rows` per arcade row: the last third of each arcade row is dark. */
static void build_scanlines(int rows)
{
    if (scanlines_tex) SDL_DestroyTexture(scanlines_tex);
    scanlines_rows = rows;
    if (rows > MAX_SCANLINE_ROWS) rows = MAX_SCANLINE_ROWS;
    scanlines_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1,
                                      FB_HEIGHT * rows);
    if (!scanlines_tex) return;
    static Uint32 column[FB_HEIGHT * MAX_SCANLINE_ROWS];
    int dark = rows / 3 > 0 ? rows / 3 : 1;
    for (int y = 0; y < FB_HEIGHT * rows; y++) {
        Uint32 v = y % rows >= rows - dark ? SCANLINE_SHADE : 255;
        column[y] = 0xFF000000u | v << 16 | v << 8 | v;
    }
    SDL_UpdateTexture(scanlines_tex, NULL, column, (int)sizeof(Uint32));
    SDL_SetTextureBlendMode(scanlines_tex, SDL_BLENDMODE_MOD);
}

static void draw_scanlines(int rows, const SDL_Rect *dst)
{
    if (scanlines_rows != rows) build_scanlines(rows);
    if (scanlines_tex) SDL_RenderCopy(renderer, scanlines_tex, NULL, dst);
}

/* The render target the frame is scaled up into (nearest) before it is scaled
   down to the picture (linear); NULL if the renderer cannot. */
static SDL_Texture *prescale_target(int w, int h)
{
    if (target && target_w == w && target_h == h) return target;
    if (target) SDL_DestroyTexture(target);
    target = NULL;
    target_w = target_h = 0;
    SDL_RendererInfo info;
    if (!SDL_RenderTargetSupported(renderer) || SDL_GetRendererInfo(renderer, &info)) return NULL;
    if ((info.max_texture_width && w > info.max_texture_width) ||
        (info.max_texture_height && h > info.max_texture_height))
        return NULL;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1"); /* read when a texture is created */
    target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (target) {
        target_w = w;
        target_h = h;
    }
    return target;
}

void video_present(const Framebuffer *fb, bool scanlines)
{
    if (!texture || tex_w != fb->w || tex_h != fb->h) {
        if (texture) SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, fb->w, fb->h);
        tex_w = fb->w;
        tex_h = fb->h;
        if (!texture) {
            SDL_Log("SDL_CreateTexture: %s", SDL_GetError());
            return;
        }
    }
    SDL_UpdateTexture(texture, NULL, fb->px, fb->w * (int)sizeof(uint32_t));

    int out_w, out_h;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    SDL_Rect dst = picture(out_w, out_h);
    /* one output row per arcade row has no room for a scanline */
    bool lines = scanlines && dst.h >= FB_HEIGHT * 2;

    SDL_Texture *up = NULL;
    int px = 1, py = 1;
    if (dst.w % fb->w || dst.h % fb->h) { /* not a whole multiple: sharp bilinear */
        px = (dst.w + fb->w - 1) / fb->w;
        py = (dst.h + fb->h - 1) / fb->h;
        up = prescale_target(fb->w * px, fb->h * py);
    }
    if (up && !SDL_SetRenderTarget(renderer, up)) {
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        if (lines) draw_scanlines(fb->h * py / FB_HEIGHT, NULL);
        SDL_SetRenderTarget(renderer, NULL);
    } else {
        up = NULL; /* nearest straight to the picture (uneven pixels if not a multiple) */
    }

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, up ? up : texture, NULL, &dst);
    if (lines && !up) draw_scanlines(dst.h / FB_HEIGHT, &dst);
    SDL_RenderPresent(renderer);
}

void video_shutdown(void)
{
    if (target) SDL_DestroyTexture(target);
    if (scanlines_tex) SDL_DestroyTexture(scanlines_tex);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    target = scanlines_tex = texture = NULL;
    renderer = NULL;
    window = NULL;
    tex_w = tex_h = scanlines_rows = target_w = target_h = 0;
}
