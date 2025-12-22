#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

#include "core/engine.h"
#include "core/scene_manager.h"
#include "core/input.h"

int main(int argc, char **argv)
{

    // -------------------------------
    // SDL2 本体の初期化
    // -------------------------------
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        SDL_Log("SDL_Init ERROR: %s", SDL_GetError());
        return 1;
    }

    // -------------------------------
    // SDL_mixer の初期化
    // -------------------------------
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0)
    {
        SDL_Log("Mix_OpenAudio ERROR: %s", Mix_GetError());
        return 1;
    }

    Mix_AllocateChannels(32);          // チャンネル数確保
    Mix_Volume(-1, SDL_MIX_MAXVOLUME); // 全チャンネル音量MAX

    // -------------------------------
    // エンジン初期化
    // -------------------------------
    if (!engine_init())
    {
        SDL_Log("Engine init failed");
        return 1;
    }

    scene_manager_init(); // シーン切替

    Uint32 last = SDL_GetTicks();
    g_running = true;

    // -------------------------------
    // メインループ
    // -------------------------------
    while (g_running)
    {

        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        last = now;

        input_update();
        scene_update(dt);

        SDL_RenderClear(g_renderer);
        scene_render(g_renderer);
        SDL_RenderPresent(g_renderer);

        SDL_Delay(1); // CPU負荷軽減
    }

    // -------------------------------
    // 終了処理
    // -------------------------------
    Mix_CloseAudio(); // SDL_mixer 終了
    engine_cleanup(); // エンジン終了
    SDL_Quit();       // SDL 全終了

    return 0;
}
