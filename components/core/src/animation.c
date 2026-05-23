#include "core/animation.h"

static int32_t ease_value(const core_animation_t *animation)
{
    if (animation->duration_ms == 0 || animation->elapsed_ms >= animation->duration_ms) {
        return animation->to;
    }
    float t = (float)animation->elapsed_ms / (float)animation->duration_ms;
    if (animation->easing == CORE_EASE_OUT_CUBIC) {
        float p = 1.0f - t;
        t = 1.0f - p * p * p;
    }
    return animation->from + (int32_t)((float)(animation->to - animation->from) * t);
}

void core_animation_manager_init(core_animation_manager_t *manager)
{
    if (manager != 0) {
        manager->transition.active = false;
    }
}

void core_animation_start(core_animation_t *animation, int32_t from, int32_t to, uint32_t duration_ms, core_easing_t easing)
{
    if (animation == 0) {
        return;
    }
    animation->active = true;
    animation->from = from;
    animation->to = to;
    animation->value = from;
    animation->elapsed_ms = 0;
    animation->duration_ms = duration_ms;
    animation->easing = easing;
}

void core_animation_update(core_animation_t *animation, uint32_t dt_ms)
{
    if (animation == 0 || !animation->active) {
        return;
    }
    animation->elapsed_ms += dt_ms;
    if (animation->elapsed_ms >= animation->duration_ms) {
        animation->elapsed_ms = animation->duration_ms;
        animation->active = false;
    }
    animation->value = ease_value(animation);
}
