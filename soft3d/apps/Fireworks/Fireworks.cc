#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include "glcommon/gltools.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <ewoksys/klog.h>

#define WIDTH 1024
#define HEIGHT 768
#define PIX_FORMAT SDL_PIXELFORMAT_ARGB8888
#define MAX_PARTICLES 5000

#define sinf pgl_fast_sin
#define cosf pgl_fast_cos
#define sqrtf pgl_fast_sqrt
#define tanf pgl_fast_tan
#define acosf pgl_fast_acos
#define asinf pgl_fast_asin
#define atanf pgl_fast_atan
#define atanf2 pgl_fast_atan2

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Particle structure - dotted trail effect
typedef struct {
    float x, y;
    float vx, vy;
    float life;
    float maxLife;
    float r, g, b;
    float brightness;
    int active;
} Particle;

// Firework rocket
typedef struct {
    float x, y;
    float vx, vy;
    float r, g, b;
    int active;
} Firework;

// Global variables
SDL_Window* window = NULL;
SDL_Renderer* ren = NULL;
SDL_Texture* tex = NULL;
glContext the_Context;
pix_t* bbufpix = NULL;
int screen_width, screen_height;

Particle particles[MAX_PARTICLES];
int num_particles = 0;

#define MAX_FIREWORKS 5
Firework fireworks[MAX_FIREWORKS];
int next_firework_time = 0;

// Initialization
void setup_context() {
    if (SDL_Init(SDL_INIT_VIDEO)) {
        klog("SDL_Init error: %s\n", SDL_GetError());
        exit(0);
    }

    // Create window with default size, will adjust to actual screen size later
    window = SDL_CreateWindow("Fireworks", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              WIDTH, HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);
    if (!window) exit(0);

    // Get actual window size (screen resolution in fullscreen mode)
    SDL_GetWindowSize(window, &screen_width, &screen_height);
    klog("Fireworks: window size %dx%d\n", screen_width, screen_height);

    ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) exit(0);

    tex = SDL_CreateTexture(ren, PIX_FORMAT, SDL_TEXTUREACCESS_STREAMING, screen_width, screen_height);
    if (!tex) exit(0);

    pgl_set_max_vertices(PGL_SMALL_MAX_VERTICES);
    if (!init_glContext(&the_Context, &bbufpix, screen_width, screen_height)) {
        puts("Failed to initialize glContext");
        exit(0);
    }
}

void cleanup() {
    free_glContext(&the_Context);
    if (tex) {
        SDL_DestroyTexture(tex);
        tex = NULL;
    }
    if (ren) {
        SDL_DestroyRenderer(ren);
        ren = NULL;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    SDL_Quit();
}

float random_float(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

// HSV to RGB conversion
void hsv_to_rgb(float h, float s, float v, float* r, float* g, float* b) {
    int i = (int)(h * 6);
    float f = h * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    switch (i % 6) {
        case 0: *r = v; *g = t; *b = p; break;
        case 1: *r = q; *g = v; *b = p; break;
        case 2: *r = p; *g = v; *b = t; break;
        case 3: *r = p; *g = q; *b = v; break;
        case 4: *r = t; *g = p; *b = v; break;
        case 5: *r = v; *g = p; *b = q; break;
    }
}

// Launch firework - random height, supports multiple simultaneous fireworks
void launch_firework() {
    // Find an available firework slot
    int slot = -1;
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        if (!fireworks[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return; // No available slots
    
    Firework* fw = &fireworks[slot];
    fw->x = random_float(screen_width * 0.1f, screen_width * 0.9f);
    fw->y = screen_height;
    fw->vx = random_float(-0.5f, 0.5f);
    // Speed adjusted based on target height, higher targets need faster speed
    fw->vy = random_float(-12.0f, -18.0f);
    
    float hue = random_float(0.0f, 1.0f);
    hsv_to_rgb(hue, 1.0f, 1.0f, &fw->r, &fw->g, &fw->b);
    
    fw->active = 1;
}

// Create explosion - smaller and more natural range
void create_explosion(float x, float y, float r, float g, float b) {
    // Check if particle array is full
    if (num_particles >= MAX_PARTICLES - 500) return;
    
    int streams = 20 + rand() % 12; // 20-32 streams
    
    for (int s = 0; s < streams; s++) {
        float base_angle = (M_PI * 2 * s) / streams;
        // Add natural random angle variation
        float angle_var = random_float(-0.15f, 0.15f);
        
        // Reduce particles per stream
        int particles_per_stream = 12 + rand() % 8;
        
        for (int i = 0; i < particles_per_stream; i++) {
            if (num_particles >= MAX_PARTICLES) break;
            
            Particle* p = &particles[num_particles++];
            
            float angle = base_angle + angle_var + random_float(-0.08f, 0.08f);
            // Slower speed, more compact range
            float base_speed = 1.2f + (float)i * 0.25f;
            float speed = base_speed * random_float(0.9f, 1.1f);
            
            p->x = x + random_float(-2.0f, 2.0f); // Slight randomness in starting position
            p->y = y + random_float(-2.0f, 2.0f);
            p->vx = cos(angle) * speed;
            p->vy = sin(angle) * speed;
            p->life = 1.0f;
            p->maxLife = random_float(50.0f, 90.0f);
            p->brightness = 1.0f - (float)i / particles_per_stream * 0.25f;
            
            // Color gradient
            float fade = 1.0f - (float)i / particles_per_stream * 0.35f;
            p->r = r * fade;
            p->g = g * fade;
            p->b = b * fade;
            
            p->active = 1;
        }
    }
    
    // Reduce center particles
    int center_particles = 15;
    for (int i = 0; i < center_particles; i++) {
        if (num_particles >= MAX_PARTICLES) break;
        
        Particle* p = &particles[num_particles++];
        
        float angle = random_float(0, M_PI * 2);
        float speed = random_float(0.3f, 1.5f);
        
        p->x = x;
        p->y = y;
        p->vx = cos(angle) * speed;
        p->vy = sin(angle) * speed;
        p->life = 1.0f;
        p->maxLife = random_float(30.0f, 60.0f);
        p->brightness = 1.0f;
        p->r = r;
        p->g = g;
        p->b = b;
        p->active = 1;
    }
}

// Update fireworks - random explosion height
void update_fireworks() {
    for (int i = 0; i < MAX_FIREWORKS; i++) {
        Firework* fw = &fireworks[i];
        if (!fw->active) continue;

        fw->x += fw->vx;
        fw->y += fw->vy;
        fw->vy += 0.1f;

        // Random explosion height: 10%-50% of screen height (Y axis points down, smaller value means higher)
        float explode_height = screen_height * random_float(0.1f, 0.3f);

        if (fw->vy > -1.0f || fw->y < explode_height) {
            create_explosion(fw->x, fw->y, fw->r, fw->g, fw->b);
            fw->active = 0;
        }
    }
}

// Update particles - enhanced gravity effect
void update_particles() {
    int i = 0;
    while (i < num_particles) {
        Particle* p = &particles[i];
        if (!p->active) {
            i++;
            continue;
        }
        
        p->x += p->vx;
        p->y += p->vy;
        p->vy += 0.12f; // Enhanced gravity
        p->life -= 1.0f / p->maxLife;
        
        // Air resistance
        p->vx *= 0.985f;
        p->vy *= 0.985f;
        
        if (p->life <= 0) {
            p->active = 0;
            particles[i] = particles[--num_particles];
        } else {
            i++;
        }
    }
}

// Draw pixel
void set_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < screen_width && y >= 0 && y < screen_height) {
        bbufpix[y * screen_width + x] = color;
    }
}

// Draw pixel with alpha blending
void blend_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (x < 0 || x >= screen_width || y < 0 || y >= screen_height) return;
    
    long idx = (long)y * screen_width + x;
    uint32_t dst = bbufpix[idx];
    
    uint8_t dstR = (dst >> 16) & 0xFF;
    uint8_t dstG = (dst >> 8) & 0xFF;
    uint8_t dstB = dst & 0xFF;
    
    uint8_t outR = (r * a + dstR * (255 - a)) / 255;
    uint8_t outG = (g * a + dstG * (255 - a)) / 255;
    uint8_t outB = (b * a + dstB * (255 - a)) / 255;
    
    bbufpix[idx] = 0xFF000000 | (outR << 16) | (outG << 8) | outB;
}

// Draw glowing pixel - larger and brighter
void draw_glow_pixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, float alpha) {
    if (alpha <= 0) return;

    // Increase brightness
    uint8_t bright_r = (uint8_t)fminf(255, r * 1.5f);
    uint8_t bright_g = (uint8_t)fminf(255, g * 1.5f);
    uint8_t bright_b = (uint8_t)fminf(255, b * 1.5f);

    uint8_t a = (uint8_t)(alpha * 255);

    // Center point - larger 3x3
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            float factor = (dx == 0 && dy == 0) ? 1.0f : 0.85f;
            blend_pixel(x+dx, y+dy, bright_r, bright_g, bright_b, (uint8_t)(a * factor));
        }
    }

    // Surrounding glow - larger 5x5 area
    uint8_t glow_a = a / 2;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            if (abs(dx) <= 1 && abs(dy) <= 1) continue; // Skip center 3x3
            float factor = 1.0f - (abs(dx) + abs(dy)) * 0.15f;
            if (factor > 0) {
                blend_pixel(x+dx, y+dy, bright_r, bright_g, bright_b, (uint8_t)(glow_a * factor));
            }
        }
    }
}

// Draw dotted trail effect - longer trail
void draw_dotted_trail(float x0, float y0, float x1, float y1,
                       uint8_t r, uint8_t g, uint8_t b, float alpha) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float dist = sqrtf(dx * dx + dy * dy);
    
    if (dist < 1.0f) {
        draw_glow_pixel((int)x0, (int)y0, r, g, b, alpha);
        return;
    }
    
    // Dot spacing - one dot every 3-4 pixels, larger grain
    float dot_spacing = 3.5f;
    int num_dots = (int)(dist / dot_spacing);
    if (num_dots < 1) num_dots = 1;
    if (num_dots > 30) num_dots = 30; // 限制最大点数
    
    for (int i = 0; i <= num_dots; i++) {
        float t = (float)i / num_dots;
        float x = x0 + dx * t;
        float y = y0 + dy * t;
        
        // Head bright, tail dim, but tail is brighter (obvious trail)
        float dot_alpha = alpha * (0.6f + 0.4f * (1.0f - t));
        if (dot_alpha > 0.05f) {
            draw_glow_pixel((int)x, (int)y, r, g, b, dot_alpha);
        }
    }
}

// Draw all particles - dotted firework effect, independent of screen ratio
void draw_particles() {
    if (!bbufpix) return;

    for (int i = 0; i < num_particles; i++) {
        Particle* p = &particles[i];
        if (!p->active) continue;

        // Calculate trail - only related to velocity, independent of screen ratio
        float speed = sqrtf(p->vx * p->vx + p->vy * p->vy);
        float tail_len = speed * 3.0f;
        if (tail_len > 18.0f) tail_len = 18.0f;
        if (tail_len < 3.0f) tail_len = 3.0f;

        float x0 = p->x;
        float y0 = p->y;
        // Trail direction is opposite to velocity direction, maintaining isotropy
        float x1 = p->x - p->vx * tail_len * 0.5f;
        float y1 = p->y - p->vy * tail_len * 0.5f;

        uint8_t r = (uint8_t)(p->r * 255 * p->brightness);
        uint8_t g = (uint8_t)(p->g * 255 * p->brightness);
        uint8_t b = (uint8_t)(p->b * 255 * p->brightness);

        // Draw dotted trail
        draw_dotted_trail(x0, y0, x1, y1, r, g, b, p->life);
    }
}

// Draw all rockets
void draw_fireworks() {
    if (!bbufpix) return;

    for (int i = 0; i < MAX_FIREWORKS; i++) {
        Firework* fw = &fireworks[i];
        if (!fw->active) continue;

        int px = (int)fw->x;
        int py = (int)fw->y;

        uint8_t r = (uint8_t)(fw->r * 255);
        uint8_t g = (uint8_t)(fw->g * 255);
        uint8_t b = (uint8_t)(fw->b * 255);

        // Rocket head glow
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                int dist = dx*dx + dy*dy;
                if (dist <= 4) {
                    uint8_t alpha = 255 - dist * 40;
                    blend_pixel(px + dx, py + dy, r, g, b, alpha);
                }
            }
        }

        // Rocket dotted trail
        for (int j = 1; j <= 12; j++) {
            float t = j * 0.8f;
            int tx = (int)(fw->x - fw->vx * t);
            int ty = (int)(fw->y - fw->vy * t);
            uint8_t alpha = (uint8_t)(200 - j * 15);
            draw_glow_pixel(tx, ty, r, g, b, alpha / 255.0f);
        }
    }
}

int handle_events() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE) return 1;
            break;
        case SDL_MOUSEBUTTONDOWN:
            return 1;
        case SDL_WINDOWEVENT:
            // Handle window close event (when xwin closes)
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                return 1;
            }
            break;
        case SDL_QUIT:
            return 1;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    srand((unsigned int)time(NULL));

    setup_context();

    // Random seed initialization for launch timing
    next_firework_time = rand() % 30 + 10;

    int frame_count = 0;

    while (1) {
        if (handle_events()) break;

        // Random launch timing, no need to wait for previous to finish
        if (frame_count >= next_firework_time) {
            launch_firework();
            // Next launch time: 10-60 frames later (about 0.16-1 second)
            next_firework_time = frame_count + rand() % 50 + 10;
        }

        update_fireworks();
        update_particles();

        // Clear screen - pure black background
        if (bbufpix) {
            long total_pixels = (long)screen_width * screen_height;
            for (long i = 0; i < total_pixels; ++i) {
                bbufpix[i] = 0xFF000000;
            }

            draw_fireworks();
            draw_particles();

            SDL_UpdateTexture(tex, NULL, bbufpix, screen_width * sizeof(pix_t));
        }

        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        frame_count++;
        SDL_Delay(16);
    }

    cleanup();
    return 0;
}
