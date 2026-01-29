#ifndef INPUT_H
#define INPUT_H

#include <SDL2/SDL.h>
#include <stdbool.h>

// 毎フレーム最初に呼ぶ（前フレーム→今フレームの準備）
void input_begin_frame(void);

// main の PollEvent ループ内で、拾ったイベントを渡す
void input_handle_event(const SDL_Event *e);

// 毎フレーム PollEvent 後に呼ぶ（SDL_GetKeyboardState 反映など）
void input_end_frame(void);

bool input_is_pressed(SDL_Scancode sc);
bool input_is_down(SDL_Scancode sc);
bool input_is_released(SDL_Scancode sc);

const char *input_get_text(void);

// フォーカス喪失時などに必要なら手動クリア
void input_clear(void);

#endif
