#include "core/app_manager.h"
#include "core/context.h"
#include "ui/status_bar.h"
#include "esp_random.h"
#include <stdio.h>
#include <string.h>

#define TETRIS_COLS 10
#define TETRIS_ROWS 20
#define TETRIS_BLOCK_SIZE 2
#define TETRIS_X_OFFSET 54
#define TETRIS_Y_OFFSET 20
#define TETRIS_STEP_MS 500

static const uint16_t SHAPES[7][4] = {
    {0x0F00, 0x4444, 0x0F00, 0x4444}, // I
    {0x4460, 0x0E80, 0xC440, 0x2E00}, // L
    {0x44C0, 0x8E00, 0x6440, 0x0E20}, // J
    {0x0660, 0x0660, 0x0660, 0x0660}, // O
    {0x06C0, 0x4620, 0x06C0, 0x4620}, // S
    {0x0E40, 0x4C40, 0x4E00, 0x8C80}, // T
    {0x0C60, 0x2640, 0x0C60, 0x2640}  // Z
};

typedef struct {
    core_screen_t screen;
    uint8_t board[TETRIS_ROWS][TETRIS_COLS];
    int8_t cur_x, cur_y;
    uint8_t cur_type, cur_rot;
    uint32_t score;
    uint32_t accumulator_ms;
    bool running;
    bool game_over;
} tetris_state_t;

static tetris_state_t s_tetris;

static bool check_collision(tetris_state_t *t, int8_t x, int8_t y, uint8_t rot) {
    uint16_t shape = SHAPES[t->cur_type][rot];
    for (int i = 0; i < 16; i++) {
        if (shape & (1 << (15 - i))) {
            int8_t bx = x + (i % 4);
            int8_t by = y + (i / 4);
            if (bx < 0 || bx >= TETRIS_COLS || by >= TETRIS_ROWS) return true;
            if (by >= 0 && t->board[by][bx]) return true;
        }
    }
    return false;
}

static void spawn_piece(tetris_state_t *t) {
    t->cur_type = (uint8_t)(esp_random() % 7);
    t->cur_rot = 0;
    t->cur_x = TETRIS_COLS / 2 - 2;
    t->cur_y = 0;
    if (check_collision(t, t->cur_x, t->cur_y, t->cur_rot)) {
        t->game_over = true;
        t->running = false;
    }
}

static void lock_piece(tetris_state_t *t) {
    uint16_t shape = SHAPES[t->cur_type][t->cur_rot];
    for (int i = 0; i < 16; i++) {
        if (shape & (1 << (15 - i))) {
            int8_t bx = t->cur_x + (i % 4);
            int8_t by = t->cur_y + (i / 4);
            if (by >= 0) t->board[by][bx] = 1;
        }
    }
    for (int y = TETRIS_ROWS - 1; y >= 0; y--) {
        bool full = true;
        for (int x = 0; x < TETRIS_COLS; x++) if (!t->board[y][x]) full = false;
        if (full) {
            t->score += 10;
            for (int ty = y; ty > 0; ty--) memcpy(t->board[ty], t->board[ty - 1], TETRIS_COLS);
            memset(t->board[0], 0, TETRIS_COLS);
            y++;
        }
    }
    spawn_piece(t);
}

static void tetris_reset(tetris_state_t *t) {
    memset(t->board, 0, sizeof(t->board));
    t->score = 0;
    t->game_over = false;
    t->running = true;
    spawn_piece(t);
}

static bool tetris_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event) {
    tetris_state_t *t = (tetris_state_t *)screen->user_data;
    if (event->phase != CORE_INPUT_PHASE_PRESS) return false;
    if (t->game_over) {
        if (event->action == CORE_INPUT_SELECT) tetris_reset(t);
        return true;
    }
    switch (event->action) {
        case CORE_INPUT_LEFT: if (!check_collision(t, t->cur_x - 1, t->cur_y, t->cur_rot)) t->cur_x--; break;
        case CORE_INPUT_RIGHT: if (!check_collision(t, t->cur_x + 1, t->cur_y, t->cur_rot)) t->cur_x++; break;
        case CORE_INPUT_DOWN: if (!check_collision(t, t->cur_x, t->cur_y + 1, t->cur_rot)) t->cur_y++; break;
        case CORE_INPUT_UP: {
            uint8_t next_rot = (t->cur_rot + 1) % 4;
            if (!check_collision(t, t->cur_x, t->cur_y, next_rot)) t->cur_rot = next_rot;
            break;
        }
        case CORE_INPUT_SELECT: t->running = !t->running; break;
        default: return false;
    }
    core_nav_mark_dirty(&ctx->nav);
    return true;
}

static void tetris_on_update(core_context_t *ctx, core_screen_t *screen, uint32_t dt_ms) {
    tetris_state_t *t = (tetris_state_t *)screen->user_data;
    if (!t->running || t->game_over) return;
    t->accumulator_ms += dt_ms;
    if (t->accumulator_ms >= TETRIS_STEP_MS) {
        t->accumulator_ms = 0;
        if (!check_collision(t, t->cur_x, t->cur_y + 1, t->cur_rot)) t->cur_y++;
        else lock_piece(t);
        core_nav_mark_dirty(&ctx->nav);
    }
}

static void tetris_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui) {
    tetris_state_t *t = (tetris_state_t *)screen->user_data;
    ui_status_bar_render(ui, "TETRIS");
    char buf[16]; snprintf(buf, sizeof(buf), "SC:%lu", t->score);
    ui_draw_text(ui, 2, 12, buf, true);
    ui_draw_rect(ui, TETRIS_X_OFFSET - 1, TETRIS_Y_OFFSET - 1, TETRIS_COLS * TETRIS_BLOCK_SIZE + 2, TETRIS_ROWS * TETRIS_BLOCK_SIZE + 2, true);
    for (int y = 0; y < TETRIS_ROWS; y++) {
        for (int x = 0; x < TETRIS_COLS; x++) {
            if (t->board[y][x]) ui_fill_rect(ui, TETRIS_X_OFFSET + x * TETRIS_BLOCK_SIZE, TETRIS_Y_OFFSET + y * TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE, true);
        }
    }
    if (t->running || t->game_over) {
        uint16_t shape = SHAPES[t->cur_type][t->cur_rot];
        for (int i = 0; i < 16; i++) {
            if (shape & (1 << (15 - i))) {
                int bx = t->cur_x + (i % 4), by = t->cur_y + (i / 4);
                if (by >= 0) ui_fill_rect(ui, TETRIS_X_OFFSET + bx * TETRIS_BLOCK_SIZE, TETRIS_Y_OFFSET + by * TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE, TETRIS_BLOCK_SIZE, true);
            }
        }
    }
    if (t->game_over) ui_draw_text_aligned(ui, 64, 30, 128, "GAME OVER", UI_ALIGN_CENTER, true);
    else if (!t->running) ui_draw_text_aligned(ui, 64, 30, 128, "PAUSED", UI_ALIGN_CENTER, true);
}

static esp_err_t tetris_launch(core_context_t *ctx) {
    memset(&s_tetris, 0, sizeof(s_tetris));
    s_tetris.screen.id = "tetris.screen";
    s_tetris.screen.title = "TETRIS";
    s_tetris.screen.user_data = &s_tetris;
    s_tetris.screen.on_input = tetris_on_input;
    s_tetris.screen.on_update = tetris_on_update;
    s_tetris.screen.on_render = tetris_on_render;
    tetris_reset(&s_tetris);
    return core_nav_push(ctx, &ctx->nav, &s_tetris.screen);
}

const core_app_descriptor_t g_tetris_app = { .id = "tetris", .name = "Tetris", .icon = "T", .launch = tetris_launch };
