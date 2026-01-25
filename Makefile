CC = gcc
CFLAGS = -Wall -O2 -std=c11 `sdl2-config --cflags`
LDFLAGS = `sdl2-config --libs` -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm

# ===============================
# ソースファイル一覧
# ===============================
SRC = \
    main.c \
    core/engine.c \
    core/scene_manager.c \
    core/input.c \
    \
    scenes/1_scene_home.c \
    scenes/2_scene_select.c \
    scenes/3_scene_chat.c \
    scenes/4_scene_allocate.c \
    scenes/5_scene_battle.c \
    \
    ui/ui_button.c \
    ui/ui_card.c \
    ui/ui_text.c \
    \
    util/texture.c \
    util/timer.c \
    util/json.c

# .c → .o 変換
OBJ = $(SRC:.c=.o)

TARGET = tvse

# ===============================
# ルール
# ===============================
all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
