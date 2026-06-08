#include "craft_engine.h"
#include <stdlib.h>
#include <string.h>

typedef struct { int x, y; uint8_t type; } mod_block_t;
static mod_block_t *mods = NULL;
static int mod_count = 0;
static int mod_cap = 0;

void craft_world_init(void) {
    mod_cap = 256;
    mods = malloc(sizeof(mod_block_t) * mod_cap);
    mod_count = 0;
}

void craft_world_free(void) {
    if (mods) free(mods);
    mods = NULL;
}

static uint32_t hash(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393U + (uint32_t)y * 668265263U;
    h = (h ^ (h >> 13)) * 1274126177U;
    return h ^ (h >> 16);
}

uint8_t craft_world_get_block(int x, int y) {
    // Limits of the "infinite" world to prevent overflow artifacts
    if (x < -1000 || x > 1000 || y < -1000 || y > 1000) return B_BEDROCK;
    
    // Check modified blocks
    if (mods) {
        for (int i=0; i<mod_count; i++) {
            if (mods[i].x == x && mods[i].y == y) return mods[i].type;
        }
    }
    
    // Spawn clearing
    if (x >= 0 && x <= 4 && y >= 0 && y <= 4) return B_AIR;
    
    uint32_t h = hash(x, y);
    float n = (h % 1000) / 1000.0f;
    
    // Trees
    if (n < 0.03f) return B_WOOD; // Tree trunk
    if ((hash(x-1, y)%1000) < 30 || (hash(x+1, y)%1000) < 30 || 
        (hash(x, y-1)%1000) < 30 || (hash(x, y+1)%1000) < 30) return B_LEAVES;
        
    // Ponds & Lakes
    if (hash(x/5, y/5) % 100 < 15) return B_WATER; 
    
    // Stone outcrops
    if (hash(x/3, y/3) % 100 < 20) return B_STONE; 
    
    return B_AIR;
}

void craft_world_set_block(int x, int y, uint8_t type) {
    if (!mods) return;
    for (int i=0; i<mod_count; i++) {
        if (mods[i].x == x && mods[i].y == y) {
            mods[i].type = type;
            return;
        }
    }
    if (mod_count < mod_cap) {
        mods[mod_count].x = x;
        mods[mod_count].y = y;
        mods[mod_count].type = type;
        mod_count++;
    }
}
