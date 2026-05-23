#include "core/app_manager.h"
#include "core/context.h"
#include "ui/status_bar.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define PADDLE_W 16
#define PADDLE_H 2
#define BALL_SIZE 2
#define BRICK_ROWS 4
#define BRICK_COLS 8
#define BRICK_W 14
#define BRICK_H 4
#define BRICK_GAP 2

typedef struct {
    core_screen_t screen;
    float ball_x, ball_y, ball_dx, ball_dy;
    int paddle_x;
    uint8_t bricks[BRICK_ROWS][BRICK_COLS];
    uint16_t score;
    bool running, game_over;
    bool move_left, move_right;
} breakout_state_t;

static breakout_state_t s_breakout;

static void breakout_reset(breakout_state_t *s) {
    s->ball_x = 64; s->ball_y = 50; s->ball_dx = 1.2f; s->ball_dy = -1.2f;
    s->paddle_x = 64 - PADDLE_W / 2;
    s->score = 0; s->running = true; s->game_over = false;
    s->move_left = false; s->move_right = false;
    memset(s->bricks, 1, sizeof(s->bricks));
}

static bool breakout_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event) {
    breakout_state_t *s = (breakout_state_t *)screen->user_data;
    bool handled = false;

    if (event->phase == CORE_INPUT_PHASE_PRESS || event->phase == CORE_INPUT_PHASE_REPEAT) {
        if (s->game_over && event->action == CORE_INPUT_SELECT) {
            breakout_reset(s);
            handled = true;
        } else {
            switch (event->action) {
                case CORE_INPUT_LEFT: s->move_left = true; s->move_right = false; handled = true; break;
                case CORE_INPUT_RIGHT: s->move_right = true; s->move_left = false; handled = true; break;
                case CORE_INPUT_SELECT: s->running = !s->running; handled = true; break;
                default: break;
            }
        }
    } else if (event->phase == CORE_INPUT_PHASE_RELEASE) {
        if (event->action == CORE_INPUT_LEFT) { s->move_left = false; handled = true; }
        if (event->action == CORE_INPUT_RIGHT) { s->move_right = false; handled = true; }
    }

    if (handled) core_nav_mark_dirty(&ctx->nav);
    return handled;
}

static void breakout_on_update(core_context_t *ctx, core_screen_t *screen, uint32_t dt_ms) {
    breakout_state_t *s = (breakout_state_t *)screen->user_data;
    if (!s->running || s->game_over) return;

    if (s->move_left) s->paddle_x = (s->paddle_x > 0) ? s->paddle_x - 3 : 0;
    if (s->move_right) s->paddle_x = (s->paddle_x < 128 - PADDLE_W) ? s->paddle_x + 3 : 128 - PADDLE_W;

    s->ball_x += s->ball_dx; s->ball_y += s->ball_dy;
    if (s->ball_x <= 0 || s->ball_x >= 128 - BALL_SIZE) s->ball_dx = -s->ball_dx;
    if (s->ball_y <= 10) s->ball_dy = -s->ball_dy;
    if (s->ball_y >= 60 - BALL_SIZE) {
        if (s->ball_x + BALL_SIZE >= s->paddle_x && s->ball_x <= s->paddle_x + PADDLE_W) {
            s->ball_dy = -fabsf(s->ball_dy);
            s->ball_dx = ((s->ball_x + BALL_SIZE/2.0f) - (s->paddle_x + PADDLE_W/2.0f)) / (PADDLE_W/2.0f) * 2.0f;
        } else { s->game_over = true; s->running = false; }
    }
    int bri = (int)((s->ball_y - 12) / (BRICK_H + BRICK_GAP));
    int brj = (int)(s->ball_x / (BRICK_W + BRICK_GAP));
    if (bri >= 0 && bri < BRICK_ROWS && brj >= 0 && brj < BRICK_COLS && s->bricks[bri][brj]) {
        s->bricks[bri][brj] = 0; s->ball_dy = -s->ball_dy; s->score++;
    }
    core_nav_mark_dirty(&ctx->nav);
}

static void breakout_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui) {
    breakout_state_t *s = (breakout_state_t *)screen->user_data;
    ui_status_bar_render(ui, "BREAKOUT");
    char buf[16]; snprintf(buf, sizeof(buf), "SC:%u", s->score);
    ui_draw_text(ui, 2, 12, buf, true);
    ui_fill_rect(ui, s->paddle_x, 60, PADDLE_W, PADDLE_H, true);
    ui_fill_rect(ui, (int)s->ball_x, (int)s->ball_y, BALL_SIZE, BALL_SIZE, true);
    for (int i = 0; i < BRICK_ROWS; i++) {
        for (int j = 0; j < BRICK_COLS; j++) {
            if (s->bricks[i][j]) ui_fill_rect(ui, j * (BRICK_W + BRICK_GAP), 12 + i * (BRICK_H + BRICK_GAP), BRICK_W, BRICK_H, true);
        }
    }
    if (s->game_over) ui_draw_text_aligned(ui, 64, 30, 128, "GAME OVER", UI_ALIGN_CENTER, true);
}

static esp_err_t breakout_launch(core_context_t *ctx) {
    memset(&s_breakout, 0, sizeof(s_breakout));
    s_breakout.screen.id = "breakout.screen";
    s_breakout.screen.title = "BREAKOUT";
    s_breakout.screen.user_data = &s_breakout;
    s_breakout.screen.on_input = breakout_on_input;
    s_breakout.screen.on_update = breakout_on_update;
    s_breakout.screen.on_render = breakout_on_render;
    breakout_reset(&s_breakout);
    return core_nav_push(ctx, &ctx->nav, &s_breakout.screen);
}

const core_app_descriptor_t g_breakout_app = { .id = "breakout", .name = "Breakout", .icon = "B", .launch = breakout_launch };
