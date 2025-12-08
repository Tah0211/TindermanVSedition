#ifndef ENGINE_H
#define ENGINE_H

#include <SDL2/SDL.h>
#include <stdbool.h>

extern SDL_Window* g_window;
extern SDL_Renderer* g_renderer;
extern bool g_running;

bool engine_init(void);
void engine_cleanup(void);

#endif
