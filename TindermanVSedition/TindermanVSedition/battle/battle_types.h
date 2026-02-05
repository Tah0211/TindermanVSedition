// battle/battle_types.h
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define MAP_W 21
#define MAP_H 21

typedef enum { TEAM_P1=0, TEAM_P2=1 } Team;
typedef enum { SLOT_HERO=0, SLOT_GIRL=1 } Slot;

typedef struct { int8_t x, y; } Pos; // 0..20で十分

static inline int manhattan(Pos a, Pos b) {
    int dx = (int)a.x - (int)b.x; if (dx < 0) dx = -dx;
    int dy = (int)a.y - (int)b.y; if (dy < 0) dy = -dy;
    return dx + dy;
}

typedef struct {
    int hp, atk, spd, st;
} Stats;

typedef struct {
    Team team;
    Slot slot;
    const char* char_id;  // "hero" / "himari" / "kiritan" etc.
    Pos pos;
    Stats stats;
    int move;             // hero=4, himari=6, kiritan=3
    bool alive;
    bool tag_learned;     // girl only
} Unit;

static inline int unit_index(Team t, Slot s) {
    return (t==TEAM_P1) ? (s==SLOT_HERO?0:1) : (s==SLOT_HERO?2:3);
}
