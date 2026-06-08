#include "craft_engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

void craft_render_scene(ui_t *ui, craft_player_t *p, craft_mob_t *mobs) {
    ui_fill_rect(ui, 0, 0, SCREEN_W, SCREEN_H, false);
    
    float cameraZ = 0.7f; // Player is taller than 0.5 (center)
    int pitch = (int)(sinf(p->walk_time) * 3.0f); // Bobbing
    int viewH = 53; // Leave bottom 11 pixels for hotbar
    
    for(int x = 0; x < SCREEN_W; x++) {
        float cameraX = 2.0f * x / (float)SCREEN_W - 1.0f;
        float rayDirX = p->dirX + p->planeX * cameraX;
        float rayDirY = p->dirY + p->planeY * cameraX;
        
        int mapX = (int)p->px;
        int mapY = (int)p->py;
        
        float sideDistX, sideDistY;
        float deltaDistX = (rayDirX == 0) ? 1e30f : fabsf(1.0f / rayDirX);
        float deltaDistY = (rayDirY == 0) ? 1e30f : fabsf(1.0f / rayDirY);
        float perpWallDist;
        
        int stepX, stepY;
        int hit = 0, side = 0;
        
        if(rayDirX < 0) { stepX = -1; sideDistX = (p->px - mapX) * deltaDistX; }
        else { stepX = 1; sideDistX = (mapX + 1.0f - p->px) * deltaDistX; }
        if(rayDirY < 0) { stepY = -1; sideDistY = (p->py - mapY) * deltaDistY; }
        else { stepY = 1; sideDistY = (mapY + 1.0f - p->py) * deltaDistY; }
        
        int depth = 0;
        while(hit == 0 && depth < 30) { // Max depth 30 blocks
            if(sideDistX < sideDistY) { sideDistX += deltaDistX; mapX += stepX; side = 0; }
            else { sideDistY += deltaDistY; mapY += stepY; side = 1; }
            
            int b = craft_world_get_block(mapX, mapY);
            if (b > 0 && b != B_WATER) hit = b;
            depth++;
        }
        if (hit == 0) hit = B_BEDROCK; // Fog fallback
        
        if(side == 0) perpWallDist = (sideDistX - deltaDistX);
        else perpWallDist = (sideDistY - deltaDistY);
        
        p->zBuffer[x] = perpWallDist;
        
        int lineHeight = (int)(viewH / perpWallDist);
        
        int unclippedStart = (int)(-lineHeight * cameraZ + viewH / 2.0f + pitch);
        int drawStart = unclippedStart;
        if(drawStart < 0) drawStart = 0;
        int drawEnd = (int)(lineHeight * (1.0f - cameraZ) + viewH / 2.0f + pitch);
        if(drawEnd >= viewH) drawEnd = viewH - 1;
        
        float wallX;
        if (side == 0) wallX = p->py + perpWallDist * rayDirY;
        else           wallX = p->px + perpWallDist * rayDirX;
        wallX -= floorf(wallX);
        
        int texX = (int)(wallX * 8.0f);
        if(side == 0 && rayDirX > 0) texX = 7 - texX;
        if(side == 1 && rayDirY < 0) texX = 7 - texX;
        
        // Sky
        for(int y = 0; y < drawStart; y++) {
            if ((x + y + (int)(p->dirX*20)) % 16 == 0) ui_draw_pixel(ui, x, y, true);
        }
        
        float step = 8.0f / lineHeight;
        float texPos = (drawStart - unclippedStart) * step;
        
        // Wall
        for(int y = drawStart; y <= drawEnd; y++) {
            int texY = (int)texPos & 7;
            texPos += step;
            bool color = (TEX[hit][texY] & (1 << (7 - texX))) != 0;
            
            // Shading
            if (side == 1 && color) color = ((x + y) % 2 == 0); // Shadow
            if (perpWallDist > 8.0f && color) color = ((x + y) % 3 == 0); // Fog
            
            if (color) ui_draw_pixel(ui, x, y, true);
        }
        
        // Floor / Water
        for(int y = drawEnd + 1; y < viewH; y++) {
            float currentDist = (viewH * cameraZ) / (y - viewH/2.0f - pitch);
            float weight = currentDist / perpWallDist;
            float currentFloorX = weight * mapX + (1.0f - weight) * p->px;
            float currentFloorY = weight * mapY + (1.0f - weight) * p->py;
            
            int fx = (int)currentFloorX;
            int fy = (int)currentFloorY;
            int b = craft_world_get_block(fx, fy);
            if (b == B_WATER) {
                if ((x + y + (int)(p->global_time*5)) % 5 == 0) ui_draw_pixel(ui, x, y, true); 
            } else {
                if (y % 2 == 0 && x % 2 == 0) ui_draw_pixel(ui, x, y, true); 
            }
        }
    }
    
    // Draw Mobs
    for(int i=0; i<5; i++) {
        if (!mobs[i].active || mobs[i].hp <= 0) continue;
        float spriteX = mobs[i].x - p->px;
        float spriteY = mobs[i].y - p->py;
        float invDet = 1.0f / (p->planeX * p->dirY - p->dirX * p->planeY);
        float transformX = invDet * (p->dirY * spriteX - p->dirX * spriteY);
        float transformY = invDet * (-p->planeY * spriteX + p->planeX * spriteY);
        
        if (transformY > 0) {
            int spriteScreenX = (int)((SCREEN_W / 2) * (1.0f + transformX / transformY));
            int spriteHeight = abs((int)(viewH / transformY));
            
            // Mob is 1 block high. To place it on the floor, we align its bottom with the wall's bottom.
            // Wall bottom for cameraZ is: lineHeight * (1.0f - cameraZ) + viewH / 2.0f + pitch
            int drawEndY = (int)(spriteHeight * (1.0f - cameraZ) + viewH / 2.0f + pitch);
            int drawStartY = drawEndY - spriteHeight;
            
            int unclippedY = drawStartY;
            
            if(drawStartY < 0) drawStartY = 0;
            if(drawEndY >= viewH) drawEndY = viewH - 1;
            
            int spriteWidth = abs((int)(viewH / transformY)); 
            int drawStartX = -spriteWidth / 2 + spriteScreenX;
            if(drawStartX < 0) drawStartX = 0;
            int drawEndX = spriteWidth / 2 + spriteScreenX;
            if(drawEndX >= SCREEN_W) drawEndX = SCREEN_W - 1;
            
            for(int stripe = drawStartX; stripe < drawEndX; stripe++) {
                if(transformY < p->zBuffer[stripe]) {
                    int texX = (int)(256 * (stripe - (-spriteWidth / 2 + spriteScreenX)) * 8 / spriteWidth) / 256;
                    for(int y = drawStartY; y <= drawEndY; y++) {
                        int texY = (int)(256 * (y - unclippedY) * 8 / spriteHeight) / 256;
                        if (texY < 0 || texY > 7) continue;
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
    if (p->hit_timer > 0) {
        px_x -= 15;
        px_y += 15;
    }
    for(int r=0; r<16; r++) {
        for(int c=0; c<16; c++) {
            if (px_y + r < viewH && px_x + c >= 0 && px_x + c < SCREEN_W) {
                if(PICKAXE[r][c] == 'X') {
                    ui_draw_pixel(ui, px_x+c, px_y+r, true);
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
    int hpw = (int)((p->hp / 100.0f) * 20);
    if (hpw > 0) ui_fill_rect(ui, 17, 57, hpw, 3, true);
    
    // Inventory
    char inv_buf[32];
    snprintf(inv_buf, sizeof(inv_buf), "S%d W%d L%d", p->inv[B_STONE], p->inv[B_WOOD], p->inv[B_LEAVES]);
    ui_draw_text(ui, 42, 56, inv_buf, true);
}
