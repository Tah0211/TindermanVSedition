// battle/battle_cmd.c
#include "battle_cmd.h"

static bool pos_in_bounds(Pos p){ return p.x>=0 && p.x<MAP_W && p.y>=0 && p.y<MAP_H; }

bool battle_cmd_validate(const TurnCmd* c){
    if(!c) return false;
    for(int i=0;i<2;i++){
        const UnitCmd* u=&c->cmd[i];
        if(u->has_move && !pos_in_bounds(u->move_to)) return false;
        if(u->skill_index < -1 || u->skill_index > 3) return false;
        if(u->target != -1 && (u->target < 0 || u->target > 1)) return false;
    }
    return true;
}

bool battle_cmd_pack(const TurnCmd* in, uint8_t out[TURNCMD_WIRE_BYTES]){
    if(!in||!out) return false;
    if(!battle_cmd_validate(in)) return false;

    // fixed layout:
    // [0] hero.has_move (0/1)
    // [1] hero.x
    // [2] hero.y
    // [3] hero.skill
    // [4] hero.target
    // [5] girl.has_move
    // [6] girl.x
    // [7] girl.y
    // [8] girl.skill
    // [9] girl.target
    const UnitCmd* h=&in->cmd[SLOT_HERO];
    const UnitCmd* g=&in->cmd[SLOT_GIRL];

    out[0]=h->has_move?1:0;
    out[1]=(uint8_t)h->move_to.x;
    out[2]=(uint8_t)h->move_to.y;
    out[3]=(uint8_t)(h->skill_index+1); // -1..3 -> 0..4
    out[4]=(uint8_t)(h->target+1);      // -1..1 -> 0..2

    out[5]=g->has_move?1:0;
    out[6]=(uint8_t)g->move_to.x;
    out[7]=(uint8_t)g->move_to.y;
    out[8]=(uint8_t)(g->skill_index+1);
    out[9]=(uint8_t)(g->target+1);
    return true;
}

bool battle_cmd_unpack(const uint8_t in[TURNCMD_WIRE_BYTES], TurnCmd* out){
    if(!in||!out) return false;

    UnitCmd* h=&out->cmd[SLOT_HERO];
    UnitCmd* g=&out->cmd[SLOT_GIRL];

    h->has_move = in[0]?true:false;
    h->move_to  = (Pos){ (int8_t)in[1], (int8_t)in[2] };
    h->skill_index = (int8_t)in[3]-1;
    h->target      = (int8_t)in[4]-1;

    g->has_move = in[5]?true:false;
    g->move_to  = (Pos){ (int8_t)in[6], (int8_t)in[7] };
    g->skill_index = (int8_t)in[8]-1;
    g->target      = (int8_t)in[9]-1;

    return battle_cmd_validate(out);
}
