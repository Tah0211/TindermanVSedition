#include "ui_text.h"
#include <string.h>
#include <stdio.h>

#define UI_TEXT_CACHE_MAX 256
#define UI_TEXT_KEY_MAX   192

typedef struct {
    bool used;
    TTF_Font *font;
    SDL_Color col;
    char key[UI_TEXT_KEY_MAX];   // text のコピー
    SDL_Texture *tex;
    int w, h;
    Uint32 last_used_tick;
} UiTextCacheEntry;

static UiTextCacheEntry g_cache[UI_TEXT_CACHE_MAX];
static SDL_Renderer *g_renderer_for_cache = NULL;
static Uint32 g_tick = 1;

static bool color_equal(SDL_Color a, SDL_Color b){
    return a.r==b.r && a.g==b.g && a.b==b.b && a.a==b.a;
}

TTF_Font *ui_load_font(const char *path, int size)
{
    TTF_Font *f = TTF_OpenFont(path, size);
    if (!f) SDL_Log("Failed to load font %s : %s", path, TTF_GetError());
    return f;
}

void ui_text_cache_init(SDL_Renderer *r)
{
    g_renderer_for_cache = r;
    memset(g_cache, 0, sizeof(g_cache));
    g_tick = 1;
}

static void entry_destroy(UiTextCacheEntry *e)
{
    if (e->tex) SDL_DestroyTexture(e->tex);
    e->tex = NULL;
    e->used = false;
}

void ui_text_cache_clear(void)
{
    for (int i = 0; i < UI_TEXT_CACHE_MAX; i++) {
        if (g_cache[i].used) entry_destroy(&g_cache[i]);
    }
}

void ui_text_cache_shutdown(void)
{
    ui_text_cache_clear();
    g_renderer_for_cache = NULL;
}

static int find_entry(TTF_Font *font, const char *text, SDL_Color col)
{
    for (int i = 0; i < UI_TEXT_CACHE_MAX; i++) {
        UiTextCacheEntry *e = &g_cache[i];
        if (!e->used) continue;
        if (e->font != font) continue;
        if (!color_equal(e->col, col)) continue;
        if (strcmp(e->key, text) != 0) continue;
        return i;
    }
    return -1;
}

static int alloc_entry_slot(void)
{
    // 空きを探す
    for (int i = 0; i < UI_TEXT_CACHE_MAX; i++) {
        if (!g_cache[i].used) return i;
    }
    // 無ければ最も古いものを追い出す（簡易LRU）
    int oldest = 0;
    Uint32 best = g_cache[0].last_used_tick;
    for (int i = 1; i < UI_TEXT_CACHE_MAX; i++) {
        if (g_cache[i].last_used_tick < best) {
            best = g_cache[i].last_used_tick;
            oldest = i;
        }
    }
    entry_destroy(&g_cache[oldest]);
    return oldest;
}

static SDL_Texture* build_texture(SDL_Renderer *r, TTF_Font *font,
                                 const char *text, SDL_Color col, int *out_w, int *out_h)
{
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, col);
    if (!surf) return NULL;

    SDL_Texture *tex = SDL_CreateTextureFromSurface(r, surf);
    if (tex) {
        *out_w = surf->w;
        *out_h = surf->h;
    }
    SDL_FreeSurface(surf);
    return tex;
}

void ui_text_draw_color(SDL_Renderer *r, TTF_Font *font, const char *text,
                        int x, int y, SDL_Color col)
{
    if (!r || !font || !text || !text[0]) return;

    // renderer が変わったらキャッシュ破棄（別rendererのtextureは使えない）
    if (g_renderer_for_cache != r) {
        ui_text_cache_clear();
        g_renderer_for_cache = r;
    }

    // 長すぎる text はキャッシュせず直描き（安全策）
    if ((int)strlen(text) >= UI_TEXT_KEY_MAX - 1) {
        int w=0,h=0;
        SDL_Texture *tmp = build_texture(r, font, text, col, &w, &h);
        if (!tmp) return;
        SDL_Rect dst = {x, y, w, h};
        SDL_RenderCopy(r, tmp, NULL, &dst);
        SDL_DestroyTexture(tmp);
        return;
    }

    int idx = find_entry(font, text, col);
    if (idx < 0) {
        idx = alloc_entry_slot();
        UiTextCacheEntry *e = &g_cache[idx];

        int w=0,h=0;
        SDL_Texture *tex = build_texture(r, font, text, col, &w, &h);
        if (!tex) return;

        e->used = true;
        e->font = font;
        e->col = col;
        strcpy(e->key, text);
        e->tex = tex;
        e->w = w;
        e->h = h;
    }

    UiTextCacheEntry *e = &g_cache[idx];
    e->last_used_tick = g_tick++;

    SDL_Rect dst = {x, y, e->w, e->h};
    SDL_RenderCopy(r, e->tex, NULL, &dst);
}

void ui_text_draw(SDL_Renderer *r, TTF_Font *font, const char *text, int x, int y)
{
    SDL_Color white = {255,255,255,255};
    ui_text_draw_color(r, font, text, x, y, white);
}
