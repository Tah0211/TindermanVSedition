#ifndef UI_BUTTON_H
#define UI_BUTTON_H

#include <SDL2/SDL.h>
#include <stdbool.h>

void ui_button_draw(SDL_Renderer *r, SDL_Rect rect, bool focused);

#endif
