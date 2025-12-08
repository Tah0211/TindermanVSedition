#include "http_client.h"
#include <SDL2/SDL.h>

void http_post_phase_done(const char *phase)
{
    SDL_Log("[MOCK] phase done: %s", phase);
}
