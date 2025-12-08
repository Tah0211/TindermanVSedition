#ifndef UI_TEXT_H
#define UI_TEXT_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

TTF_Font *ui_load_font(const char *path, int size);
void ui_text_draw(SDL_Renderer *r, TTF_Font *font,
                  const char *text, int x, int y);

#endif
