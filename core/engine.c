#include "engine.h"
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>

SDL_Window *g_window = NULL;
SDL_Renderer *g_renderer = NULL;
bool g_running = false;

bool engine_init(void)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    // IMG
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
    {
        SDL_Log("IMG_Init failed: %s", IMG_GetError());
        return false;
    }

    // TTF
    if (TTF_Init() < 0)
    {
        SDL_Log("TTF_Init failed: %s", TTF_GetError());
        return false;
    }

    // Mixer
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0)
    {
        SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());
        return false;
    }

    Mix_AllocateChannels(32);
    Mix_Volume(-1, MIX_MAX_VOLUME);

    // ★ ウィンドウ作成時点でフルスクリーン（安定）
    g_window = SDL_CreateWindow(
        "TVSE",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_FULLSCREEN_DESKTOP // ← 重要
    );

    if (!g_window)
    {
        SDL_Log("Window create failed: %s", SDL_GetError());
        return false;
    }

    g_renderer = SDL_CreateRenderer(
        g_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!g_renderer)
    {
        SDL_Log("Renderer create failed: %s", SDL_GetError());
        return false;
    }

    // ★ 描画領域を1280×720に固定（背景が確実に全画面表示される）
    SDL_RenderSetLogicalSize(g_renderer, 1280, 720);

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);

    return true;
}

void engine_cleanup(void)
{
    Mix_CloseAudio();
    TTF_Quit();
    IMG_Quit();

    if (g_renderer)
        SDL_DestroyRenderer(g_renderer);
    if (g_window)
        SDL_DestroyWindow(g_window);

    SDL_Quit();
}
