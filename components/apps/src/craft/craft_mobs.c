#include "craft_engine.h"
#include "esp_random.h"
#include <math.h>

static craft_mob_t mobs[5];

void craft_mobs_init(void) {
    for(int i=0; i<5; i++) {
        mobs[i].active = false;
        mobs[i].timer = 0;
    }
}

void craft_mobs_update(craft_player_t *p, float dtSec) {
    for(int i=0; i<5; i++) {
        if (!mobs[i].active || mobs[i].hp <= 0) {
            // Spawn new mob near player
            float ang = (esp_random() % 360) * 3.14159f / 180.0f;
            float r = 12.0f + (esp_random() % 8);
            int mx = (int)(p->px + cosf(ang) * r);
            int my = (int)(p->py + sinf(ang) * r);
            if (craft_world_get_block(mx, my) == B_AIR) {
                mobs[i].x = mx + 0.5f;
                mobs[i].y = my + 0.5f;
                mobs[i].hp = 3;
                mobs[i].active = true;
            }
            continue;
        }
        
        float dx = p->px - mobs[i].x;
        float dy = p->py - mobs[i].y;
        float dist = sqrtf(dx*dx + dy*dy);
        
        if (dist > 30.0f) {
            mobs[i].active = false; // Despawn if too far
            continue;
        }
        
        mobs[i].timer += dtSec;
        if (mobs[i].timer > 0.2f) { // move tick
            mobs[i].timer = 0;
            if (dist < 12.0f && dist > 1.0f) {
                float nx = mobs[i].x + (dx/dist)*0.4f;
                float ny = mobs[i].y + (dy/dist)*0.4f;
                if (craft_world_get_block((int)nx, (int)ny) == B_AIR) {
                    mobs[i].x = nx;
                    mobs[i].y = ny;
                }
            } else if (dist <= 1.0f) {
                p->hp -= 5.0f;
                if (p->hp <= 0) {
                    p->px = 2.5f; p->py = 2.5f; p->hp = 100.0f; // Respawn
                }
            }
        }
    }
}

craft_mob_t* craft_mobs_get_all(void) {
    return mobs;
}
