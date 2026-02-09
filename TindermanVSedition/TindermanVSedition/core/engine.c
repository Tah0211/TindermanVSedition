// core/engine.c
#include "engine.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>

SDL_Window *g_window = NULL;
SDL_Renderer *g_renderer = NULL;
bool g_running = false;

static bool g_img_inited = false;
static bool g_ttf_inited = false;
static bool g_mix_inited = false;

bool engine_init(void)
{
    // SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    // IMG (PNG)
    if (!(IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG))
    {
        SDL_Log("IMG_Init failed: %s", IMG_GetError());
        SDL_Quit();
        return false;
    }
    g_img_inited = true;

    // TTF
    if (TTF_Init() < 0)
    {
        SDL_Log("TTF_Init failed: %s", TTF_GetError());
        IMG_Quit();
        SDL_Quit();
        return false;
    }
    g_ttf_inited = true;

    // Mixer
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0)
    {
        SDL_Log("Mix_OpenAudio failed: %s", Mix_GetError());
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return false;
    }
    g_mix_inited = true;

    Mix_AllocateChannels(32);
    Mix_Volume(-1, MIX_MAX_VOLUME);

    // Window
    g_window = SDL_CreateWindow(
        "TVSE",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_SHOWN);
    if (!g_window)
    {
        SDL_Log("Window create failed: %s", SDL_GetError());
        engine_cleanup();
        return false;
    }

    // Renderer
    g_renderer = SDL_CreateRenderer(
        g_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_renderer)
    {
        SDL_Log("Renderer create failed: %s", SDL_GetError());
        engine_cleanup();
        return false;
    }

    // Logical size (固定 1280x720)
    SDL_RenderSetLogicalSize(g_renderer, 1280, 720);

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);

    return true;
}

void engine_cleanup(void)
{
    // Renderer / Window
    if (g_renderer)
    {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = NULL;
    }
    if (g_window)
    {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }

    // Mixer
    if (g_mix_inited)
    {
        Mix_CloseAudio();
        g_mix_inited = false;
    }

    // TTF
    if (g_ttf_inited)
    {
        TTF_Quit();
        g_ttf_inited = false;
    }

    // IMG
    if (g_img_inited)
    {
        IMG_Quit();
        g_img_inited = false;
    }

    SDL_Quit();
}
