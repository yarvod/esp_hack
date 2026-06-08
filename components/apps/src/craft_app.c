#include "core/app_manager.h"
#include "core/context.h"
#include "ui/ui.h"
#include "esp_random.h"
#include <string.h>
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
    
    for(int i=0; i<8; i++) {
        int cx = 5 + esp_random() % (MAP_W - 10);
        int cy = 5 + esp_random() % (MAP_H - 10);
        for(int dx=-3; dx<=3; dx++) {
            for(int dy=-3; dy<=3; dy++) {
                if(dx*dx + dy*dy < 8 && randf() > 0.2f) st->map[cx+dx][cy+dy] = B_WATER;
            }
        }
    }
    
    for(int i=0; i<40; i++) {
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
    
    for(int i=0; i<15; i++) {
        int cx = 5 + esp_random() % (MAP_W - 10);
        int cy = 5 + esp_random() % (MAP_H - 10);
        for(int dx=-2; dx<=2; dx++) {
            for(int dy=-2; dy<=2; dy++) {
                if(dx*dx + dy*dy < 5 && st->map[cx+dx][cy+dy] == B_AIR) st->map[cx+dx][cy+dy] = B_STONE;
            }
        }
    }

    st->px = 2.5f;
    st->py = 2.5f;
    st->map[2][2] = B_AIR;
    st->map[2][3] = B_AIR;
    
    st->dirX = 1.0f; st->dirY = 0.0f;
    st->planeX = 0.0f; st->planeY = 0.8f;
    
    for(int i=0; i<5; i++) {
        int mx, my;
        do {
            mx = 2 + esp_random() % (MAP_W - 4);
            my = 2 + esp_random() % (MAP_H - 4);
        } while(st->map[mx][my] != B_AIR || (mx < 5 && my < 5));
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
            if (dist < 2.5f) {
                float dot = (dx/dist)*st->dirX + (dy/dist)*st->dirY;
                if (dot > 0.8f) {
                    st->mobs[i].hp--;
                    st->mobs[i].x += st->dirX * 0.5f;
                    st->mobs[i].y += st->dirY * 0.5f;
                }
            }
        }
    }
}

static void craft_up(core_context_t *ctx, core_screen_t *sc, uint32_t dt) {
    if (!st) return;
    
    float dtSec = dt / 1000.0f;
    float moveSpeed = 4.0f * dtSec;
    float rotSpeed = 3.0f * dtSec;
    
    if (st->keys[CORE_INPUT_UP]) {
        float nx = st->px + st->dirX * moveSpeed;
        float ny = st->py + st->dirY * moveSpeed;
        if(st->map[(int)nx][(int)st->py] == B_AIR || st->map[(int)nx][(int)st->py] == B_WATER) st->px = nx;
        if(st->map[(int)st->px][(int)ny] == B_AIR || st->map[(int)st->px][(int)ny] == B_WATER) st->py = ny;
    }
    if (st->keys[CORE_INPUT_DOWN]) {
        float nx = st->px - st->dirX * moveSpeed;
        float ny = st->py - st->dirY * moveSpeed;
        if(st->map[(int)nx][(int)st->py] == B_AIR || st->map[(int)nx][(int)st->py] == B_WATER) st->px = nx;
        if(st->map[(int)st->px][(int)ny] == B_AIR || st->map[(int)st->px][(int)ny] == B_WATER) st->py = ny;
    }
    if (st->keys[CORE_INPUT_RIGHT]) {
        float oldDirX = st->dirX;
        st->dirX = st->dirX * cosf(-rotSpeed) - st->dirY * sinf(-rotSpeed);
        st->dirY = oldDirX * sinf(-rotSpeed) + st->dirY * cosf(-rotSpeed);
        float oldPlaneX = st->planeX;
        st->planeX = st->planeX * cosf(-rotSpeed) - st->planeY * sinf(-rotSpeed);
        st->planeY = oldPlaneX * sinf(-rotSpeed) + st->planeY * cosf(-rotSpeed);
    }
    if (st->keys[CORE_INPUT_LEFT]) {
        float oldDirX = st->dirX;
        st->dirX = st->dirX * cosf(rotSpeed) - st->dirY * sinf(rotSpeed);
        st->dirY = oldDirX * sinf(rotSpeed) + st->dirY * cosf(rotSpeed);
        float oldPlaneX = st->planeX;
        st->planeX = st->planeX * cosf(rotSpeed) - st->planeY * sinf(rotSpeed);
        st->planeY = oldPlaneX * sinf(rotSpeed) + st->planeY * cosf(rotSpeed);
    }
    
    if (st->hit_timer > 0) {
        st->hit_timer -= dt;
        if (st->hit_timer < 0) st->hit_timer = 0;
    }
    
    for(int i=0; i<5; i++) {
        if (st->mobs[i].hp > 0) {
            st->mobs[i].timer += dtSec;
            if (st->mobs[i].timer > 0.5f) {
                st->mobs[i].timer = 0;
                float dx = st->px - st->mobs[i].x;
                float dy = st->py - st->mobs[i].y;
                float dist = sqrtf(dx*dx + dy*dy);
                if (dist < 8.0f && dist > 1.0f) { 
                    float nx = st->mobs[i].x + (dx/dist)*0.2f;
                    float ny = st->mobs[i].y + (dy/dist)*0.2f;
                    if(st->map[(int)nx][(int)ny] == B_AIR || st->map[(int)nx][(int)ny] == B_WATER) {
                        st->mobs[i].x = nx; st->mobs[i].y = ny;
                    }
                } else if (dist <= 1.0f) {
                    st->hp -= 2.0f; 
                    if (st->hp < 0) {
                        st->px = 2.5f; st->py = 2.5f; st->hp = 100.0f;
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
                st->hit_timer = 150;
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
        
        int lineHeight = (int)(SCREEN_H / perpWallDist);
        int drawStart = -lineHeight / 2 + SCREEN_H / 2;
        int trueDrawStart = drawStart;
        if(drawStart < 0) drawStart = 0;
        int drawEnd = lineHeight / 2 + SCREEN_H / 2;
        if(drawEnd >= SCREEN_H) drawEnd = SCREEN_H - 1;
        
        float wallX;
        if (side == 0) wallX = st->py + perpWallDist * rayDirY;
        else           wallX = st->px + perpWallDist * rayDirX;
        wallX -= floorf(wallX);
        
        bool isEdgeX = (wallX < 0.05f || wallX > 0.95f);
        
        for(int y = drawStart; y <= drawEnd; y++) {
            float texY = (float)(y - trueDrawStart) / (float)lineHeight;
            bool isEdgeY = (texY < 0.05f || texY > 0.95f);
            
            bool color = false;
            if (isEdgeX || isEdgeY) {
                color = true; 
            } else {
                if (hit == B_BEDROCK) color = ((x/2 + y/2) % 2 == 0);
                else if (hit == B_WOOD) color = ((int)(wallX*8) % 2 == 0); 
                else if (hit == B_LEAVES) color = ((x*y) % 3 == 0); 
                else if (hit == B_STONE) color = ((x+y) % 2 == 0); 
                else color = true; 
                
                if (side == 1 && !color) {
                    if ((x+y)%2 == 0) color = true; 
                } else if (side == 1 && color) {
                    if ((x+y)%2 == 0) color = false; 
                }
            }
            if (color) ui_draw_pixel(ui, x, y, true);
        }
        
        for(int y = drawEnd + 1; y < SCREEN_H; y++) {
            float currentDist = SCREEN_H / (2.0f * y - SCREEN_H);
            float weight = currentDist / perpWallDist;
            float currentFloorX = weight * mapX + (1.0f - weight) * st->px;
            float currentFloorY = weight * mapY + (1.0f - weight) * st->py;
            
            int fx = (int)currentFloorX;
            int fy = (int)currentFloorY;
            if(fx >= 0 && fx < MAP_W && fy >= 0 && fy < MAP_H) {
                if (st->map[fx][fy] == B_WATER) {
                    if (y % 3 == 0) ui_draw_pixel(ui, x, y, true); 
                } else {
                    if (y % 2 == 0 && x % 2 == 0) ui_draw_pixel(ui, x, y, true); 
                }
            }
        }
    }
    
    for(int i=0; i<5; i++) {
        if (st->mobs[i].hp <= 0) continue;
        float spriteX = st->mobs[i].x - st->px;
        float spriteY = st->mobs[i].y - st->py;
        float invDet = 1.0f / (st->planeX * st->dirY - st->dirX * st->planeY);
        float transformX = invDet * (st->dirY * spriteX - st->dirX * spriteY);
        float transformY = invDet * (-st->planeY * spriteX + st->planeX * spriteY);
        
        if (transformY > 0) {
            int spriteScreenX = (int)((SCREEN_W / 2) * (1.0f + transformX / transformY));
            int spriteHeight = abs((int)(SCREEN_H / transformY));
            int drawStartY = -spriteHeight / 2 + SCREEN_H / 2;
            if(drawStartY < 0) drawStartY = 0;
            int drawEndY = spriteHeight / 2 + SCREEN_H / 2;
            if(drawEndY >= SCREEN_H) drawEndY = SCREEN_H - 1;
            
            int spriteWidth = abs((int)(SCREEN_H / transformY)); 
            int drawStartX = -spriteWidth / 2 + spriteScreenX;
            if(drawStartX < 0) drawStartX = 0;
            int drawEndX = spriteWidth / 2 + spriteScreenX;
            if(drawEndX >= SCREEN_W) drawEndX = SCREEN_W - 1;
            
            for(int stripe = drawStartX; stripe < drawEndX; stripe++) {
                if(transformY < st->zBuffer[stripe]) {
                    int texX = (int)(256 * (stripe - (-spriteWidth / 2 + spriteScreenX)) * 8 / spriteWidth) / 256;
                    for(int y = drawStartY; y < drawEndY; y++) {
                        int d = (y) * 256 - SCREEN_H * 128 + spriteHeight * 128;
                        int texY = ((d * 8) / spriteHeight) / 256;
                        bool pixel = false;
                        if ((texY == 2 || texY == 3) && (texX == 2 || texX == 5)) pixel = true; 
                        if (texY == 4 && texX >= 3 && texX <= 4) pixel = true; 
                        if (texY == 5 && texX >= 2 && texX <= 5) pixel = true; 
                        if (texY == 6 && (texX == 2 || texX == 5)) pixel = true; 
                        
                        if (pixel) ui_draw_pixel(ui, stripe, y, false); 
                        else ui_draw_pixel(ui, stripe, y, true); 
                    }
                }
            }
        }
    }
    
    int handY = SCREEN_H - 25;
    int handX = SCREEN_W - 35;
    if (st->hit_timer > 0) {
        handY -= 5;
        handX -= 5;
    }
    for(int w=0; w<20; w++) {
        for(int h=0; h<25; h++) {
            if (h > w) { 
                ui_draw_pixel(ui, handX + w, handY + h, true);
                if (w > 2 && w < 18 && h > w + 2 && (w+h)%2==0) {
                    ui_draw_pixel(ui, handX + w, handY + h, false); 
                }
            }
        }
    }
    
    ui_draw_pixel(ui, SCREEN_W/2, SCREEN_H/2, true);
    ui_draw_pixel(ui, SCREEN_W/2 - 1, SCREEN_H/2, true);
    ui_draw_pixel(ui, SCREEN_W/2 + 1, SCREEN_H/2, true);
    ui_draw_pixel(ui, SCREEN_W/2, SCREEN_H/2 - 1, true);
    ui_draw_pixel(ui, SCREEN_W/2, SCREEN_H/2 + 1, true);
    
    ui_fill_rect(ui, 5, 5, 40, 1, true);
    ui_fill_rect(ui, 5, 9, 40, 1, true);
    ui_fill_rect(ui, 5, 5, 1, 5, true);
    ui_fill_rect(ui, 44, 5, 1, 5, true);
    int hpw = (int)((st->hp / 100.0f) * 38);
    if (hpw > 0) ui_fill_rect(ui, 6, 6, hpw, 3, true);
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
