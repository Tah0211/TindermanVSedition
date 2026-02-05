#include "ui_card.h"

void ui_card_draw(SDL_Renderer* r, UiCard* c, int focused)
{
    (void)focused;
    SDL_SetRenderDrawColor(r, 180, 180, 180, 255);
    SDL_RenderDrawRect(r, &c->rect);
}
