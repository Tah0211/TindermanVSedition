#include "ui_button.h"

void ui_button_draw(SDL_Renderer *r, SDL_Rect rect, bool focused)
{
    if (focused)
    {
        // 選択時：明るく＋枠を太めに
        SDL_SetRenderDrawColor(r, 255, 255, 255, 60);
        SDL_RenderFillRect(r, &rect);

        SDL_SetRenderDrawColor(r, 255, 255, 255, 200);
        SDL_RenderDrawRect(r, &rect);

        // 枠をもう1段階太くする
        SDL_Rect inner = {rect.x + 2, rect.y + 2, rect.w - 4, rect.h - 4};
        SDL_RenderDrawRect(r, &inner);
    }
    else
    {
        // 非選択時：薄い状態
        SDL_SetRenderDrawColor(r, 255, 255, 255, 25);
        SDL_RenderFillRect(r, &rect);

        SDL_SetRenderDrawColor(r, 255, 255, 255, 80);
        SDL_RenderDrawRect(r, &rect);
    }
}
