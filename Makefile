CC = gcc
CFLAGS = -Wall -O2 -std=c11 `sdl2-config --cflags`
LDFLAGS = `sdl2-config --libs` -lSDL2_image -lSDL2_ttf -lSDL2_mixer

SRC = \
    main.c \
    core/engine.c \
    core/scene_manager.c \
    core/input.c \
    scenes/1_scene_home.c \
    scenes/2_scene_select.c \
    scenes/3_scene_chat.c \
    ui/ui_button.c \
    ui/ui_card.c \
    ui/ui_text.c \
    net/net_mock.c \
    util/texture.c \
    util/timer.c \
    util/json.o \
    net/http_client.o \
    -lm


OBJ = $(SRC:.c=.o)

TARGET = tvse

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean

