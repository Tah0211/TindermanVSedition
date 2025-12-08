#ifndef UTIL_TIMER_H
#define UTIL_TIMER_H

#include <SDL2/SDL.h>

typedef struct {
    Uint32 start_ms;
    Uint32 duration_ms;
} SimpleTimer;

static inline void timer_start(SimpleTimer* t, Uint32 duration_ms)
{
    t->start_ms = SDL_GetTicks();
    t->duration_ms = duration_ms;
}

static inline Uint32 timer_remaining(SimpleTimer* t)
{
    Uint32 now = SDL_GetTicks();
    Uint32 elapsed = now - t->start_ms;
    if (elapsed >= t->duration_ms) return 0;
    return t->duration_ms - elapsed;
}

#endif
