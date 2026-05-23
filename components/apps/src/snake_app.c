#include "core/app_manager.h"
#include "core/context.h"
#include "ui/status_bar.h"

#include <stdio.h>
#include <string.h>

#define SNAKE_GRID_W 30
#define SNAKE_GRID_H 10
#define SNAKE_CELL 4
#define SNAKE_BOARD_X 4
#define SNAKE_BOARD_Y 22
#define SNAKE_MAX_LEN (SNAKE_GRID_W * SNAKE_GRID_H)
#define SNAKE_STEP_MS 180

typedef struct {
    int8_t x;
    int8_t y;
} snake_point_t;

typedef enum {
    SNAKE_DIR_UP = 0,
    SNAKE_DIR_DOWN,
    SNAKE_DIR_LEFT,
    SNAKE_DIR_RIGHT,
} snake_dir_t;

typedef struct {
    core_screen_t screen;
    snake_point_t body[SNAKE_MAX_LEN];
    snake_point_t food;
    snake_dir_t dir;
    snake_dir_t pending_dir;
    uint16_t length;
    uint16_t score;
    uint32_t accumulator_ms;
    uint32_t rng;
    bool running;
    bool game_over;
} snake_state_t;

static snake_state_t s_snake;

static bool points_equal(snake_point_t a, snake_point_t b)
{
    return a.x == b.x && a.y == b.y;
}

static uint32_t snake_rand(snake_state_t *snake)
{
    snake->rng = snake->rng * 1664525U + 1013904223U;
    return snake->rng;
}

static bool snake_occupies(const snake_state_t *snake, snake_point_t point)
{
    for (uint16_t i = 0; i < snake->length; ++i) {
        if (points_equal(snake->body[i], point)) {
            return true;
        }
    }
    return false;
}

static void snake_place_food(snake_state_t *snake)
{
    for (uint16_t tries = 0; tries < SNAKE_MAX_LEN; ++tries) {
        snake_point_t point = {
            .x = (int8_t)(snake_rand(snake) % SNAKE_GRID_W),
            .y = (int8_t)(snake_rand(snake) % SNAKE_GRID_H),
        };
        if (!snake_occupies(snake, point)) {
            snake->food = point;
            return;
        }
    }
    snake->food = (snake_point_t){0, 0};
}

static void snake_reset(snake_state_t *snake)
{
    snake->length = 4;
    snake->score = 0;
    snake->dir = SNAKE_DIR_RIGHT;
    snake->pending_dir = SNAKE_DIR_RIGHT;
    snake->accumulator_ms = 0;
    snake->running = true;
    snake->game_over = false;
    snake->rng ^= 0xA53C9E21U;

    snake->body[0] = (snake_point_t){8, 5};
    snake->body[1] = (snake_point_t){7, 5};
    snake->body[2] = (snake_point_t){6, 5};
    snake->body[3] = (snake_point_t){5, 5};
    snake_place_food(snake);
}

static bool snake_is_reverse(snake_dir_t a, snake_dir_t b)
{
    return (a == SNAKE_DIR_UP && b == SNAKE_DIR_DOWN) ||
           (a == SNAKE_DIR_DOWN && b == SNAKE_DIR_UP) ||
           (a == SNAKE_DIR_LEFT && b == SNAKE_DIR_RIGHT) ||
           (a == SNAKE_DIR_RIGHT && b == SNAKE_DIR_LEFT);
}

static void snake_set_dir(snake_state_t *snake, snake_dir_t dir)
{
    if (!snake_is_reverse(snake->dir, dir)) {
        snake->pending_dir = dir;
    }
}

static bool snake_collides_with_self(const snake_state_t *snake, snake_point_t head, bool grows)
{
    uint16_t limit = grows ? snake->length : snake->length - 1;
    for (uint16_t i = 0; i < limit; ++i) {
        if (points_equal(snake->body[i], head)) {
            return true;
        }
    }
    return false;
}

static void snake_step(snake_state_t *snake)
{
    snake_point_t head = snake->body[0];
    snake->dir = snake->pending_dir;

    switch (snake->dir) {
    case SNAKE_DIR_UP:
        head.y--;
        break;
    case SNAKE_DIR_DOWN:
        head.y++;
        break;
    case SNAKE_DIR_LEFT:
        head.x--;
        break;
    case SNAKE_DIR_RIGHT:
        head.x++;
        break;
    }

    bool out = head.x < 0 || head.x >= SNAKE_GRID_W || head.y < 0 || head.y >= SNAKE_GRID_H;
    bool grows = points_equal(head, snake->food);
    if (out || snake_collides_with_self(snake, head, grows)) {
        snake->running = false;
        snake->game_over = true;
        return;
    }

    uint16_t new_length = snake->length;
    if (grows && new_length < SNAKE_MAX_LEN) {
        new_length++;
        snake->score++;
    }

    for (uint16_t i = new_length - 1; i > 0; --i) {
        snake->body[i] = snake->body[i - 1];
    }
    snake->body[0] = head;
    snake->length = new_length;

    if (grows) {
        snake_place_food(snake);
    }
}

static bool snake_on_input(core_context_t *ctx, core_screen_t *screen, const core_input_event_t *event)
{
    snake_state_t *snake = (snake_state_t *)screen->user_data;
    if (event->phase != CORE_INPUT_PHASE_PRESS && event->phase != CORE_INPUT_PHASE_REPEAT) {
        return false;
    }

    switch (event->action) {
    case CORE_INPUT_UP:
        snake_set_dir(snake, SNAKE_DIR_UP);
        break;
    case CORE_INPUT_DOWN:
        snake_set_dir(snake, SNAKE_DIR_DOWN);
        break;
    case CORE_INPUT_LEFT:
        snake_set_dir(snake, SNAKE_DIR_LEFT);
        break;
    case CORE_INPUT_RIGHT:
        snake_set_dir(snake, SNAKE_DIR_RIGHT);
        break;
    case CORE_INPUT_SELECT:
        if (snake->game_over) {
            snake_reset(snake);
        } else {
            snake->running = !snake->running;
        }
        break;
    default:
        return false;
    }

    core_nav_mark_dirty(&ctx->nav);
    return true;
}

static void snake_on_update(core_context_t *ctx, core_screen_t *screen, uint32_t dt_ms)
{
    snake_state_t *snake = (snake_state_t *)screen->user_data;
    if (!snake->running || snake->game_over) {
        return;
    }

    snake->accumulator_ms += dt_ms;
    while (snake->accumulator_ms >= SNAKE_STEP_MS) {
        snake->accumulator_ms -= SNAKE_STEP_MS;
        snake_step(snake);
        core_nav_mark_dirty(&ctx->nav);
        if (snake->game_over) {
            break;
        }
    }
}

static void draw_cell(ui_t *ui, snake_point_t point, bool fill)
{
    int x = SNAKE_BOARD_X + point.x * SNAKE_CELL;
    int y = SNAKE_BOARD_Y + point.y * SNAKE_CELL;
    if (fill) {
        ui_fill_rect(ui, x, y, SNAKE_CELL, SNAKE_CELL, true);
    } else {
        ui_draw_rect(ui, x, y, SNAKE_CELL, SNAKE_CELL, true);
    }
}

static void snake_on_render(core_context_t *ctx, core_screen_t *screen, ui_t *ui)
{
    (void)ctx;
    snake_state_t *snake = (snake_state_t *)screen->user_data;
    char score[18];
    snprintf(score, sizeof(score), "SCORE %u", snake->score);

    ui_status_bar_render(ui, "SNAKE");
    ui_draw_text(ui, 4, 12, score, true);
    if (snake->game_over) {
        ui_draw_text_aligned(ui, 62, 12, 62, "GAME OVER", UI_ALIGN_RIGHT, true);
    } else if (!snake->running) {
        ui_draw_text_aligned(ui, 82, 12, 42, "PAUSE", UI_ALIGN_RIGHT, true);
    }

    ui_draw_rect(ui, SNAKE_BOARD_X - 1, SNAKE_BOARD_Y - 1,
                 SNAKE_GRID_W * SNAKE_CELL + 2, SNAKE_GRID_H * SNAKE_CELL + 2, true);
    draw_cell(ui, snake->food, false);
    for (uint16_t i = 0; i < snake->length; ++i) {
        draw_cell(ui, snake->body[i], true);
    }
}

static esp_err_t snake_launch(core_context_t *ctx)
{
    memset(&s_snake, 0, sizeof(s_snake));
    s_snake.rng = 0xC6C6F00DU;
    s_snake.screen.id = "snake.screen";
    s_snake.screen.title = "SNAKE";
    s_snake.screen.user_data = &s_snake;
    s_snake.screen.on_input = snake_on_input;
    s_snake.screen.on_update = snake_on_update;
    s_snake.screen.on_render = snake_on_render;
    snake_reset(&s_snake);
    return core_nav_push(ctx, &ctx->nav, &s_snake.screen);
}

const core_app_descriptor_t g_snake_app = {
    .id = "snake",
    .name = "Snake",
    .icon = "S",
    .launch = snake_launch,
};
