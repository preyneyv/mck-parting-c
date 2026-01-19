#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <shared/apps/apps.h>
#include <shared/config.h>
#include <shared/engine.h>
#include <shared/utils/vec.h>

// --- Tunables ---
static const float SHIP_RADIUS = 4.0f;
static const float DRIFT_ACCEL = 4.0f;             // px/s^2 (always applied)
static const float BOOST_ACCEL = 40.0f;            // extra px/s^2 when both buttons held
static const float MAX_SPEED = 12.0f;              // px/s (regular cap)
static const float BOOST_MAX_SPEED = 80.0f;        // px/s (boost cap)
static const float BOOST_BRAKE = 0.5f;             // 1/s (deceleration when boost released)
static const float ANGULAR_ACCEL = 16.0f;          // rad/s^2
static const float ANGULAR_DAMP = 4.0f;            // 1/s
static const float STEER_HEADING_INFLUENCE = 2.0f; // 1/s (higher -> more pull to facing)
static const float CAMERA_FOLLOW = 4.5f;           // 1/s (higher = snappier camera)
static const float STAR_FIELD_W = 320.0f;
static const float STAR_FIELD_H = 200.0f;

static const uint32_t RESTART_HOLD_MS = 900;

enum
{
    ASTEROID_MAX = 12,
    STAR_COUNT = 48,
};
static const uint8_t ASTEROID_R_MIN = 4;
static const uint8_t ASTEROID_R_MAX = 8;
static const float ASTEROID_SPEED_MIN = 8.0f;
static const float ASTEROID_SPEED_MAX = 22.0f;
static const uint32_t ASTEROID_TTL_MIN_MS = 6000;
static const uint32_t ASTEROID_TTL_MAX_MS = 12000;

static const uint32_t SPAWN_INTERVAL_BASE_MS = 1600;
static const uint32_t SPAWN_INTERVAL_MIN_MS = 400;
static const uint32_t SPAWN_INTERVAL_DECAY_PER_SEC = 30; // interval reduction each second

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct
{
    bool active;
    vec2f_t pos;
    vec2f_t vel;
    uint8_t r;
    uint32_t ttl_ms;
} asteroid_t;

typedef struct
{
    vec2f_t pos;
    vec2f_t vel;
    float angle;
    float angular_vel;
    vec2f_t cam_pos;

    uint32_t last_tick_ms;
    uint32_t elapsed_ms;
    uint32_t next_spawn_at;
    uint32_t restart_hold_ms;

    bool game_over;
    uint32_t score;

    vec2f_t stars[STAR_COUNT];

    asteroid_t ast[ASTEROID_MAX];
} state_t;

static state_t state;

static inline uint32_t now_ms()
{
    return g_engine.tick; // tick is 1000 Hz
}

static inline float clampf(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static inline float randf()
{
    return (float)rand() / (float)RAND_MAX;
}

static inline float wrapf(float v, float max)
{
    if (v < 0)
        v += max;
    if (v >= max)
        v -= max;
    return v;
}

static inline vec2f_t world_to_screen(vec2f_t world)
{
    return (vec2f_t){
        .x = world.x - state.cam_pos.x + (float)DISP_WIDTH * 0.5f,
        .y = world.y - state.cam_pos.y + (float)DISP_HEIGHT * 0.5f,
    };
}

static inline vec2f_t vec2f_scale(vec2f_t v, float s)
{
    return (vec2f_t){.x = v.x * s, .y = v.y * s};
}

static inline vec2f_t vec2f_add_local(vec2f_t a, vec2f_t b)
{
    return (vec2f_t){.x = a.x + b.x, .y = a.y + b.y};
}

static inline float vec2f_len(vec2f_t v)
{
    return sqrtf(v.x * v.x + v.y * v.y);
}

static inline vec2f_t vec2f_norm(vec2f_t v)
{
    float l = vec2f_len(v);
    if (l <= 0.0001f)
        return (vec2f_t){.x = 0.0f, .y = 0.0f};
    return vec2f_scale(v, 1.0f / l);
}

static inline vec2f_t vec2f_lerp(vec2f_t a, vec2f_t b, float t)
{
    return (vec2f_t){.x = a.x + (b.x - a.x) * t, .y = a.y + (b.y - a.y) * t};
}

static inline vec2f_t rotate_vec(vec2f_t v, float rad)
{
    float c = cosf(rad);
    float s = sinf(rad);
    return (vec2f_t){.x = v.x * c - v.y * s, .y = v.x * s + v.y * c};
}

static void init_starfield(vec2f_t center)
{
    for (uint8_t i = 0; i < STAR_COUNT; i++)
    {
        float x = center.x - STAR_FIELD_W * 0.5f + randf() * STAR_FIELD_W;
        float y = center.y - STAR_FIELD_H * 0.5f + randf() * STAR_FIELD_H;
        state.stars[i] = (vec2f_t){.x = x, .y = y};
    }
}

static void update_starfield(vec2f_t center)
{
    float half_w = STAR_FIELD_W * 0.5f;
    float half_h = STAR_FIELD_H * 0.5f;
    for (uint8_t i = 0; i < STAR_COUNT; i++)
    {
        vec2f_t *s = &state.stars[i];
        if (s->x < center.x - half_w)
        {
            s->x = center.x + half_w;
            s->y = center.y - half_h + randf() * STAR_FIELD_H;
        }
        else if (s->x > center.x + half_w)
        {
            s->x = center.x - half_w;
            s->y = center.y - half_h + randf() * STAR_FIELD_H;
        }

        if (s->y < center.y - half_h)
        {
            s->y = center.y + half_h;
            s->x = center.x - half_w + randf() * STAR_FIELD_W;
        }
        else if (s->y > center.y + half_h)
        {
            s->y = center.y - half_h;
            s->x = center.x - half_w + randf() * STAR_FIELD_W;
        }
    }
}

static void reset_state()
{
    memset(&state, 0, sizeof(state));
    state.pos = (vec2f_t){.x = DISP_WIDTH / 2.0f, .y = DISP_HEIGHT / 2.0f};
    state.cam_pos = state.pos;
    state.angle = -M_PI * 0.5f;
    state.last_tick_ms = now_ms();
    state.next_spawn_at = state.last_tick_ms + SPAWN_INTERVAL_BASE_MS;
    init_starfield(state.cam_pos);
}

static void spawn_asteroid(uint32_t current_ms)
{
    uint8_t slot = ASTEROID_MAX;
    for (uint8_t i = 0; i < ASTEROID_MAX; i++)
    {
        if (!state.ast[i].active)
        {
            slot = i;
            break;
        }
    }
    if (slot == ASTEROID_MAX)
        return;

    uint8_t edge = rand() % 4;
    float x = 0.0f;
    float y = 0.0f;
    float half_w = (float)DISP_WIDTH * 0.5f;
    float half_h = (float)DISP_HEIGHT * 0.5f;
    float cam_x = state.cam_pos.x;
    float cam_y = state.cam_pos.y;
    if (edge == 0)
    {
        x = cam_x - half_w - 4.0f;
        y = cam_y - half_h + randf() * (float)DISP_HEIGHT;
    }
    else if (edge == 1)
    {
        x = cam_x + half_w + 4.0f;
        y = cam_y - half_h + randf() * (float)DISP_HEIGHT;
    }
    else if (edge == 2)
    {
        x = cam_x - half_w + randf() * (float)DISP_WIDTH;
        y = cam_y - half_h - 4.0f;
    }
    else
    {
        x = cam_x - half_w + randf() * (float)DISP_WIDTH;
        y = cam_y + half_h + 4.0f;
    }

    vec2f_t center = state.cam_pos;
    vec2f_t to_center = (vec2f_t){.x = center.x - x, .y = center.y - y};
    float base_ang = atan2f(to_center.y, to_center.x);
    float jitter = (randf() - 0.5f) * 0.8f; // +/- 0.4 rad
    float ang = base_ang + jitter;

    float speed = ASTEROID_SPEED_MIN + randf() * (ASTEROID_SPEED_MAX - ASTEROID_SPEED_MIN);
    vec2f_t vel = (vec2f_t){.x = cosf(ang) * speed, .y = sinf(ang) * speed};

    uint8_t r = ASTEROID_R_MIN + (rand() % (ASTEROID_R_MAX - ASTEROID_R_MIN + 1));
    uint32_t ttl = ASTEROID_TTL_MIN_MS + (uint32_t)(randf() * (ASTEROID_TTL_MAX_MS - ASTEROID_TTL_MIN_MS));

    state.ast[slot] = (asteroid_t){
        .active = true,
        .pos = (vec2f_t){.x = x, .y = y},
        .vel = vel,
        .r = r,
        .ttl_ms = ttl,
    };

    (void)current_ms;
}

static void update_ship(float dt_s, uint32_t dt_ms)
{
    int8_t input = 0;
    if (BUTTON_PRESSED(BUTTON_LEFT))
        input -= 1;
    if (BUTTON_PRESSED(BUTTON_RIGHT))
        input += 1;

    state.angular_vel += (float)input * ANGULAR_ACCEL * dt_s;
    state.angular_vel *= expf(-ANGULAR_DAMP * dt_s);
    state.angle += state.angular_vel * dt_s;

    vec2f_t forward = (vec2f_t){.x = cosf(state.angle), .y = sinf(state.angle)};

    bool boosting = BUTTON_PRESSED(BUTTON_LEFT) && BUTTON_PRESSED(BUTTON_RIGHT);
    float speed = vec2f_len(state.vel);
    float accel = 0.0f;
    if (boosting)
    {
        if (speed < BOOST_MAX_SPEED)
            accel = DRIFT_ACCEL + BOOST_ACCEL;
    }
    else
    {
        if (speed < MAX_SPEED)
            accel = DRIFT_ACCEL;
    }

    state.vel.x += forward.x * accel * dt_s;
    state.vel.y += forward.y * accel * dt_s;

    speed = vec2f_len(state.vel);
    if (!boosting && speed > MAX_SPEED)
    {
        float brake = expf(-BOOST_BRAKE * dt_s);
        state.vel = vec2f_scale(state.vel, brake);
        speed = vec2f_len(state.vel);
    }

    if (speed > 0.01f)
    {
        vec2f_t cur_dir = vec2f_scale(state.vel, 1.0f / speed);
        float blend = clampf(STEER_HEADING_INFLUENCE * dt_s, 0.0f, 1.0f);
        vec2f_t new_dir = vec2f_norm(vec2f_lerp(cur_dir, forward, blend));
        state.vel = vec2f_scale(new_dir, speed);
    }

    state.pos.x += state.vel.x * dt_s;
    state.pos.y += state.vel.y * dt_s;

    state.elapsed_ms += dt_ms;
}

static void update_asteroids(float dt_s, uint32_t current_ms)
{
    for (uint8_t i = 0; i < ASTEROID_MAX; i++)
    {
        if (!state.ast[i].active)
            continue;

        asteroid_t *a = &state.ast[i];
        if (a->ttl_ms <= 0)
        {
            a->active = false;
            continue;
        }

        uint32_t step_ms = (uint32_t)(dt_s * 1000.0f);
        if (step_ms > 0 && a->ttl_ms > step_ms)
            a->ttl_ms -= step_ms;
        else
            a->ttl_ms = 0;

        a->pos.x += a->vel.x * dt_s;
        a->pos.y += a->vel.y * dt_s;

        // collision check
        float dx = a->pos.x - state.pos.x;
        float dy = a->pos.y - state.pos.y;
        float rr = SHIP_RADIUS + (float)a->r;
        if ((dx * dx + dy * dy) <= (rr * rr))
        {
            state.game_over = true;
            state.score = state.elapsed_ms / 1000;
            state.restart_hold_ms = 0;
            break;
        }
    }

    if (current_ms >= state.next_spawn_at)
    {
        uint32_t elapsed_s = state.elapsed_ms / 1000;
        uint32_t interval = SPAWN_INTERVAL_BASE_MS;
        uint32_t decay = elapsed_s * SPAWN_INTERVAL_DECAY_PER_SEC;
        if (decay < SPAWN_INTERVAL_BASE_MS)
            interval = SPAWN_INTERVAL_BASE_MS - decay;
        if (interval < SPAWN_INTERVAL_MIN_MS)
            interval = SPAWN_INTERVAL_MIN_MS;

        spawn_asteroid(current_ms);
        state.next_spawn_at = current_ms + interval;
    }
}

static void tick()
{
    uint32_t now = now_ms();
    uint32_t dt_ms = now - state.last_tick_ms;
    if (dt_ms == 0)
        return;

    state.last_tick_ms = now;
    float dt_s = (float)dt_ms / 1000.0f;

    if (state.game_over)
    {
        if (BUTTON_PRESSED(BUTTON_LEFT) && BUTTON_PRESSED(BUTTON_RIGHT))
        {
            state.restart_hold_ms += dt_ms;
            if (state.restart_hold_ms >= RESTART_HOLD_MS)
            {
                reset_state();
            }
        }
        else
        {
            state.restart_hold_ms = 0;
        }
        return;
    }

    update_ship(dt_s, dt_ms);

    float cam_blend = 1.0f - expf(-CAMERA_FOLLOW * dt_s);
    state.cam_pos = vec2f_lerp(state.cam_pos, state.pos, cam_blend);
    update_starfield(state.cam_pos);
    update_asteroids(dt_s, now);
}

static void draw_ship(u8g2_t *u8g2)
{
    vec2f_t forward = (vec2f_t){.x = cosf(state.angle), .y = sinf(state.angle)};
    vec2f_t left = rotate_vec(forward, 2.45f);
    vec2f_t right = rotate_vec(forward, -2.45f);

    vec2f_t nose_w = vec2f_add_local(state.pos, vec2f_scale(forward, SHIP_RADIUS + 3.0f));
    vec2f_t p1_w = vec2f_add_local(state.pos, vec2f_scale(left, SHIP_RADIUS));
    vec2f_t p2_w = vec2f_add_local(state.pos, vec2f_scale(right, SHIP_RADIUS));

    vec2f_t nose = world_to_screen(nose_w);
    vec2f_t p1 = world_to_screen(p1_w);
    vec2f_t p2 = world_to_screen(p2_w);

    u8g2_DrawTriangle(u8g2,
                      (int16_t)nose.x, (int16_t)nose.y,
                      (int16_t)p1.x, (int16_t)p1.y,
                      (int16_t)p2.x, (int16_t)p2.y);

    if (BUTTON_PRESSED(BUTTON_LEFT) && BUTTON_PRESSED(BUTTON_RIGHT))
    {
        vec2f_t tail_w = vec2f_add_local(state.pos, vec2f_scale(forward, -(SHIP_RADIUS + 2.0f)));
        vec2f_t tail = world_to_screen(tail_w);
        u8g2_DrawLine(u8g2, (int16_t)tail.x, (int16_t)tail.y,
                      (int16_t)(tail.x - forward.x * 4.0f),
                      (int16_t)(tail.y - forward.y * 4.0f));
    }
}

static void draw_asteroids(u8g2_t *u8g2)
{
    for (uint8_t i = 0; i < ASTEROID_MAX; i++)
    {
        if (!state.ast[i].active)
            continue;

        asteroid_t *a = &state.ast[i];
        vec2f_t screen = world_to_screen(a->pos);
        if (screen.x < -(int16_t)a->r || screen.x > (DISP_WIDTH + a->r) ||
            screen.y < -(int16_t)a->r || screen.y > (DISP_HEIGHT + a->r))
            continue;
        u8g2_DrawCircle(u8g2, (int16_t)screen.x, (int16_t)screen.y, a->r, U8G2_DRAW_ALL);
    }
}

static void draw_starfield(u8g2_t *u8g2)
{
    for (uint8_t i = 0; i < STAR_COUNT; i++)
    {
        vec2f_t screen = world_to_screen(state.stars[i]);
        if (screen.x < 0 || screen.x >= DISP_WIDTH || screen.y < 0 || screen.y >= DISP_HEIGHT)
            continue;
        u8g2_DrawPixel(u8g2, (int16_t)screen.x, (int16_t)screen.y);
    }
}

static void draw_hud(u8g2_t *u8g2)
{
    u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
    u8g2_SetDrawColor(u8g2, 1);

    char buf[24];
    snprintf(buf, sizeof(buf), "T:%lus", (unsigned long)(state.elapsed_ms / 1000));
    u8g2_DrawStr(u8g2, 0, 6, buf);

    float speed = vec2f_len(state.vel);
    snprintf(buf, sizeof(buf), "V:%d", (int)speed);
    u8g2_DrawStr(u8g2, 78, 6, buf);
}

static void draw_game_over(u8g2_t *u8g2)
{
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    const char *msg = "GAME OVER";
    uint16_t w = u8g2_GetStrWidth(u8g2, msg);
    u8g2_DrawStr(u8g2, (DISP_WIDTH - w) / 2, 20, msg);

    u8g2_SetFont(u8g2, u8g2_font_5x7_tr);
    char buf[24];
    snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)state.score);
    u8g2_DrawStr(u8g2, 20, 36, buf);

    const char *hint = "Hold both";
    w = u8g2_GetStrWidth(u8g2, hint);
    u8g2_DrawStr(u8g2, (DISP_WIDTH - w) / 2, 52, hint);
}

static void frame()
{
    u8g2_t *u8g2 = display_get_u8g2(&g_engine.display);
    u8g2_SetDrawColor(u8g2, 1);

    draw_starfield(u8g2);
    draw_asteroids(u8g2);
    draw_ship(u8g2);
    draw_hud(u8g2);

    if (state.game_over)
    {
        draw_game_over(u8g2);
    }
}

static void enter()
{
    reset_state();
}

app_t app_asteroids = {
    .name = "asteroids",
    .enter = enter,
    .tick = tick,
    .frame = frame,
};
