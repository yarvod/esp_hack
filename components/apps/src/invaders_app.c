#include "core/app_manager.h"
#include "core/context.h"
#include "ui/status_bar.h"
#include <stdio.h>
#include <string.h>

#define INV_ROWS 3
#define INV_COLS 6
#define INV_W 8
#define INV_H 6
#define INV_GAP 6
#define PLAYER_W 10
#define PLAYER_H 4
#define MAX_BULLETS 4
#define FIRE_COOLDOWN_MS 400

typedef struct { float x, y; bool active; } bullet_t;
typedef struct {
    core_screen_t screen;
    float player_x;
    uint8_t invaders[INV_ROWS][INV_COLS];
    float inv_x, inv_y, inv_dx;
    bullet_t p_bullet;
    bullet_t e_bullets[MAX_BULLETS];
    uint16_t score;
    uint8_t level;
    bool running, game_over;
    bool move_left, move_right, firing;
    uint32_t fire_timer_ms;
} invaders_state_t;

static invaders_state_t s_inv;

static void inv_spawn_wave(invaders_state_t *s) {
    memset(s->invaders, 1, sizeof(s->invaders));
    s->inv_x = 10;
    s->inv_y = 15;
    s->inv_dx = 0.5f + (s->level * 0.2f);
}

static void inv_reset(invaders_state_t *s) {
    s->player_x = 64 - PLAYER_W / 2;
    s->level = 0;
    inv_spawn_wave(s);
    s->p_bullet.active = false;
    for (int i = 0; i < MAX_BULLETS; i++) s->e_bullets[i].active = false;
    s->score = 0; s->running = true; s->game_over = false;
    s->move_left = false; s->move_right = false; s->firing = false;
    s->fire_timer_ms = 0;
}

static bool inv_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event) {
    invaders_state_t *s = (invaders_state_t *)screen->user_data;
    bool handled = false;

    if (event->phase == CORE_INPUT_PHASE_PRESS || event->phase == CORE_INPUT_PHASE_REPEAT) {
        if (s->game_over && event->action == CORE_INPUT_SELECT) {
            inv_reset(s);
            handled = true;
        } else {
            switch (event->action) {
                case CORE_INPUT_LEFT: s->move_left = true; s->move_right = false; handled = true; break;
                case CORE_INPUT_RIGHT: s->move_right = true; s->move_left = false; handled = true; break;
                case CORE_INPUT_UP:
                case CORE_INPUT_SELECT:
                    s->firing = true;
                    handled = true;
                    break;
                default: break;
            }
        }
    } else if (event->phase == CORE_INPUT_PHASE_RELEASE) {
        if (event->action == CORE_INPUT_LEFT) { s->move_left = false; handled = true; }
        if (event->action == CORE_INPUT_RIGHT) { s->move_right = false; handled = true; }
        if (event->action == CORE_INPUT_UP || event->action == CORE_INPUT_SELECT) { s->firing = false; handled = true; }
    }

    if (handled) core_nav_mark_dirty(&ctx->nav);
    return handled;
}

static void inv_on_update(core_context_t *ctx, core_screen_t *screen, uint32_t dt_ms) {
    invaders_state_t *s = (invaders_state_t *)screen->user_data;
    if (!s->running || s->game_over) return;

    if (s->move_left) s->player_x = (s->player_x > 0) ? s->player_x - 3 : 0;
    if (s->move_right) s->player_x = (s->player_x < 128 - PLAYER_W) ? s->player_x + 3 : 128 - PLAYER_W;

    if (s->firing) {
        if (!s->p_bullet.active) {
            s->p_bullet.active = true;
            s->p_bullet.x = s->player_x + PLAYER_W / 2;
            s->p_bullet.y = 55;
            s->fire_timer_ms = FIRE_COOLDOWN_MS;
        } else if (s->fire_timer_ms > dt_ms) {
            s->fire_timer_ms -= dt_ms;
        } else {
            // Auto-fire doesn't need to wait for bullet to disappear if we have multiple bullets,
            // but here we have only one. Let's just reset timer.
            s->fire_timer_ms = 0;
        }
    }

    s->inv_x += s->inv_dx;
    if (s->inv_x <= 0 || s->inv_x >= 128 - (INV_COLS * (INV_W + INV_GAP))) {
        s->inv_dx = -s->inv_dx; s->inv_y += 2;
        if (s->inv_y > 45) { s->game_over = true; s->running = false; }
    }
    if (s->p_bullet.active) {
        s->p_bullet.y -= 2.0f;
        if (s->p_bullet.y < 10) s->p_bullet.active = false;
        else {
            int r = (int)((s->p_bullet.y - s->inv_y) / (INV_H + INV_GAP));
            int c = (int)((s->p_bullet.x - s->inv_x) / (INV_W + INV_GAP));
            if (r >= 0 && r < INV_ROWS && c >= 0 && c < INV_COLS && s->invaders[r][c]) {
                s->invaders[r][c] = 0; s->p_bullet.active = false; s->score += 10;
            }
        }
    }

    // Check if all invaders are cleared
    bool any_invaders = false;
    for (int i = 0; i < INV_ROWS; i++) {
        for (int j = 0; j < INV_COLS; j++) {
            if (s->invaders[i][j]) { any_invaders = true; break; }
        }
        if (any_invaders) break;
    }

    if (!any_invaders) {
        s->level++;
        inv_spawn_wave(s);
    }

    core_nav_mark_dirty(&ctx->nav);
}

static void inv_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui) {
    invaders_state_t *s = (invaders_state_t *)screen->user_data;
    ui_status_bar_render(ui, "INVADERS");
    char buf[24]; snprintf(buf, sizeof(buf), "SC:%u LV:%u", s->score, s->level + 1);
    ui_draw_text(ui, 2, 12, buf, true);
    ui_fill_rect(ui, (int)s->player_x, 58, PLAYER_W, PLAYER_H, true);
    if (s->p_bullet.active) ui_fill_rect(ui, (int)s->p_bullet.x, (int)s->p_bullet.y, 1, 3, true);
    for (int i = 0; i < INV_ROWS; i++) {
        for (int j = 0; j < INV_COLS; j++) {
            if (s->invaders[i][j]) ui_draw_rect(ui, (int)s->inv_x + j * (INV_W + INV_GAP), (int)s->inv_y + i * (INV_H + INV_GAP), INV_W, INV_H, true);
        }
    }
    if (s->game_over) ui_draw_text_aligned(ui, 64, 35, 128, "GAME OVER", UI_ALIGN_CENTER, true);
}

static esp_err_t inv_launch(core_context_t *ctx) {
    memset(&s_inv, 0, sizeof(s_inv));
    s_inv.screen.id = "invaders.screen";
    s_inv.screen.title = "INVADERS";
    s_inv.screen.user_data = &s_inv;
    s_inv.screen.on_input = inv_on_input;
    s_inv.screen.on_update = inv_on_update;
    s_inv.screen.on_render = inv_on_render;
    inv_reset(&s_inv);
    return core_nav_push(ctx, &ctx->nav, &s_inv.screen);
}

const core_app_descriptor_t g_invaders_app = { .id = "invaders", .name = "Invaders", .icon = "I", .launch = inv_launch };
