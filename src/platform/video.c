#include "platform/video.h"

#include <SDL.h>

#define SCANLINE_SHADE 150 /* brightness of a scanline's dark rows, /255 */

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture, *scanlines_tex;
static int tex_w, tex_h, scanlines_scale;

bool video_init(bool windowed)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* nearest */

    Uint32 flags = windowed ? SDL_WINDOW_RESIZABLE : SDL_WINDOW_FULLSCREEN_DESKTOP;
    window = SDL_CreateWindow("Defender", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              FB_WIDTH * 3, FB_HEIGHT * 3, flags);
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

int video_display_scale(void)
{
    int out_w, out_h;
    SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
    int scale = out_w / FB_WIDTH < out_h / FB_HEIGHT ? out_w / FB_WIDTH : out_h / FB_HEIGHT;
    return scale < 1 ? 1 : scale;
}

/* A 1-px-wide column, one row per output row: the last third of each arcade row is dark. */
static void build_scanlines(int scale)
{
    if (scanlines_tex) SDL_DestroyTexture(scanlines_tex);
    scanlines_scale = scale;
    if (scale > 16) scale = 16; /* stretched beyond that */
    scanlines_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1,
                                      FB_HEIGHT * scale);
    if (!scanlines_tex) return;
    static Uint32 rows[FB_HEIGHT * 16];
    int dark = scale / 3 > 0 ? scale / 3 : 1;
    for (int y = 0; y < FB_HEIGHT * scale; y++) {
        Uint32 v = y % scale >= scale - dark ? SCANLINE_SHADE : 255;
        rows[y] = 0xFF000000u | v << 16 | v << 8 | v;
    }
    SDL_UpdateTexture(scanlines_tex, NULL, rows, (int)sizeof(Uint32));
    SDL_SetTextureBlendMode(scanlines_tex, SDL_BLENDMODE_MOD);
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
    int scale = video_display_scale();
    SDL_Rect dst = { (out_w - FB_WIDTH * scale) / 2, (out_h - FB_HEIGHT * scale) / 2,
                     FB_WIDTH * scale, FB_HEIGHT * scale };

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    if (scanlines && scale >= 2) { /* one output row per arcade row has no room for a scanline */
        if (scanlines_scale != scale) build_scanlines(scale);
        if (scanlines_tex) SDL_RenderCopy(renderer, scanlines_tex, NULL, &dst);
    }
    SDL_RenderPresent(renderer);
}

void video_shutdown(void)
{
    if (scanlines_tex) SDL_DestroyTexture(scanlines_tex);
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    scanlines_tex = texture = NULL;
    renderer = NULL;
    window = NULL;
    tex_w = tex_h = scanlines_scale = 0;
}
