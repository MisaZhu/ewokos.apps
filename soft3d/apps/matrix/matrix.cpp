#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "glcommon/gltools.h"
#include "glcommon/gltext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

#include <ewoksys/klog.h>

#define WIDTH 640
#define HEIGHT 480
#define FONT_SIZE 14
#define FONT_PATH "/usr/system/fonts/Hack-Regular.ttf"
#define MAX_WIDTH 3840
#define MAX_HEIGHT 2160

#define PIX_FORMAT SDL_PIXELFORMAT_ARGB8888

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

const char* MATRIX_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@#$%^&*()_+-=[]{}|;:,.<>?";

unsigned char* font_buffer = NULL;
stbtt_fontinfo font;
int font_ascent, font_descent, font_linegap;
float font_scale = 0;
#define MAX_CHAR_BITMAP_SIZE 64

int is_font_loaded = 0;

#define MAX_COLUMN_LENGTH 100

struct GlyphCacheEntry {
    unsigned char bitmap[MAX_CHAR_BITMAP_SIZE * MAX_CHAR_BITMAP_SIZE];
    int w;
    int h;
    int x0, y0;
    int valid;
};

#define MAX_GLYPH_CACHE 256
static GlyphCacheEntry glyph_cache[MAX_GLYPH_CACHE];
static int glyph_cache_initialized = 0;

void init_glyph_cache() {
    if (glyph_cache_initialized) return;
    glyph_cache_initialized = 1;
    memset(glyph_cache, 0, sizeof(glyph_cache));
}

extern int width;
extern int height;
extern pix_t* bbufpix;

GlyphCacheEntry* get_glyph_cache(char c) {
    unsigned char uc = (unsigned char)c;
    GlyphCacheEntry* entry = &glyph_cache[uc];
    if (entry->valid) {
        return entry;
    }

    int glyph = stbtt_FindGlyphIndex(&font, uc);
    int empty = stbtt_IsGlyphEmpty(&font, glyph);
    int gx0 = 0, gy0 = 0, gx1 = 0, gy1 = 0;
    int has_box = stbtt_GetGlyphBox(&font, glyph, &gx0, &gy0, &gx1, &gy1);

    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&font, uc, font_scale, font_scale, &x0, &y0, &x1, &y1);

    int w = x1 - x0;
    int h = y1 - y0;

    entry->w = w;
    entry->h = h;
    entry->x0 = x0;
    entry->y0 = y0;

    if (w > 0 && h > 0 && w <= MAX_CHAR_BITMAP_SIZE && h <= MAX_CHAR_BITMAP_SIZE) {
        memset(entry->bitmap, 0, sizeof(entry->bitmap));
        stbtt_MakeCodepointBitmap(&font, entry->bitmap, w, h, w, font_scale, font_scale, uc);
    }
    entry->valid = 1;
    return entry;
}

static void draw_fallback_block(int x, int y, int cell_w, int cell_h, uint32_t color) {
    if (!bbufpix || cell_w <= 0 || cell_h <= 0) return;

    int block_w = cell_w * 3 / 5;
    int block_h = cell_h * 4 / 5;
    if (block_w < 2) block_w = 2;
    if (block_h < 2) block_h = 2;
    if (block_w > cell_w) block_w = cell_w;
    if (block_h > cell_h) block_h = cell_h;

    int start_x = x + (cell_w - block_w) / 2;
    int start_y = y + (cell_h - block_h) / 2;

    for (int py = start_y; py < start_y + block_h; ++py) {
        if (py < 0 || py >= height) continue;
        for (int px = start_x; px < start_x + block_w; ++px) {
            if (px < 0 || px >= width) continue;
            bbufpix[(long)py * width + px] = color;
        }
    }
}

#define DIGIT_HEIGHT 14
#define DIGIT_WIDTH 9
#define DIGIT_SPACING 1

static const char* DIGIT_PATTERNS[10] = {
    "#########"
    "#########"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "#########"
    "#########",
    "   ##    "
    "  ###    "
    " ####    "
    "   ##    "
    "   ##    "
    "   ##    "
    "   ##    "
    "   ##    "
    "   ##    "
    "   ##    "
    "   ##    "
    "   ##    "
    " ########"
    " ########",
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "#########"
    "#########"
    "###      "
    "###      "
    "###      "
    "###      "
    "#########"
    "#########",
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "#########"
    "#########",
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "      ###",
    "#########"
    "#########"
    "###      "
    "###      "
    "###      "
    "###      "
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "#########"
    "#########",
    "#########"
    "#########"
    "###      "
    "###      "
    "###      "
    "###      "
    "#########"
    "#########"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "#########"
    "#########",
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "    ###  "
    "    ###  "
    "  ###    "
    "  ###    "
    "###      "
    "###      "
    "###      "
    "###      ",
    "#########"
    "#########"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "#########"
    "#########"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "#########"
    "#########",
    "#########"
    "#########"
    "##     ##"
    "##     ##"
    "##     ##"
    "##     ##"
    "#########"
    "#########"
    "      ###"
    "      ###"
    "      ###"
    "      ###"
    "#########"
    "#########"
};

static const char* COLON_PATTERN =
    "         "
    "         "
    "   ###   "
    "   ###   "
    "   ###   "
    "   ###   "
    "         "
    "         "
    "   ###   "
    "   ###   "
    "   ###   "
    "   ###   "
    "         "
    "         ";

static bool get_time_string(char* buf, size_t buf_size) {
    time_t now = time(NULL);
    if(now < 100000)
        return false;
    struct tm time_info;
    localtime_r(&now, &time_info);
    snprintf(buf, buf_size, "%02d:%02d:%02d", time_info.tm_hour, time_info.tm_min, time_info.tm_sec);
    return true;
}

int g_char_scale = 6;

char get_random_matrix_char() {
    size_t char_count = strlen(MATRIX_CHARS);
    if (char_count == 0) return 'A';
    return MATRIX_CHARS[rand() % char_count];
}

void render_digit_with_chars(int digit, int x, int y, int char_scale, uint32_t color) {
    const char* pattern = DIGIT_PATTERNS[digit];
    for (int row = 0; row < DIGIT_HEIGHT; row++) {
        for (int col = 0; col < DIGIT_WIDTH; col++) {
            if (pattern[row * DIGIT_WIDTH + col] == '#') {
                int screen_x = x + col * char_scale;
                int screen_y = y + row * char_scale;
                if (!is_font_loaded) {
                    draw_fallback_block(screen_x, screen_y, char_scale, char_scale, color);
                    continue;
                }
                char c = get_random_matrix_char();
                GlyphCacheEntry* glyph = get_glyph_cache(c);
                if (glyph->w <= 0 || glyph->h <= 0) {
                    draw_fallback_block(screen_x, screen_y, char_scale, char_scale, color);
                    continue;
                }
                int baseline_y = screen_y + (int)(font_ascent * font_scale) + glyph->y0;
                for (int gy = 0; gy < glyph->h; gy++) {
                    int py = baseline_y + gy;
                    if (py < 0 || py >= height) continue;
                    for (int gx = 0; gx < glyph->w; gx++) {
                        int px = screen_x + glyph->x0 + gx;
                        if (px < 0 || px >= width) continue;
                        unsigned char alpha = glyph->bitmap[gy * glyph->w + gx];
                        if (alpha > 0) {
                            long idx = ((long)py * width) + px;
                            bbufpix[idx] = color;
                        }
                    }
                }
            }
        }
    }
}

void render_colon_with_chars(int x, int y, int char_scale, uint32_t color) {
    for (int row = 0; row < DIGIT_HEIGHT; row++) {
        for (int col = 0; col < DIGIT_WIDTH; col++) {
            if (COLON_PATTERN[row * DIGIT_WIDTH + col] == '#') {
                int screen_x = x + col * char_scale;
                int screen_y = y + row * char_scale;
                if (!is_font_loaded) {
                    draw_fallback_block(screen_x, screen_y, char_scale, char_scale, color);
                    continue;
                }
                char c = get_random_matrix_char();
                GlyphCacheEntry* glyph = get_glyph_cache(c);
                if (glyph->w <= 0 || glyph->h <= 0) {
                    draw_fallback_block(screen_x, screen_y, char_scale, char_scale, color);
                    continue;
                }
                int baseline_y = screen_y + (int)(font_ascent * font_scale) + glyph->y0;
                for (int gy = 0; gy < glyph->h; gy++) {
                    int py = baseline_y + gy;
                    if (py < 0 || py >= height) continue;
                    for (int gx = 0; gx < glyph->w; gx++) {
                        int px = screen_x + glyph->x0 + gx;
                        if (px < 0 || px >= width) continue;
                        unsigned char alpha = glyph->bitmap[gy * glyph->w + gx];
                        if (alpha > 0) {
                            long idx = ((long)py * width) + px;
                            bbufpix[idx] = color;
                        }
                    }
                }
            }
        }
    }
}

void render_matrix_time(const char* time_str, int start_x, int start_y, int char_scale, uint32_t color) {
    int x = start_x;
    int digit_w = DIGIT_WIDTH * char_scale;
    int spacing = DIGIT_SPACING * char_scale;

    for (int i = 0; time_str[i] != 0; i++) {
        char c = time_str[i];
        if (c == ':') {
            render_colon_with_chars(x, start_y, char_scale, color);
            x += digit_w + spacing;
        } else if (c >= '0' && c <= '9') {
            render_digit_with_chars(c - '0', x, start_y, char_scale, color);
            x += digit_w + spacing;
        }
    }
}

void render_time_overlay() {
    char time_str[16];
    if(!get_time_string(time_str, sizeof(time_str)))
        return;

    int num_digits = 6;
    int num_colons = 2;
    int num_gaps = num_digits + num_colons - 1;
    int time_chars_w = num_digits * DIGIT_WIDTH + num_colons * DIGIT_WIDTH + num_gaps * DIGIT_SPACING;
    int time_chars_h = DIGIT_HEIGHT;

    int target_time_width = width * 2 / 3;
    int target_time_height = height / 2;

    int char_scale_x = target_time_width / time_chars_w;
    int char_scale_y = target_time_height / time_chars_h;
    int char_scale = char_scale_x < char_scale_y ? char_scale_x : char_scale_y;
    if (char_scale < 4) char_scale = 4;
    if (char_scale > 30) char_scale = 30;

    int digit_w = DIGIT_WIDTH * char_scale;
    int spacing = DIGIT_SPACING * char_scale;
    int actual_time_width = num_digits * digit_w + num_colons * digit_w + num_gaps * spacing;
    int actual_time_height = time_chars_h * char_scale;
    int time_x = (width - actual_time_width) / 2;
    int time_y = (height - actual_time_height) / 2;

    render_matrix_time(time_str, time_x, time_y, char_scale, 0xFF00FF00);
}

struct Column {
    float y;
    float speed;
    int length;
    char chars[MAX_COLUMN_LENGTH];

    Column() {
        reset();
    }

    void reset() {
        y = (float)(rand() % HEIGHT) - HEIGHT;
        speed = 2.0f + (float)(rand() % 100) / 50.0f;
        length = 10 + rand() % 40;
        if (length > MAX_COLUMN_LENGTH) length = MAX_COLUMN_LENGTH;
        size_t char_count = strlen(MATRIX_CHARS);
        if (char_count == 0) char_count = 1;
        for (int i = 0; i < length; i++) {
            chars[i] = MATRIX_CHARS[rand() % char_count];
        }
    }

    void update(float dt) {
        y += speed * dt * 60.0f;
        if (y - length * FONT_SIZE > HEIGHT) {
            reset();
        }
        if (rand() % 3 == 0) {
            size_t char_count = strlen(MATRIX_CHARS);
            if (char_count == 0) char_count = 1;

            int idx = rand() % length;
            chars[idx] = MATRIX_CHARS[rand() % char_count];
        }
    }
};

SDL_Window* window = nullptr;
SDL_Renderer* ren = nullptr;
SDL_Texture* tex = nullptr;
glContext the_Context = {};
pix_t* bbufpix = nullptr;

int width = 0, height = 0;
int buf_width = 0, buf_height = 0;

std::vector<Column> columns;

void setup_context() {
    if (SDL_Init(SDL_INIT_VIDEO)) {
        klog("SDL_Init error: %s\n", SDL_GetError());
        exit(0);
    }

    window = SDL_CreateWindow("Matrix Rain", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN);
    if (!window) exit(0);

    SDL_GetWindowSize(window, &width, &height);
    if (width <= 0 || height <= 0) {
        width = WIDTH;
        height = HEIGHT;
    }

    ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) exit(0);

    tex = SDL_CreateTexture(ren, PIX_FORMAT, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!tex) exit(0);

    pgl_set_max_vertices(PGL_SMALL_MAX_VERTICES);
    if (!init_glContext(&the_Context, &bbufpix, width, height)) {
        puts("Failed to initialize glContext");
        exit(0);
    }

    buf_width = width;
    buf_height = height;
}

void cleanup() {
    if (font_buffer) free(font_buffer);
    free_glContext(&the_Context);
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
}

int handle_events();

static int load_font_from_path(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 10 * 1024 * 1024) {
        fclose(f);
        return 0;
    }

    font_buffer = (unsigned char*)malloc(fsize);
    if (!font_buffer) {
        fclose(f);
        return 0;
    }

    size_t total_read = 0;
    while (total_read < (size_t)fsize) {
        size_t nread = fread(font_buffer + total_read, 1, (size_t)fsize - total_read, f);
        if (nread == 0) {
            break;
        }
        total_read += nread;
    }
    fclose(f);

    if (total_read != (size_t)fsize) {
        free(font_buffer);
        font_buffer = NULL;
        return 0;
    }

    int font_offset = stbtt_GetFontOffsetForIndex(font_buffer, 0);
    if (font_offset < 0 || !stbtt_InitFont(&font, font_buffer, font_offset)) {
        free(font_buffer);
        font_buffer = NULL;
        return 0;
    }

    stbtt_GetFontVMetrics(&font, &font_ascent, &font_descent, &font_linegap);
    font_scale = stbtt_ScaleForPixelHeight(&font, FONT_SIZE);
    if (font_ascent <= font_descent || font_scale <= 0.0f || font_scale > 1000.0f) {
        free(font_buffer);
        font_buffer = NULL;
        return 0;
    }
    is_font_loaded = 1;
    return 1;
}

int load_font() {
    static const char* font_paths[] = {
        FONT_PATH,
        "/usr/system/fonts/system.ttf",
        "/usr/system/fonts/DejaVuSansMono.ttf",
        "/usr/system/fonts/UbuntuMono-Regular.ttf",
    };

    for (size_t i = 0; i < sizeof(font_paths) / sizeof(font_paths[0]); ++i) {
        if (load_font_from_path(font_paths[i])) {
            return 1;
        }
    }
    klog("matrix: failed to load any font, rendering will stay blank\n");
    return 0;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    srand((unsigned int)time(NULL));

    setup_context();

    load_font();
    init_glyph_cache();

    int num_columns = width / FONT_SIZE;
    if (num_columns <= 0) num_columns = 1;
    columns.resize(num_columns);

    for (size_t i = 0; i < columns.size(); i++) {
        columns[i].y = (float)(rand() % (HEIGHT + HEIGHT)) - HEIGHT;
    }

    unsigned int last_time = SDL_GetTicks();
    int frame_count = 0;
    unsigned int fps_time = 0;
    float accumulator = 0.0f;
    const float FIXED_DT = 1.0f / 60.0f;  // Fixed 60 FPS timestep

    while (1) {
        unsigned int current_time = SDL_GetTicks();
        float dt = (current_time - last_time) / 1000.0f;
        if (dt > 0.1f) dt = 0.1f;  // Clamp large frame gaps to avoid visible jumps after stalls
        last_time = current_time;
        accumulator += dt;

        if (handle_events()) {
            break;
        }
        frame_count++;
        if (current_time - fps_time > 3000) {
            klog("%d FPS (%dx%d)\n", frame_count * 1000 / (current_time - fps_time), width, height);
            frame_count = 0;
            fps_time = current_time;
        }

        long total_pixels = (long)width * height;
        for (long i = 0; i < total_pixels; ++i) {
            bbufpix[i] = 0xFF000000;
        }

        // Update with a fixed timestep to keep the animation speed consistent.
        while (accumulator >= FIXED_DT) {
            for (size_t ci = 0; ci < columns.size(); ci++) {
                columns[ci].update(FIXED_DT);
            }
            accumulator -= FIXED_DT;
        }

        if (is_font_loaded) {
            for (size_t ci = 0; ci < columns.size(); ci++) {

                Column& col = columns[ci];
                int col_x = (int)(ci * FONT_SIZE);
                if (col_x >= width) continue;

                for (int j = 0; j < col.length; j++) {
                    int char_y = (int)(col.y - j * FONT_SIZE);
                    if (char_y < -FONT_SIZE || char_y >= height + FONT_SIZE) continue;

                    char c = col.chars[j];

                    float brightness = 1.0f - (float)j / col.length;
                    uint32_t color;
                    if (j == 0) {
                        color = 0xFFFFFFFF;
                    } else {
                        unsigned char g_val = (unsigned char)(brightness * 255);
                        unsigned char r_val = (unsigned char)(brightness * 50);
                        color = 0xFF000000 | ((uint32_t)r_val << 16) | ((uint32_t)g_val << 8);
                    }

                    GlyphCacheEntry* glyph = get_glyph_cache(c);
                    if (glyph->w <= 0 || glyph->h <= 0) {
                        draw_fallback_block(col_x, char_y, FONT_SIZE, FONT_SIZE, color);
                        continue;
                    }

                    int baseline_y = char_y + (int)(font_ascent * font_scale) + glyph->y0;

                    for (int row = 0; row < glyph->h; row++) {
                        int py = baseline_y + row;
                        if (py < 0 || py >= height) continue;

                        for (int col_idx = 0; col_idx < glyph->w; col_idx++) {
                            int px = col_x + glyph->x0 + col_idx;
                            if (px < 0 || px >= width) continue;

                            unsigned char alpha = glyph->bitmap[row * glyph->w + col_idx];
                            if (alpha > 0) {
                                long idx = ((long)py * width) + px;
                                bbufpix[idx] = color;
                            }
                        }
                    }
                }
            }
        }

        render_time_overlay();

        SDL_UpdateTexture(tex, NULL, bbufpix, width * sizeof(pix_t));
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderFillRect(ren, NULL);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }

    cleanup();
    return 0;
}

int handle_events() {
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                return 1;
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
            return 1;

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_CLOSE) {
                return 1;
            }
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                width = event.window.data1;
                height = event.window.data2;

                ResizeFramebuffer(width, height);
                bbufpix = (pix_t*)GetBackBuffer();
                glViewport(0, 0, width, height);
                SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, PIX_FORMAT, SDL_TEXTUREACCESS_STREAMING, width, height);
            
                int num_cols = width / FONT_SIZE;
                if (num_cols <= 0) num_cols = 1;
                if (num_cols > 10000) {
                    break;
                }
                columns.resize(num_cols);
                for (size_t k = 0; k < columns.size(); k++) {
                    columns[k].reset();
                }
            }
            break;

        case SDL_QUIT:
            return 1;
        }
    }

    return 0;
}
