#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ui/ui.h"

#define SCREEN_W 128
#define SCREEN_H 64

#define B_AIR 0
#define B_STONE 1
#define B_BEDROCK 2
#define B_WOOD 3
#define B_LEAVES 4
#define B_WATER 5

typedef struct {
    float x, y;
    int hp;
    float timer;
    bool active;
} craft_mob_t;

typedef struct {
    float px, py;
    float dirX, dirY;
    float planeX, planeY;
    float hp;
    int inv[6];
    float walk_time;
    float global_time;
    int hit_timer;
    bool keys[7];
    float zBuffer[SCREEN_W];
} craft_player_t;

// World
void craft_world_init(void);
void craft_world_free(void);
uint8_t craft_world_get_block(int x, int y);
void craft_world_set_block(int x, int y, uint8_t type);

// Mobs
void craft_mobs_init(void);
void craft_mobs_update(craft_player_t *p, float dtSec);
craft_mob_t* craft_mobs_get_all(void);

// Render
void craft_render_scene(ui_t *ui, craft_player_t *p, craft_mob_t *mobs);
