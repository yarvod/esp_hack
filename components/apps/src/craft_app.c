#include "core/app_manager.h"
#include "core/context.h"
#include "ui/ui.h"
#include "esp_random.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define MAP_W 64
#define MAP_H 64
#define SCREEN_W 128
#define SCREEN_H 64

#define B_AIR 0
#define B_STONE 1
#define B_BEDROCK 2
#define B_WOOD 3
#define B_LEAVES 4
#define B_WATER 5

// 8x8 Textures
static const uint8_t TEX[6][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0: AIR
    {0xFE, 0x82, 0x82, 0xFE, 0x28, 0x28, 0xFE, 0x82}, // 1: STONE (Bricks)
    {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55}, // 2: BEDROCK (Checkerboard)
    {0x55, 0xDD, 0x55, 0x77, 0x55, 0xDD, 0x55, 0x77}, // 3: WOOD (Bark)
    {0x8A, 0x05, 0x50, 0xA8, 0x05, 0x50, 0x8A, 0x05}, // 4: LEAVES (Sparse dots)
    {0x00, 0x3C, 0x00, 0x3C, 0x00, 0x3C, 0x00, 0x3C}  // 5: WATER (Waves)
};

static const uint8_t CREEPER[8] = {
    0x00, 0x00, 0x24, 0x24, 0x18, 0x3C, 0x24, 0x00 // Creeper face
};

const char* PICKAXE[16] = {
    "      XXXXXX    ",
    "    XXXXXXXXX   ",
    "   XXXXX  XXXX  ",
    "   XXX      XX  ",
    "    X      XX   ",
    "          XX    ",
    "         XX     ",
    "        XX      ",
    "       XX       ",
    "      XX        ",
    "     XX         ",
    "    XX          ",
    "   XX           ",
    "  XX            ",
    " XX             ",
    "X               "
};

typedef struct {
    core_screen_t s;
    uint8_t map[MAP_W][MAP_H];
    float px, py;
    float dirX, dirY;
    float planeX, planeY;
    
    bool keys[7]; 
    
    struct { float x, y; int hp; float timer; } mobs[5];
    
    float zBuffer[SCREEN_W];
    
    int hit_timer;
    float hp;
    float walk_time;
    float global_time;
    int inv[6];
} craft_st_t;

static craft_st_t *st = NULL;

static float randf(void) { return (esp_random() % 1000) / 1000.0f; }

static void generate_map(void) {
    for (int x = 0; x < MAP_W; x++) {
        for (int y = 0; y < MAP_H; y++) {
            if (x == 0 || x == MAP_W - 1 || y == 0 || y == MAP_H - 1) {
                st->map[x][y] = B_BEDROCK;
            } else {
                st->map[x][y] = B_AIR;
            }
        }
    }
    
    // Generate Lakes
    for(int i=0; i<10; i++) {
        int cx = 5 + esp_random() % (MAP_W - 10);
        int cy = 5 + esp_random() % (MAP_H - 10);
        for(int dx=-4; dx<=4; dx++) {
            for(int dy=-4; dy<=4; dy++) {
                if(dx*dx + dy*dy < 12 && randf() > 0.1f) st->map[cx+dx][cy+dy] = B_WATER;
            }
        }
    }
    
    // Generate Forests
    for(int i=0; i<50; i++) {
        int cx = 3 + esp_random() % (MAP_W - 6);
        int cy = 3 + esp_random() % (MAP_H - 6);
        if(st->map[cx][cy] == B_AIR) {
            st->map[cx][cy] = B_WOOD;
            if(st->map[cx+1][cy] == B_AIR) st->map[cx+1][cy] = B_LEAVES;
            if(st->map[cx-1][cy] == B_AIR) st->map[cx-1][cy] = B_LEAVES;
            if(st->map[cx][cy+1] == B_AIR) st->map[cx][cy+1] = B_LEAVES;
            if(st->map[cx][cy-1] == B_AIR) st->map[cx][cy-1] = B_LEAVES;
        }
    }
    
    // Generate Stone nodes
    for(int i=0; i<20; i++) {
        int cx = 5 + esp_random() % (MAP_W - 10);
        int cy = 5 + esp_random() % (MAP_H - 10);
        for(int dx=-2; dx<=2; dx++) {
            for(int dy=-2; dy<=2; dy++) {
                if(dx*dx + dy*dy < 6 && st->map[cx+dx][cy+dy] == B_AIR) st->map[cx+dx][cy+dy] = B_STONE;
            }
        }
    }

    // Clear spawn
    st->px = 2.5f;
    st->py = 2.5f;
    for(int dx=-1; dx<=1; dx++) {
        for(int dy=-1; dy<=1; dy++) {
            st->map[2+dx][2+dy] = B_AIR;
        }
    }
    
    st->dirX = 1.0f; st->dirY = 0.0f;
    st->planeX = 0.0f; st->planeY = 0.8f; // FOV
    
    // Spawn mobs
    for(int i=0; i<5; i++) {
        int mx, my, attempts = 0;
        do {
            mx = 2 + esp_random() % (MAP_W - 4);
            my = 2 + esp_random() % (MAP_H - 4);
            attempts++;
        } while(st->map[mx][my] != B_AIR && attempts < 100);
        st->mobs[i].x = mx + 0.5f;
        st->mobs[i].y = my + 0.5f;
        st->mobs[i].hp = 3;
    }
    
    st->hp = 100.0f;
}

static void break_block(void) {
    float rayDirX = st->dirX;
    float rayDirY = st->dirY;
    int mapX = (int)st->px;
    int mapY = (int)st->py;
    float sideDistX, sideDistY;
    float deltaDistX = (rayDirX == 0) ? 1e30f : fabsf(1.0f / rayDirX);
    float deltaDistY = (rayDirY == 0) ? 1e30f : fabsf(1.0f / rayDirY);
    int stepX, stepY;
    
    if(rayDirX < 0) { stepX = -1; sideDistX = (st->px - mapX) * deltaDistX; }
    else { stepX = 1; sideDistX = (mapX + 1.0f - st->px) * deltaDistX; }
    if(rayDirY < 0) { stepY = -1; sideDistY = (st->py - mapY) * deltaDistY; }
    else { stepY = 1; sideDistY = (mapY + 1.0f - st->py) * deltaDistY; }
    
    for (int i=0; i<5; i++) { 
        if(sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; }
        else { sideDistY += deltaDistY; mapY += stepY; }
        
        if (mapX > 0 && mapX < MAP_W - 1 && mapY > 0 && mapY < MAP_H - 1) {
            int b = st->map[mapX][mapY];
            if (b > 0 && b != B_BEDROCK && b != B_WATER) {
                if (b < 6) st->inv[b]++;
                st->map[mapX][mapY] = B_AIR; 
                return;
            }
        }
    }
}

static void hit_mob(void) {
    for(int i=0; i<5; i++) {
        if (st->mobs[i].hp > 0) {
            float dx = st->mobs[i].x - st->px;
            float dy = st->mobs[i].y - st->py;
            float dist = sqrtf(dx*dx + dy*dy);
            if (dist < 3.0f) {
                float dot = (dx/dist)*st->dirX + (dy/dist)*st->dirY;
                if (dot > 0.8f) {
                    st->mobs[i].hp--;
                    // Knockback
                    float nx = st->mobs[i].x + st->dirX * 0.8f;
                    float ny = st->mobs[i].y + st->dirY * 0.8f;
                    if (st->map[(int)nx][(int)ny] == B_AIR || st->map[(int)nx][(int)ny] == B_WATER) {
                        st->mobs[i].x = nx;
                        st->mobs[i].y = ny;
                    }
                }
            }
        }
    }
}

static void craft_up(core_context_t *ctx, core_screen_t *sc, uint32_t dt) {
    if (!st) return;
    
    float dtSec = dt / 1000.0f;
    st->global_time += dtSec;
    
    float moveSpeed = 4.0f * dtSec;
    float rotSpeed = 3.0f * dtSec;
    
    bool moved = false;
    
    if (st->keys[CORE_INPUT_UP]) {
        float nx = st->px + st->dirX * moveSpeed;
        float ny = st->py + st->dirY * moveSpeed;
        if(st->map[(int)nx][(int)st->py] == B_AIR || st->map[(int)nx][(int)st->py] == B_WATER) st->px = nx;
        if(st->map[(int)st->px][(int)ny] == B_AIR || st->map[(int)st->px][(int)ny] == B_WATER) st->py = ny;
        moved = true;
    }
    if (st->keys[CORE_INPUT_DOWN]) {
        float nx = st->px - st->dirX * moveSpeed;
        float ny = st->py - st->dirY * moveSpeed;
        if(st->map[(int)nx][(int)st->py] == B_AIR || st->map[(int)nx][(int)st->py] == B_WATER) st->px = nx;
        if(st->map[(int)st->px][(int)ny] == B_AIR || st->map[(int)st->px][(int)ny] == B_WATER) st->py = ny;
        moved = true;
    }
    
    if (moved) st->walk_time += moveSpeed * 3.0f;
    
    // Inverted Left/Right as requested: RIGHT = Left turn (Counter-Clockwise)
    if (st->keys[CORE_INPUT_RIGHT]) {
        float a = rotSpeed; // +rotSpeed = CCW
        float oldDirX = st->dirX;
        st->dirX = st->dirX * cosf(a) - st->dirY * sinf(a);
        st->dirY = oldDirX * sinf(a) + st->dirY * cosf(a);
        float oldPlaneX = st->planeX;
        st->planeX = st->planeX * cosf(a) - st->planeY * sinf(a);
        st->planeY = oldPlaneX * sinf(a) + st->planeY * cosf(a);
    }
    if (st->keys[CORE_INPUT_LEFT]) {
        float a = -rotSpeed; // -rotSpeed = CW
        float oldDirX = st->dirX;
        st->dirX = st->dirX * cosf(a) - st->dirY * sinf(a);
        st->dirY = oldDirX * sinf(a) + st->dirY * cosf(a);
        float oldPlaneX = st->planeX;
        st->planeX = st->planeX * cosf(a) - st->planeY * sinf(a);
        st->planeY = oldPlaneX * sinf(a) + st->planeY * cosf(a);
    }
    
    if (st->hit_timer > 0) {
        st->hit_timer -= dt;
        if (st->hit_timer < 0) st->hit_timer = 0;
    }
    
    // Mobs logic
    for(int i=0; i<5; i++) {
        if (st->mobs[i].hp > 0) {
            st->mobs[i].timer += dtSec;
            if (st->mobs[i].timer > 0.3f) {
                st->mobs[i].timer = 0;
                float dx = st->px - st->mobs[i].x;
                float dy = st->py - st->mobs[i].y;
                float dist = sqrtf(dx*dx + dy*dy);
                
                if (dist < 10.0f && dist > 1.0f) { 
                    float nx = st->mobs[i].x + (dx/dist)*0.4f;
                    float ny = st->mobs[i].y + (dy/dist)*0.4f;
                    if(st->map[(int)nx][(int)ny] == B_AIR || st->map[(int)nx][(int)ny] == B_WATER) {
                        st->mobs[i].x = nx; st->mobs[i].y = ny;
                    }
                } else if (dist <= 1.0f) {
                    st->hp -= 5.0f; // Attack player
                    if (st->hp < 0) {
                        st->px = 2.5f; st->py = 2.5f; st->hp = 100.0f; // Respawn
                    }
                }
            }
        }
    }
    
    core_nav_mark_dirty(&ctx->nav);
}

static bool craft_in(core_context_t *ctx, core_screen_t *sc, const core_input_event_t *e) {
    if (!st) return false;
    
    if (e->action == CORE_INPUT_BACK && e->phase == CORE_INPUT_PHASE_PRESS) {
        core_nav_pop(ctx, &ctx->nav);
        free(st); st = NULL;
        return true;
    }
    
    if (e->action < 7) {
        if (e->phase == CORE_INPUT_PHASE_PRESS) {
            st->keys[e->action] = true;
            if (e->action == CORE_INPUT_SELECT && st->hit_timer == 0) {
                st->hit_timer = 200;
                break_block();
                hit_mob();
            }
        } else if (e->phase == CORE_INPUT_PHASE_RELEASE) {
            st->keys[e->action] = false;
        }
        return true;
    }
    return false;
}

static void craft_r(core_context_t *ctx, core_screen_t *sc, ui_t *ui) {
    if (!st) return;
    ui_fill_rect(ui, 0, 0, SCREEN_W, SCREEN_H, false);
    
    int pitch = (int)(sinf(st->walk_time) * 3.0f); // View bobbing
    int viewH = 53; // Leave bottom 11 pixels for hotbar
    
    // Raycasting Walls
    for(int x = 0; x < SCREEN_W; x++) {
        float cameraX = 2.0f * x / (float)SCREEN_W - 1.0f;
        float rayDirX = st->dirX + st->planeX * cameraX;
        float rayDirY = st->dirY + st->planeY * cameraX;
        
        int mapX = (int)st->px;
        int mapY = (int)st->py;
        
        float sideDistX, sideDistY;
        float deltaDistX = (rayDirX == 0) ? 1e30f : fabsf(1.0f / rayDirX);
        float deltaDistY = (rayDirY == 0) ? 1e30f : fabsf(1.0f / rayDirY);
        float perpWallDist;
        
        int stepX, stepY;
        int hit = 0, side = 0;
        
        if(rayDirX < 0) { stepX = -1; sideDistX = (st->px - mapX) * deltaDistX; }
        else { stepX = 1; sideDistX = (mapX + 1.0f - st->px) * deltaDistX; }
        if(rayDirY < 0) { stepY = -1; sideDistY = (st->py - mapY) * deltaDistY; }
        else { stepY = 1; sideDistY = (mapY + 1.0f - st->py) * deltaDistY; }
        
        while(hit == 0) {
            if(sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; }
            else { sideDistY += deltaDistY; mapY += stepY; side = 1; }
            if(mapX >= 0 && mapX < MAP_W && mapY >= 0 && mapY < MAP_H) {
                int b = st->map[mapX][mapY];
                if (b > 0 && b != B_WATER) {
                    hit = b;
                }
            } else {
                hit = B_BEDROCK; 
            }
        }
        
        if(side == 0) perpWallDist = (sideDistX - deltaDistX);
        else perpWallDist = (sideDistY - deltaDistY);
        
        st->zBuffer[x] = perpWallDist;
        
        int lineHeight = (int)(viewH / perpWallDist);
        
        // Unclipped start for texture calc
        int unclippedStart = -lineHeight / 2 + viewH / 2 + pitch;
        
        int drawStart = unclippedStart;
        if(drawStart < 0) drawStart = 0;
        int drawEnd = lineHeight / 2 + viewH / 2 + pitch;
        if(drawEnd >= viewH) drawEnd = viewH - 1;
        
        float wallX;
        if (side == 0) wallX = st->py + perpWallDist * rayDirY;
        else           wallX = st->px + perpWallDist * rayDirX;
        wallX -= floorf(wallX);
        
        int texX = (int)(wallX * 8.0f);
        if(side == 0 && rayDirX > 0) texX = 7 - texX;
        if(side == 1 && rayDirY < 0) texX = 7 - texX;
        
        // Sky
        for(int y = 0; y < drawStart; y++) {
            if ((x + y + (int)(st->dirX*15)) % 12 == 0) ui_draw_pixel(ui, x, y, true);
        }
        
        float step = 8.0f / lineHeight;
        float texPos = (drawStart - unclippedStart) * step;
        
        // Wall
        for(int y = drawStart; y <= drawEnd; y++) {
            int texY = (int)texPos & 7;
            texPos += step;
            
            bool color = (TEX[hit][texY] & (1 << (7 - texX))) != 0;
            
            // Shading
            if (side == 1) {
                if ((x + y) % 2 != 0) color = false; // darken
            }
            if (perpWallDist > 5.0f) {
                if ((x + y) % 2 == 0) color = false; // fade to black
            }
            
            if (color) ui_draw_pixel(ui, x, y, true);
        }
        
        // Floor / Water
        for(int y = drawEnd + 1; y < viewH; y++) {
            float currentDist = viewH / (2.0f * (y - pitch) - viewH);
            float weight = currentDist / perpWallDist;
            float currentFloorX = weight * mapX + (1.0f - weight) * st->px;
            float currentFloorY = weight * mapY + (1.0f - weight) * st->py;
            
            int fx = (int)currentFloorX;
            int fy = (int)currentFloorY;
            if(fx >= 0 && fx < MAP_W && fy >= 0 && fy < MAP_H) {
                if (st->map[fx][fy] == B_WATER) {
                    if ((x + y + (int)(st->global_time*5)) % 4 == 0) ui_draw_pixel(ui, x, y, true); 
                } else {
                    if (y % 2 == 0 && x % 2 == 0) ui_draw_pixel(ui, x, y, true); 
                }
            }
        }
    }
    
    // Draw Mobs
    for(int i=0; i<5; i++) {
        if (st->mobs[i].hp <= 0) continue;
        float spriteX = st->mobs[i].x - st->px;
        float spriteY = st->mobs[i].y - st->py;
        float invDet = 1.0f / (st->planeX * st->dirY - st->dirX * st->planeY);
        float transformX = invDet * (st->dirY * spriteX - st->dirX * spriteY);
        float transformY = invDet * (-st->planeY * spriteX + st->planeX * spriteY);
        
        if (transformY > 0) {
            int spriteScreenX = (int)((SCREEN_W / 2) * (1.0f + transformX / transformY));
            int spriteHeight = abs((int)(viewH / transformY));
            int drawStartY = -spriteHeight / 2 + viewH / 2 + pitch;
            int unclippedY = drawStartY;
            if(drawStartY < 0) drawStartY = 0;
            int drawEndY = spriteHeight / 2 + viewH / 2 + pitch;
            if(drawEndY >= viewH) drawEndY = viewH - 1;
            
            int spriteWidth = abs((int)(viewH / transformY)); 
            int drawStartX = -spriteWidth / 2 + spriteScreenX;
            if(drawStartX < 0) drawStartX = 0;
            int drawEndX = spriteWidth / 2 + spriteScreenX;
            if(drawEndX >= SCREEN_W) drawEndX = SCREEN_W - 1;
            
            for(int stripe = drawStartX; stripe < drawEndX; stripe++) {
                if(transformY < st->zBuffer[stripe]) {
                    int texX = (int)(256 * (stripe - (-spriteWidth / 2 + spriteScreenX)) * 8 / spriteWidth) / 256;
                    for(int y = drawStartY; y < drawEndY; y++) {
                        int texY = (int)(256 * (y - unclippedY) * 8 / spriteHeight) / 256;
                        bool pixel = (CREEPER[texY] & (1 << (7 - texX))) != 0;
                        if (pixel) ui_draw_pixel(ui, stripe, y, false); 
                        else ui_draw_pixel(ui, stripe, y, true); 
                    }
                }
            }
        }
    }
    
    // Draw Pickaxe Hand
    int px_x = 90;
    int px_y = 35 + pitch;
    if (st->hit_timer > 0) {
        px_x -= 15;
        px_y += 15;
    }
    for(int r=0; r<16; r++) {
        for(int c=0; c<16; c++) {
            if (px_y + r < viewH && px_x + c >= 0 && px_x + c < SCREEN_W) {
                if(PICKAXE[r][c] == 'X') {
                    ui_draw_pixel(ui, px_x+c, px_y+r, true);
                    // Dither the pickaxe body slightly for texture
                    if ((r+c)%2 == 0) ui_draw_pixel(ui, px_x+c, px_y+r, false);
                }
            }
        }
    }
    
    // Crosshair
    ui_draw_pixel(ui, SCREEN_W/2, viewH/2, true);
    ui_draw_pixel(ui, SCREEN_W/2 - 1, viewH/2, true);
    ui_draw_pixel(ui, SCREEN_W/2 + 1, viewH/2, true);
    ui_draw_pixel(ui, SCREEN_W/2, viewH/2 - 1, true);
    ui_draw_pixel(ui, SCREEN_W/2, viewH/2 + 1, true);
    
    // Hotbar overlay (y = 54 to 63)
    ui_fill_rect(ui, 0, 54, 128, 10, false); // clear background
    ui_fill_rect(ui, 0, 53, 128, 1, true);   // border line
    
    // HP Bar
    ui_draw_text(ui, 2, 56, "HP", true);
    ui_fill_rect(ui, 16, 56, 22, 1, true);
    ui_fill_rect(ui, 16, 60, 22, 1, true);
    ui_fill_rect(ui, 16, 56, 1, 5, true);
    ui_fill_rect(ui, 37, 56, 1, 5, true);
    int hpw = (int)((st->hp / 100.0f) * 20);
    if (hpw > 0) ui_fill_rect(ui, 17, 57, hpw, 3, true);
    
    // Inventory
    char inv_buf[32];
    snprintf(inv_buf, sizeof(inv_buf), "S%d W%d L%d", st->inv[B_STONE], st->inv[B_WOOD], st->inv[B_LEAVES]);
    ui_draw_text(ui, 42, 56, inv_buf, true);
}

static esp_err_t craft_launch(core_context_t *ctx) {
    if (st) return ESP_FAIL; 
    st = malloc(sizeof(craft_st_t));
    if (!st) return ESP_ERR_NO_MEM;
    memset(st, 0, sizeof(craft_st_t));
    
    st->s.id = "craft";
    st->s.on_input = craft_in;
    st->s.on_update = craft_up;
    st->s.on_render = craft_r;
    
    generate_map();
    
    return core_nav_push(ctx, &ctx->nav, &st->s);
}

const core_app_descriptor_t g_craft_app = { .id = "craft", .name = "Craft 3D", .icon = "C", .launch = craft_launch };
