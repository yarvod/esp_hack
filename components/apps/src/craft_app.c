#include "core/app_manager.h"
#include "core/context.h"
#include "ui/ui.h"
#include "craft/craft_engine.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    core_screen_t s;
    craft_player_t p;
} app_st_t;

static app_st_t *st = NULL;

static void break_block(void) {
    float rayDirX = st->p.dirX;
    float rayDirY = st->p.dirY;
    int mapX = (int)st->p.px;
    int mapY = (int)st->p.py;
    float sideDistX, sideDistY;
    float deltaDistX = (rayDirX == 0) ? 1e30f : fabsf(1.0f / rayDirX);
    float deltaDistY = (rayDirY == 0) ? 1e30f : fabsf(1.0f / rayDirY);
    int stepX, stepY;
    
    if(rayDirX < 0) { stepX = -1; sideDistX = (st->p.px - mapX) * deltaDistX; }
    else { stepX = 1; sideDistX = (mapX + 1.0f - st->p.px) * deltaDistX; }
    if(rayDirY < 0) { stepY = -1; sideDistY = (st->p.py - mapY) * deltaDistY; }
    else { stepY = 1; sideDistY = (mapY + 1.0f - st->p.py) * deltaDistY; }
    
    for (int i=0; i<5; i++) { 
        if(sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; }
        else { sideDistY += deltaDistY; mapY += stepY; }
        
        int b = craft_world_get_block(mapX, mapY);
        if (b > 0 && b != B_BEDROCK && b != B_WATER) {
            if (b < 6) st->p.inv[b]++;
            craft_world_set_block(mapX, mapY, B_AIR); 
            return;
        }
    }
}

static void hit_mob(void) {
    craft_mob_t* mobs = craft_mobs_get_all();
    for(int i=0; i<5; i++) {
        if (mobs[i].active && mobs[i].hp > 0) {
            float dx = mobs[i].x - st->p.px;
            float dy = mobs[i].y - st->p.py;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist < 3.0f) {
                float dot = (dx/dist)*st->p.dirX + (dy/dist)*st->p.dirY;
                if (dot > 0.8f) {
                    mobs[i].hp--;
                    float nx = mobs[i].x + st->p.dirX * 0.8f;
                    float ny = mobs[i].y + st->p.dirY * 0.8f;
                    if (craft_world_get_block((int)nx, (int)ny) == B_AIR) {
                        mobs[i].x = nx; mobs[i].y = ny;
                    }
                }
            }
        }
    }
}

static void craft_up(core_context_t *ctx, core_screen_t *sc, uint32_t dt) {
    if (!st) return;
    float dtSec = dt / 1000.0f;
    st->p.global_time += dtSec;
    
    float moveSpeed = 4.0f * dtSec;
    float rotSpeed = 3.0f * dtSec;
    bool moved = false;
    
    if (st->p.keys[CORE_INPUT_UP]) {
        float nx = st->p.px + st->p.dirX * moveSpeed;
        float ny = st->p.py + st->p.dirY * moveSpeed;
        if(craft_world_get_block((int)nx, (int)st->p.py) == B_AIR || craft_world_get_block((int)nx, (int)st->p.py) == B_WATER) st->p.px = nx;
        if(craft_world_get_block((int)st->p.px, (int)ny) == B_AIR || craft_world_get_block((int)st->p.px, (int)ny) == B_WATER) st->p.py = ny;
        moved = true;
    }
    if (st->p.keys[CORE_INPUT_DOWN]) {
        float nx = st->p.px - st->p.dirX * moveSpeed;
        float ny = st->p.py - st->p.dirY * moveSpeed;
        if(craft_world_get_block((int)nx, (int)st->p.py) == B_AIR || craft_world_get_block((int)nx, (int)st->p.py) == B_WATER) st->p.px = nx;
        if(craft_world_get_block((int)st->p.px, (int)ny) == B_AIR || craft_world_get_block((int)st->p.px, (int)ny) == B_WATER) st->p.py = ny;
        moved = true;
    }
    
    if (moved) st->p.walk_time += moveSpeed * 3.0f;
    
    if (st->p.keys[CORE_INPUT_LEFT]) {
        float a = -rotSpeed; // CW
        float oldDirX = st->p.dirX;
        st->p.dirX = st->p.dirX * cosf(a) - st->p.dirY * sinf(a);
        st->p.dirY = oldDirX * sinf(a) + st->p.dirY * cosf(a);
        float oldPlaneX = st->p.planeX;
        st->p.planeX = st->p.planeX * cosf(a) - st->p.planeY * sinf(a);
        st->p.planeY = oldPlaneX * sinf(a) + st->p.planeY * cosf(a);
    }
    if (st->p.keys[CORE_INPUT_RIGHT]) {
        float a = rotSpeed; // CCW
        float oldDirX = st->p.dirX;
        st->p.dirX = st->p.dirX * cosf(a) - st->p.dirY * sinf(a);
        st->p.dirY = oldDirX * sinf(a) + st->p.dirY * cosf(a);
        float oldPlaneX = st->p.planeX;
        st->p.planeX = st->p.planeX * cosf(a) - st->p.planeY * sinf(a);
        st->p.planeY = oldPlaneX * sinf(a) + st->p.planeY * cosf(a);
    }
    
    if (st->p.hit_timer > 0) {
        st->p.hit_timer -= dt;
        if (st->p.hit_timer < 0) st->p.hit_timer = 0;
    }
    
    craft_mobs_update(&st->p, dtSec);
    core_nav_mark_dirty(&ctx->nav);
}

static bool craft_in(core_context_t *ctx, core_screen_t *sc, const core_input_event_t *e) {
    if (!st) return false;
    if (e->action == CORE_INPUT_BACK && e->phase == CORE_INPUT_PHASE_PRESS) {
        core_nav_pop(ctx, &ctx->nav);
        craft_world_free();
        free(st); st = NULL;
        return true;
    }
    if (e->action < 7) {
        if (e->phase == CORE_INPUT_PHASE_PRESS) {
            st->p.keys[e->action] = true;
            if (e->action == CORE_INPUT_SELECT && st->p.hit_timer == 0) {
                st->p.hit_timer = 200;
                break_block();
                hit_mob();
            }
        } else if (e->phase == CORE_INPUT_PHASE_RELEASE) {
            st->p.keys[e->action] = false;
        }
        return true;
    }
    return false;
}

static void craft_r(core_context_t *ctx, core_screen_t *sc, ui_t *ui) {
    if (!st) return;
    craft_render_scene(ui, &st->p, craft_mobs_get_all());
}

static esp_err_t craft_launch(core_context_t *ctx) {
    if (st) return ESP_FAIL; 
    st = malloc(sizeof(app_st_t));
    if (!st) return ESP_ERR_NO_MEM;
    memset(st, 0, sizeof(app_st_t));
    
    st->s.id = "craft";
    st->s.on_input = craft_in;
    st->s.on_update = craft_up;
    st->s.on_render = craft_r;
    
    craft_world_init();
    craft_mobs_init();
    
    st->p.px = 2.5f; st->p.py = 2.5f;
    st->p.dirX = 1.0f; st->p.dirY = 0.0f;
    st->p.planeX = 0.0f; st->p.planeY = 0.8f;
    st->p.hp = 100.0f;
    
    return core_nav_push(ctx, &ctx->nav, &st->s);
}

const core_app_descriptor_t g_craft_app = { .id = "craft", .name = "Craft 3D", .icon = "C", .launch = craft_launch };
