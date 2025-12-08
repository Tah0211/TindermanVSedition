#ifndef INPUT_H
#define INPUT_H

#include <SDL2/SDL.h>
#include <stdbool.h>

void input_update(void);
bool input_is_pressed(SDL_Scancode sc);
bool input_is_down(SDL_Scancode sc);
bool input_is_released(SDL_Scancode sc);

#endif
