#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include <ewoksys/kernel_tic.h>
#include <ewoksys/keydef.h>
#include <ewoksys/proc.h>
#include <graph/graph.h>
#include <mouse/mouse.h>
#include <x/x.h>
#include <x/xwin.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Image {
	int w;
	int h;
	std::vector<uint32_t> pixels;

	Image() : w(0), h(0) {
	}

	Image(int width, int height, uint32_t fill = 0) {
		reset(width, height, fill);
	}

	void reset(int width, int height, uint32_t fill = 0) {
		w = width;
		h = height;
		pixels.assign((size_t)w * (size_t)h, fill);
	}

	inline uint32_t& at(int x, int y) {
		return pixels[(size_t)y * (size_t)w + (size_t)x];
	}

	inline const uint32_t& at(int x, int y) const {
		return pixels[(size_t)y * (size_t)w + (size_t)x];
	}
};

struct Enemy {
	float x;
	float y;
	int archetype;
	float hp;
	float hurt_timer;
	float attack_timer;
	float move_phase;
	bool dead;
};

struct DrawEnemy {
	int index;
	float distance_sq;
};

static int win_width = 640;
static int win_height = 480;
static xwin_t* g_xwin = NULL;

static bool g_keys[512];
static bool g_mouse_fire = false;
static bool g_restart_pressed = false;
static float g_mouse_turn = 0.0f;
static bool g_mouse_seen = false;
static int g_mouse_x = 0;

static uint64_t g_last_frame_ms = 0;
static float g_frame_dt = 0.016f;
static float g_game_time = 0.0f;

static float g_player_x = 2.5f;
static float g_player_y = 2.5f;
static float g_player_angle = 0.0f;
static float g_player_hp = 100.0f;
static int g_player_ammo = 36;
static float g_shoot_cooldown = 0.0f;
static float g_muzzle_timer = 0.0f;
static float g_damage_flash = 0.0f;
static float g_walk_phase = 0.0f;
static bool g_game_over = false;
static bool g_victory = false;

static std::vector<float> g_depth_buffer;
static std::vector<Enemy> g_enemies;

static Image g_wall_textures[5];
static Image g_floor_texture;
static Image g_ceiling_texture;
static Image g_weapon_sprite;
static Image g_muzzle_sprite;
static Image g_enemy_sprites[2][4];

static const int MAP_W = 16;
static const int MAP_H = 16;
static char g_map[MAP_H][MAP_W + 1];

static const char* k_level_template[MAP_H] = {
	"1111111111111111",
	"1S...2......E..1",
	"1.11.2.3333.11.1",
	"1....2....3....1",
	"1.22.222..3.2..1",
	"1....4....3..F.1",
	"1.11.4.1111111.1",
	"1....4........11",
	"1.333333.2222..1",
	"1..E....2....F.1",
	"1.1111..2.1111.1",
	"1...F...2......1",
	"1.2222.22.3333.1",
	"1.E....4....E..1",
	"1......4.......1",
	"1111111111111111",
};

static inline float clampf(float v, float lo, float hi) {
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static inline int maxi(int a, int b) {
	return a > b ? a : b;
}

static inline int mini(int a, int b) {
	return a < b ? a : b;
}

static inline float maxf(float a, float b) {
	return a > b ? a : b;
}

static inline float lerpf(float a, float b, float t) {
	return a + (b - a) * t;
}

static inline float wrap_angle(float a) {
	while (a < -((float)M_PI)) a += (float)(M_PI * 2.0);
	while (a > (float)M_PI) a -= (float)(M_PI * 2.0);
	return a;
}

static inline uint8_t u8_clamp(int v) {
	if (v < 0) return 0;
	if (v > 255) return 255;
	return (uint8_t)v;
}

static inline uint32_t make_color(int a, int r, int g, int b) {
	return ((uint32_t)u8_clamp(a) << 24) | ((uint32_t)u8_clamp(r) << 16) |
		((uint32_t)u8_clamp(g) << 8) | (uint32_t)u8_clamp(b);
}

static inline uint32_t rgb(int r, int g, int b) {
	return make_color(255, r, g, b);
}

static inline uint32_t scale_rgb(uint32_t color, float s) {
	int r = (int)(((color >> 16) & 0xff) * s);
	int g = (int)(((color >> 8) & 0xff) * s);
	int b = (int)((color & 0xff) * s);
	return make_color((color >> 24) & 0xff, r, g, b);
}

static inline uint32_t mix_rgb(uint32_t a, uint32_t b, float t) {
	t = clampf(t, 0.0f, 1.0f);
	int ar = (a >> 16) & 0xff;
	int ag = (a >> 8) & 0xff;
	int ab = a & 0xff;
	int br = (b >> 16) & 0xff;
	int bg = (b >> 8) & 0xff;
	int bb = b & 0xff;
	return rgb((int)lerpf((float)ar, (float)br, t),
		(int)lerpf((float)ag, (float)bg, t),
		(int)lerpf((float)ab, (float)bb, t));
}

static inline uint32_t alpha_color(uint32_t color, int alpha) {
	return (color & 0x00ffffffu) | ((uint32_t)u8_clamp(alpha) << 24);
}

static inline uint32_t hash_noise(int x, int y, int seed) {
	uint32_t v = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663) ^ (uint32_t)(seed * 83492791);
	v ^= v >> 13;
	v *= 1274126177u;
	v ^= v >> 16;
	return v;
}

static void fill_rect(Image& img, int x, int y, int w, int h, uint32_t color) {
	if (w <= 0 || h <= 0) return;
	int x0 = maxi(0, x);
	int y0 = maxi(0, y);
	int x1 = mini(img.w, x + w);
	int y1 = mini(img.h, y + h);
	for (int py = y0; py < y1; ++py) {
		for (int px = x0; px < x1; ++px) {
			img.at(px, py) = color;
		}
	}
}

static void draw_rect_outline(Image& img, int x, int y, int w, int h, uint32_t color) {
	fill_rect(img, x, y, w, 1, color);
	fill_rect(img, x, y + h - 1, w, 1, color);
	fill_rect(img, x, y, 1, h, color);
	fill_rect(img, x + w - 1, y, 1, h, color);
}

static void fill_circle(Image& img, int cx, int cy, int radius, uint32_t color) {
	int rr = radius * radius;
	for (int y = cy - radius; y <= cy + radius; ++y) {
		if (y < 0 || y >= img.h) continue;
		for (int x = cx - radius; x <= cx + radius; ++x) {
			if (x < 0 || x >= img.w) continue;
			int dx = x - cx;
			int dy = y - cy;
			if (dx * dx + dy * dy <= rr) {
				img.at(x, y) = color;
			}
		}
	}
}

static void draw_line(Image& img, int x0, int y0, int x1, int y1, uint32_t color, int thickness) {
	int dx = abs(x1 - x0);
	int sx = x0 < x1 ? 1 : -1;
	int dy = -abs(y1 - y0);
	int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy;
	for (;;) {
		fill_rect(img, x0 - thickness / 2, y0 - thickness / 2, thickness, thickness, color);
		if (x0 == x1 && y0 == y1) break;
		int e2 = err * 2;
		if (e2 >= dy) {
			err += dy;
			x0 += sx;
		}
		if (e2 <= dx) {
			err += dx;
			y0 += sy;
		}
	}
}

static void tint_image(Image& img, uint32_t color, float amount) {
	for (size_t i = 0; i < img.pixels.size(); ++i) {
		uint32_t c = img.pixels[i];
		if (((c >> 24) & 0xff) == 0) continue;
		img.pixels[i] = mix_rgb(c, color, amount);
	}
}

static uint32_t sample_image(const Image& img, float u, float v) {
	if (img.w == 0 || img.h == 0) return 0;
	int x = (int)(u * img.w);
	int y = (int)(v * img.h);
	if (x < 0) x = 0;
	if (x >= img.w) x = img.w - 1;
	if (y < 0) y = 0;
	if (y >= img.h) y = img.h - 1;
	return img.at(x, y);
}

static void generate_wall_brick(Image& img, uint32_t body, uint32_t mortar, uint32_t accent) {
	img.reset(64, 64, body);
	for (int y = 0; y < img.h; ++y) {
		int mortar_line = (y % 16) == 0 || (y % 16) == 15;
		int row_offset = ((y / 16) & 1) ? 8 : 0;
		for (int x = 0; x < img.w; ++x) {
			uint32_t n = hash_noise(x, y, 11) & 31u;
			uint32_t c = scale_rgb(body, 0.88f + (float)n / 128.0f);
			if (((x + row_offset) % 16) == 0 || ((x + row_offset) % 16) == 15 || mortar_line) {
				c = mortar;
			} else if (((hash_noise(x, y, 19) >> 5) & 7u) == 0u) {
				c = mix_rgb(c, accent, 0.35f);
			}
			img.at(x, y) = c;
		}
	}
}

static void generate_wall_panel(Image& img) {
	img.reset(64, 64, rgb(55, 76, 90));
	for (int y = 0; y < img.h; ++y) {
		for (int x = 0; x < img.w; ++x) {
			float band = 0.85f + (float)((x + y) & 7) / 48.0f;
			uint32_t base = scale_rgb(rgb(55, 76, 90), band);
			if (x % 16 == 0 || y % 16 == 0) base = rgb(22, 30, 36);
			if ((x % 16 == 2 || x % 16 == 13) && (y % 16 == 2 || y % 16 == 13)) base = rgb(200, 210, 220);
			img.at(x, y) = base;
		}
	}
	fill_rect(img, 23, 0, 18, 64, alpha_color(rgb(20, 110, 140), 255));
	for (int y = 4; y < 60; y += 8) {
		fill_rect(img, 25, y, 14, 2, rgb(90, 220, 255));
	}
}

static void generate_wall_toxic(Image& img) {
	img.reset(64, 64, rgb(45, 56, 32));
	for (int y = 0; y < img.h; ++y) {
		for (int x = 0; x < img.w; ++x) {
			float shade = 0.75f + (float)(hash_noise(x, y, 7) & 63u) / 180.0f;
			img.at(x, y) = scale_rgb(rgb(45, 56, 32), shade);
		}
	}
	for (int y = 2; y < img.h; y += 14) {
		draw_line(img, 0, y, 63, y + 6, rgb(150, 255, 90), 2);
	}
	for (int x = 8; x < img.w; x += 16) {
		draw_line(img, x, 0, x + 6, 63, rgb(90, 180, 60), 2);
	}
}

static void generate_wall_door(Image& img) {
	img.reset(64, 64, rgb(84, 56, 98));
	fill_rect(img, 0, 0, 64, 64, rgb(84, 56, 98));
	draw_rect_outline(img, 2, 2, 60, 60, rgb(200, 150, 230));
	fill_rect(img, 28, 0, 8, 64, rgb(32, 18, 40));
	fill_circle(img, 32, 16, 8, rgb(255, 180, 80));
	fill_circle(img, 32, 16, 4, rgb(255, 235, 180));
	for (int y = 28; y < 60; y += 6) {
		fill_rect(img, 12, y, 14, 2, rgb(255, 120, 70));
		fill_rect(img, 38, y, 14, 2, rgb(110, 180, 255));
	}
}

static void generate_floor(Image& img) {
	img.reset(64, 64, rgb(52, 44, 40));
	for (int y = 0; y < img.h; ++y) {
		for (int x = 0; x < img.w; ++x) {
			uint32_t c = (((x / 8) + (y / 8)) & 1) ? rgb(62, 54, 48) : rgb(46, 38, 35);
			if ((hash_noise(x, y, 23) & 31u) == 0u) c = rgb(92, 26, 18);
			if (x % 8 == 0 || y % 8 == 0) c = rgb(24, 20, 18);
			img.at(x, y) = c;
		}
	}
}

static void generate_ceiling(Image& img) {
	img.reset(64, 64, rgb(18, 20, 30));
	for (int y = 0; y < img.h; ++y) {
		for (int x = 0; x < img.w; ++x) {
			uint32_t c = (((x / 16) + (y / 16)) & 1) ? rgb(26, 30, 44) : rgb(18, 20, 30);
			if (x % 16 == 0 || y % 16 == 0) c = rgb(9, 10, 16);
			if ((x % 16 == 8) && (y % 16 == 8)) c = rgb(255, 180, 96);
			img.at(x, y) = c;
		}
	}
}

static void generate_weapon(Image& img) {
	img.reset(128, 88, 0);
	fill_rect(img, 40, 34, 48, 36, rgb(50, 54, 62));
	fill_rect(img, 46, 18, 12, 28, rgb(90, 98, 110));
	fill_rect(img, 70, 18, 12, 28, rgb(90, 98, 110));
	fill_rect(img, 48, 14, 8, 6, rgb(140, 150, 168));
	fill_rect(img, 72, 14, 8, 6, rgb(140, 150, 168));
	fill_rect(img, 54, 30, 20, 14, rgb(110, 26, 20));
	fill_rect(img, 52, 44, 24, 12, rgb(120, 70, 30));
	fill_rect(img, 56, 56, 16, 24, rgb(80, 46, 18));
	draw_rect_outline(img, 40, 34, 48, 36, rgb(200, 210, 225));
	fill_rect(img, 58, 40, 12, 4, rgb(255, 190, 96));
	fill_rect(img, 44, 70, 40, 8, rgb(36, 36, 40));
}

static void generate_muzzle(Image& img) {
	img.reset(64, 64, 0);
	fill_circle(img, 32, 32, 18, alpha_color(rgb(255, 210, 96), 220));
	fill_circle(img, 32, 32, 10, alpha_color(rgb(255, 250, 210), 240));
	draw_line(img, 32, 4, 32, 60, alpha_color(rgb(255, 200, 80), 180), 3);
	draw_line(img, 4, 32, 60, 32, alpha_color(rgb(255, 200, 80), 180), 3);
	draw_line(img, 10, 10, 54, 54, alpha_color(rgb(255, 160, 60), 160), 2);
	draw_line(img, 54, 10, 10, 54, alpha_color(rgb(255, 160, 60), 160), 2);
}

static void generate_enemy(Image& img, int archetype, int frame) {
	uint32_t skin = archetype == 0 ? rgb(176, 86, 70) : rgb(68, 154, 140);
	uint32_t armor = archetype == 0 ? rgb(82, 28, 26) : rgb(20, 64, 86);
	uint32_t glow = archetype == 0 ? rgb(255, 208, 72) : rgb(120, 255, 240);
	uint32_t dark = archetype == 0 ? rgb(42, 16, 16) : rgb(10, 24, 34);
	img.reset(64, 96, 0);

	if (frame == 3) {
		fill_rect(img, 10, 58, 44, 18, armor);
		fill_rect(img, 6, 64, 54, 16, skin);
		fill_circle(img, 16, 56, 10, skin);
		fill_circle(img, 48, 56, 9, skin);
		fill_rect(img, 20, 48, 22, 10, dark);
		tint_image(img, rgb(28, 28, 28), 0.45f);
		return;
	}

	fill_circle(img, 32, 18, 14, skin);
	fill_rect(img, 20, 30, 24, 34, armor);
	fill_rect(img, 16, 34, 8, 22, skin);
	fill_rect(img, 40, 34, 8, 22, skin);
	fill_rect(img, 22, 64, 8, 22, dark);
	fill_rect(img, 34, 64, 8, 22, dark);
	fill_rect(img, 22, 78, 8, 10, rgb(28, 28, 28));
	fill_rect(img, 34, 78, 8, 10, rgb(28, 28, 28));
	fill_rect(img, 23, 40, 18, 10, mix_rgb(armor, glow, 0.35f));
	fill_rect(img, 27, 22, 4, 3, glow);
	fill_rect(img, 35, 22, 4, 3, glow);
	fill_circle(img, 18, 12, 5, dark);
	fill_circle(img, 46, 12, 5, dark);

	if (frame == 1) {
		draw_line(img, 18, 40, 6, 54, skin, 4);
		draw_line(img, 46, 40, 58, 54, skin, 4);
		fill_circle(img, 6, 54, 4, glow);
		fill_circle(img, 58, 54, 4, glow);
	} else {
		draw_line(img, 18, 42, 10, 56, skin, 4);
		draw_line(img, 46, 42, 54, 56, skin, 4);
	}

	if (frame == 2) {
		tint_image(img, rgb(255, 120, 120), 0.55f);
	}
}

static void generate_assets(void) {
	generate_wall_brick(g_wall_textures[1], rgb(130, 54, 48), rgb(56, 24, 22), rgb(230, 128, 72));
	generate_wall_panel(g_wall_textures[2]);
	generate_wall_toxic(g_wall_textures[3]);
	generate_wall_door(g_wall_textures[4]);
	generate_floor(g_floor_texture);
	generate_ceiling(g_ceiling_texture);
	generate_weapon(g_weapon_sprite);
	generate_muzzle(g_muzzle_sprite);
	for (int archetype = 0; archetype < 2; ++archetype) {
		for (int frame = 0; frame < 4; ++frame) {
			generate_enemy(g_enemy_sprites[archetype][frame], archetype, frame);
		}
	}
}

static inline bool key_down(int code) {
	if (code < 0 || code >= (int)(sizeof(g_keys) / sizeof(g_keys[0]))) return false;
	return g_keys[code];
}

static inline void set_key(int code, bool down) {
	if (code >= 0 && code < (int)(sizeof(g_keys) / sizeof(g_keys[0]))) {
		g_keys[code] = down;
	}
}

static bool is_wall_tile(int tx, int ty) {
	if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return true;
	char c = g_map[ty][tx];
	return c >= '1' && c <= '4';
}

static int wall_texture_index(int tx, int ty) {
	if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return 1;
	char c = g_map[ty][tx];
	if (c >= '1' && c <= '4') return c - '0';
	return 1;
}

static bool circle_hits_wall(float x, float y, float radius) {
	int min_x = (int)floorf(x - radius);
	int max_x = (int)floorf(x + radius);
	int min_y = (int)floorf(y - radius);
	int max_y = (int)floorf(y + radius);
	for (int ty = min_y; ty <= max_y; ++ty) {
		for (int tx = min_x; tx <= max_x; ++tx) {
			if (!is_wall_tile(tx, ty)) continue;
			float nearest_x = clampf(x, (float)tx, (float)tx + 1.0f);
			float nearest_y = clampf(y, (float)ty, (float)ty + 1.0f);
			float dx = x - nearest_x;
			float dy = y - nearest_y;
			if (dx * dx + dy * dy < radius * radius) return true;
		}
	}
	return false;
}

static void try_move_player(float nx, float ny) {
	const float radius = 0.22f;
	if (!circle_hits_wall(nx, g_player_y, radius)) g_player_x = nx;
	if (!circle_hits_wall(g_player_x, ny, radius)) g_player_y = ny;
}

static bool has_line_of_sight(float x0, float y0, float x1, float y1) {
	float dx = x1 - x0;
	float dy = y1 - y0;
	float dist = sqrtf(dx * dx + dy * dy);
	if (dist < 0.001f) return true;
	int steps = (int)(dist * 16.0f);
	if (steps < 1) steps = 1;
	for (int i = 1; i < steps; ++i) {
		float t = (float)i / (float)steps;
		float px = x0 + dx * t;
		float py = y0 + dy * t;
		if (is_wall_tile((int)floorf(px), (int)floorf(py))) return false;
	}
	return true;
}

static void reset_game(void) {
	memset(g_keys, 0, sizeof(g_keys));
	g_mouse_fire = false;
	g_restart_pressed = false;
	g_mouse_turn = 0.0f;
	g_mouse_seen = false;
	g_enemies.clear();

	for (int y = 0; y < MAP_H; ++y) {
		strcpy(g_map[y], k_level_template[y]);
		for (int x = 0; x < MAP_W; ++x) {
			char c = g_map[y][x];
			if (c == 'S') {
				g_player_x = (float)x + 0.5f;
				g_player_y = (float)y + 0.5f;
				g_player_angle = 0.0f;
				g_map[y][x] = '.';
			} else if (c == 'E' || c == 'F') {
				Enemy enemy;
				enemy.x = (float)x + 0.5f;
				enemy.y = (float)y + 0.5f;
				enemy.archetype = (c == 'F') ? 1 : 0;
				enemy.hp = enemy.archetype == 0 ? 55.0f : 75.0f;
				enemy.hurt_timer = 0.0f;
				enemy.attack_timer = 0.0f;
				enemy.move_phase = (float)((x * 17 + y * 31) & 7);
				enemy.dead = false;
				g_enemies.push_back(enemy);
				g_map[y][x] = '.';
			}
		}
	}

	g_player_hp = 100.0f;
	g_player_ammo = 36;
	g_shoot_cooldown = 0.0f;
	g_muzzle_timer = 0.0f;
	g_damage_flash = 0.0f;
	g_walk_phase = 0.0f;
	g_game_over = false;
	g_victory = false;
	g_last_frame_ms = kernel_tic_ms(0);
	g_frame_dt = 0.016f;
	g_game_time = 0.0f;
}

static void shoot_weapon(void) {
	if (g_shoot_cooldown > 0.0f || g_game_over || g_victory) return;
	if (g_player_ammo <= 0) return;

	g_player_ammo--;
	g_shoot_cooldown = 0.26f;
	g_muzzle_timer = 0.09f;

	int best_index = -1;
	float best_dist = 1e9f;
	for (size_t i = 0; i < g_enemies.size(); ++i) {
		Enemy& enemy = g_enemies[i];
		if (enemy.dead) continue;
		float dx = enemy.x - g_player_x;
		float dy = enemy.y - g_player_y;
		float dist = sqrtf(dx * dx + dy * dy);
		if (dist > 10.0f) continue;
		float target_angle = atan2f(dy, dx);
		float angle_diff = fabsf(wrap_angle(target_angle - g_player_angle));
		float aim_window = 0.08f + 0.22f / (dist + 0.2f);
		if (angle_diff > aim_window) continue;
		if (!has_line_of_sight(g_player_x, g_player_y, enemy.x, enemy.y)) continue;
		if (dist < best_dist) {
			best_dist = dist;
			best_index = (int)i;
		}
	}

	if (best_index >= 0) {
		Enemy& enemy = g_enemies[(size_t)best_index];
		enemy.hp -= enemy.archetype == 0 ? 30.0f : 24.0f;
		enemy.hurt_timer = 0.18f;
		if (enemy.hp <= 0.0f) {
			enemy.dead = true;
			enemy.attack_timer = 0.0f;
		}
	}
}

static void update_game(float dt) {
	g_frame_dt = dt;
	g_game_time += dt;
	g_damage_flash = maxf(0.0f, g_damage_flash - dt);
	g_shoot_cooldown = maxf(0.0f, g_shoot_cooldown - dt);
	g_muzzle_timer = maxf(0.0f, g_muzzle_timer - dt);

	if (g_restart_pressed) {
		reset_game();
		return;
	}

	if (g_game_over || g_victory) return;

	float move_speed = 2.55f;
	float turn_speed = 2.25f;
	float move_x = 0.0f;
	float move_y = 0.0f;

	float dir_x = cosf(g_player_angle);
	float dir_y = sinf(g_player_angle);
	float side_x = -dir_y;
	float side_y = dir_x;

	if (key_down('w') || key_down('W') || key_down(KEY_UP)) {
		move_x += dir_x;
		move_y += dir_y;
	}
	if (key_down('s') || key_down('S') || key_down(KEY_DOWN)) {
		move_x -= dir_x;
		move_y -= dir_y;
	}
	if (key_down('a') || key_down('A')) {
		move_x -= side_x;
		move_y -= side_y;
	}
	if (key_down('d') || key_down('D')) {
		move_x += side_x;
		move_y += side_y;
	}
	if (key_down(KEY_LEFT) || key_down('q') || key_down('Q')) {
		g_player_angle -= turn_speed * dt;
	}
	if (key_down(KEY_RIGHT) || key_down('e') || key_down('E')) {
		g_player_angle += turn_speed * dt;
	}

	g_player_angle += g_mouse_turn;
	g_mouse_turn = 0.0f;
	g_player_angle = wrap_angle(g_player_angle);

	float move_len = sqrtf(move_x * move_x + move_y * move_y);
	if (move_len > 0.001f) {
		move_x /= move_len;
		move_y /= move_len;
		try_move_player(g_player_x + move_x * move_speed * dt, g_player_y + move_y * move_speed * dt);
		g_walk_phase += dt * 10.0f;
	}

	bool fire = g_mouse_fire || key_down(KEY_SPACE) || key_down(KEY_ENTER);
	if (fire) shoot_weapon();

	bool all_dead = true;
	for (size_t i = 0; i < g_enemies.size(); ++i) {
		Enemy& enemy = g_enemies[i];
		enemy.hurt_timer = maxf(0.0f, enemy.hurt_timer - dt);
		enemy.attack_timer = maxf(0.0f, enemy.attack_timer - dt);

		if (enemy.dead) continue;
		all_dead = false;

		float dx = g_player_x - enemy.x;
		float dy = g_player_y - enemy.y;
		float dist = sqrtf(dx * dx + dy * dy);
		bool visible = has_line_of_sight(enemy.x, enemy.y, g_player_x, g_player_y);

		if (visible && dist > 1.55f) {
			float nx = dx / (dist + 0.0001f);
			float ny = dy / (dist + 0.0001f);
			float strafe = sinf(g_game_time * 1.2f + enemy.move_phase) * 0.35f;
			float move_step = (enemy.archetype == 0 ? 1.0f : 1.25f) * dt;
			float tx = enemy.x + (nx + ny * strafe) * move_step;
			float ty = enemy.y + (ny - nx * strafe) * move_step;
			if (!circle_hits_wall(tx, ty, 0.22f)) {
				enemy.x = tx;
				enemy.y = ty;
			}
		}

		if (visible && dist < 5.7f && enemy.attack_timer <= 0.0f) {
			enemy.attack_timer = enemy.archetype == 0 ? 1.15f : 0.92f;
			float damage = enemy.archetype == 0 ? 8.0f : 12.0f;
			if (dist < 2.1f) damage += 4.0f;
			g_player_hp -= damage;
			g_damage_flash = 0.25f;
			if (g_player_hp <= 0.0f) {
				g_player_hp = 0.0f;
				g_game_over = true;
			}
		}
	}

	if (all_dead) g_victory = true;
}

static inline void put_pixel(graph_t* g, int x, int y, uint32_t color) {
	if (x < 0 || y < 0 || x >= g->w || y >= g->h) return;
	g->buffer[(size_t)y * (size_t)g->w + (size_t)x] = color;
}

static inline void blend_pixel(graph_t* g, int x, int y, uint32_t src) {
	if (x < 0 || y < 0 || x >= g->w || y >= g->h) return;
	uint8_t a = (src >> 24) & 0xff;
	if (a == 0) return;
	uint32_t* dst = &g->buffer[(size_t)y * (size_t)g->w + (size_t)x];
	if (a == 255) {
		*dst = src;
		return;
	}
	uint32_t d = *dst;
	int sr = (src >> 16) & 0xff;
	int sg = (src >> 8) & 0xff;
	int sb = src & 0xff;
	int dr = (d >> 16) & 0xff;
	int dg = (d >> 8) & 0xff;
	int db = d & 0xff;
	int ia = 255 - a;
	*dst = make_color(255,
		(sr * a + dr * ia) / 255,
		(sg * a + dg * ia) / 255,
		(sb * a + db * ia) / 255);
}

static void draw_scaled_sprite(graph_t* g, const Image& img, int dx, int dy, int dw, int dh, float shade) {
	if (dw <= 0 || dh <= 0) return;
	for (int y = 0; y < dh; ++y) {
		float v = (float)y / (float)dh;
		int sy = (int)(v * img.h);
		if (sy < 0) sy = 0;
		if (sy >= img.h) sy = img.h - 1;
		int py = dy + y;
		if (py < 0 || py >= g->h) continue;
		for (int x = 0; x < dw; ++x) {
			float u = (float)x / (float)dw;
			int sx = (int)(u * img.w);
			if (sx < 0) sx = 0;
			if (sx >= img.w) sx = img.w - 1;
			uint32_t src = img.at(sx, sy);
			uint8_t a = (src >> 24) & 0xff;
			if (a == 0) continue;
			src = make_color(a,
				(int)(((src >> 16) & 0xff) * shade),
				(int)(((src >> 8) & 0xff) * shade),
				(int)((src & 0xff) * shade));
			blend_pixel(g, dx + x, py, src);
		}
	}
}

static void draw_vertical_wall_slice(graph_t* g, int x, int draw_start, int draw_end, const Image& tex, float tex_x, float dist, int side) {
	if (draw_start < 0) draw_start = 0;
	if (draw_end >= g->h) draw_end = g->h - 1;
	float shade = 1.0f / (1.0f + dist * 0.22f);
	if (side) shade *= 0.82f;
	for (int y = draw_start; y <= draw_end; ++y) {
		float t = (float)(y - draw_start) / (float)maxi(1, draw_end - draw_start + 1);
		uint32_t c = sample_image(tex, tex_x, t);
		put_pixel(g, x, y, scale_rgb(c, shade));
	}
}

static void render_floor_and_ceiling(graph_t* g) {
	float dir_x = cosf(g_player_angle);
	float dir_y = sinf(g_player_angle);
	float plane_scale = tanf(0.52f);
	float plane_x = -dir_y * plane_scale;
	float plane_y = dir_x * plane_scale;
	int half_h = g->h / 2;
	float pos_z = 0.5f * (float)g->h;

	for (int y = half_h + 1; y < g->h; ++y) {
		float p = (float)y - (float)half_h;
		if (p <= 0.0f) continue;
		float row_dist = pos_z / p;
		float ray0_x = dir_x - plane_x;
		float ray0_y = dir_y - plane_y;
		float ray1_x = dir_x + plane_x;
		float ray1_y = dir_y + plane_y;
		float step_x = row_dist * (ray1_x - ray0_x) / (float)g->w;
		float step_y = row_dist * (ray1_y - ray0_y) / (float)g->w;
		float floor_x = g_player_x + row_dist * ray0_x;
		float floor_y = g_player_y + row_dist * ray0_y;

		for (int x = 0; x < g->w; ++x) {
			int cell_x = (int)floorf(floor_x);
			int cell_y = (int)floorf(floor_y);
			float tx = floor_x - (float)cell_x;
			float ty = floor_y - (float)cell_y;

			uint32_t floor_c = sample_image(g_floor_texture, tx, ty);
			uint32_t ceil_c = sample_image(g_ceiling_texture, tx, ty);
			float floor_shade = 1.0f / (1.0f + row_dist * 0.12f);
			float ceil_shade = 0.82f / (1.0f + row_dist * 0.10f);
			put_pixel(g, x, y, scale_rgb(floor_c, floor_shade));
			put_pixel(g, x, g->h - y - 1, scale_rgb(ceil_c, ceil_shade));

			floor_x += step_x;
			floor_y += step_y;
		}
	}

	graph_fill_rect(g, 0, 0, g->w, 2, rgb(200, 90, 60));
}

static void render_walls(graph_t* g) {
	g_depth_buffer.assign((size_t)g->w, 1e9f);
	float dir_x = cosf(g_player_angle);
	float dir_y = sinf(g_player_angle);
	float plane_scale = tanf(0.52f);
	float plane_x = -dir_y * plane_scale;
	float plane_y = dir_x * plane_scale;

	for (int x = 0; x < g->w; ++x) {
		float camera_x = 2.0f * (float)x / (float)g->w - 1.0f;
		float ray_dir_x = dir_x + plane_x * camera_x;
		float ray_dir_y = dir_y + plane_y * camera_x;

		int map_x = (int)floorf(g_player_x);
		int map_y = (int)floorf(g_player_y);

		float delta_x = ray_dir_x == 0.0f ? 1e9f : fabsf(1.0f / ray_dir_x);
		float delta_y = ray_dir_y == 0.0f ? 1e9f : fabsf(1.0f / ray_dir_y);
		float side_x;
		float side_y;
		int step_x;
		int step_y;
		int side = 0;

		if (ray_dir_x < 0.0f) {
			step_x = -1;
			side_x = (g_player_x - (float)map_x) * delta_x;
		} else {
			step_x = 1;
			side_x = ((float)map_x + 1.0f - g_player_x) * delta_x;
		}
		if (ray_dir_y < 0.0f) {
			step_y = -1;
			side_y = (g_player_y - (float)map_y) * delta_y;
		} else {
			step_y = 1;
			side_y = ((float)map_y + 1.0f - g_player_y) * delta_y;
		}

		while (!is_wall_tile(map_x, map_y)) {
			if (side_x < side_y) {
				side_x += delta_x;
				map_x += step_x;
				side = 0;
			} else {
				side_y += delta_y;
				map_y += step_y;
				side = 1;
			}
		}

		float perp_dist;
		if (side == 0) {
			perp_dist = ((float)map_x - g_player_x + (1.0f - (float)step_x) * 0.5f) / ray_dir_x;
		} else {
			perp_dist = ((float)map_y - g_player_y + (1.0f - (float)step_y) * 0.5f) / ray_dir_y;
		}
		if (perp_dist < 0.001f) perp_dist = 0.001f;
		g_depth_buffer[(size_t)x] = perp_dist;

		int line_height = (int)((float)g->h / perp_dist);
		int draw_start = -line_height / 2 + g->h / 2;
		int draw_end = line_height / 2 + g->h / 2;

		float wall_x = side == 0 ? g_player_y + perp_dist * ray_dir_y : g_player_x + perp_dist * ray_dir_x;
		wall_x -= floorf(wall_x);
		float tex_x = wall_x;
		if ((side == 0 && ray_dir_x > 0.0f) || (side == 1 && ray_dir_y < 0.0f)) {
			tex_x = 1.0f - tex_x;
		}
		draw_vertical_wall_slice(g, x, draw_start, draw_end, g_wall_textures[wall_texture_index(map_x, map_y)], tex_x, perp_dist, side);
	}
}

static bool enemy_draw_order(const DrawEnemy& a, const DrawEnemy& b) {
	return a.distance_sq > b.distance_sq;
}

static void render_enemies(graph_t* g) {
	float dir_x = cosf(g_player_angle);
	float dir_y = sinf(g_player_angle);
	float plane_scale = tanf(0.52f);
	float plane_x = -dir_y * plane_scale;
	float plane_y = dir_x * plane_scale;
	float inv_det = 1.0f / (plane_x * dir_y - dir_x * plane_y);

	std::vector<DrawEnemy> order;
	order.reserve(g_enemies.size());
	for (size_t i = 0; i < g_enemies.size(); ++i) {
		float dx = g_enemies[i].x - g_player_x;
		float dy = g_enemies[i].y - g_player_y;
		DrawEnemy item;
		item.index = (int)i;
		item.distance_sq = dx * dx + dy * dy;
		order.push_back(item);
	}
	for (size_t i = 0; i < order.size(); ++i) {
		size_t best = i;
		for (size_t j = i + 1; j < order.size(); ++j) {
			if (enemy_draw_order(order[j], order[best])) best = j;
		}
		if (best != i) {
			DrawEnemy tmp = order[i];
			order[i] = order[best];
			order[best] = tmp;
		}
	}

	for (size_t i = 0; i < order.size(); ++i) {
		Enemy& enemy = g_enemies[(size_t)order[i].index];
		float sprite_x = enemy.x - g_player_x;
		float sprite_y = enemy.y - g_player_y;
		float transform_x = inv_det * (dir_y * sprite_x - dir_x * sprite_y);
		float transform_y = inv_det * (-plane_y * sprite_x + plane_x * sprite_y);
		if (transform_y <= 0.2f) continue;

		int frame = 0;
		if (enemy.dead) frame = 3;
		else if (enemy.hurt_timer > 0.0f) frame = 2;
		else if (enemy.attack_timer > 0.62f) frame = 1;
		const Image& sprite = g_enemy_sprites[enemy.archetype][frame];

		int sprite_screen_x = (int)((g->w / 2.0f) * (1.0f + transform_x / transform_y));
		float scale = enemy.dead ? 0.45f : 0.92f;
		int sprite_h = abs((int)((float)g->h / transform_y * scale));
		int sprite_w = abs((int)((float)g->h / transform_y * scale * ((float)sprite.w / (float)sprite.h)));
		if (sprite_h <= 2 || sprite_w <= 2) continue;

		int draw_start_y = enemy.dead ? g->h / 2 + sprite_h / 6 : g->h / 2 - sprite_h / 2;
		int draw_end_y = draw_start_y + sprite_h;
		int draw_start_x = sprite_screen_x - sprite_w / 2;
		int draw_end_x = sprite_screen_x + sprite_w / 2;
		float shade = 1.0f / (1.0f + transform_y * 0.15f);

		for (int stripe = draw_start_x; stripe < draw_end_x; ++stripe) {
			if (stripe < 0 || stripe >= g->w) continue;
			if (transform_y > g_depth_buffer[(size_t)stripe]) continue;
			float u = (float)(stripe - draw_start_x) / (float)maxi(1, draw_end_x - draw_start_x);
			int sx = (int)(u * sprite.w);
			if (sx < 0) sx = 0;
			if (sx >= sprite.w) sx = sprite.w - 1;

			for (int y = draw_start_y; y < draw_end_y; ++y) {
				if (y < 0 || y >= g->h) continue;
				float v = (float)(y - draw_start_y) / (float)maxi(1, draw_end_y - draw_start_y);
				int sy = (int)(v * sprite.h);
				if (sy < 0) sy = 0;
				if (sy >= sprite.h) sy = sprite.h - 1;
				uint32_t src = sprite.at(sx, sy);
				uint8_t a = (src >> 24) & 0xff;
				if (a == 0) continue;
				src = make_color(a,
					(int)(((src >> 16) & 0xff) * shade),
					(int)(((src >> 8) & 0xff) * shade),
					(int)((src & 0xff) * shade));
				blend_pixel(g, stripe, y, src);
			}
		}
	}
}

static void draw_crosshair(graph_t* g) {
	int cx = g->w / 2;
	int cy = g->h / 2;
	uint32_t c = rgb(255, 220, 160);
	for (int i = -8; i <= 8; ++i) {
		if (abs(i) > 2) {
			put_pixel(g, cx + i, cy, c);
			put_pixel(g, cx, cy + i, c);
		}
	}
}

static void draw_bar(graph_t* g, int x, int y, int w, int h, float ratio, uint32_t fill, uint32_t border) {
	if (w <= 0 || h <= 0) return;
	graph_fill_rect(g, x, y, w, h, rgb(16, 16, 18));
	graph_fill_rect(g, x + 2, y + 2, (int)((float)(w - 4) * clampf(ratio, 0.0f, 1.0f)), h - 4, fill);
	graph_rect(g, x, y, w, h, border);
}

static void draw_seven_segment_digit(graph_t* g, int x, int y, int scale, int digit, uint32_t color) {
	static const uint8_t segs[10] = {
		0x3f, 0x06, 0x5b, 0x4f, 0x66,
		0x6d, 0x7d, 0x07, 0x7f, 0x6f
	};
	if (digit < 0 || digit > 9) return;
	uint8_t mask = segs[digit];
	int w = 6 * scale;
	int h = 12 * scale;
	int t = maxi(1, scale);
	if (mask & 0x01) graph_fill_rect(g, x + t, y, w - 2 * t, t, color);
	if (mask & 0x02) graph_fill_rect(g, x + w - t, y + t, t, h / 2 - t, color);
	if (mask & 0x04) graph_fill_rect(g, x + w - t, y + h / 2, t, h / 2 - t, color);
	if (mask & 0x08) graph_fill_rect(g, x + t, y + h - t, w - 2 * t, t, color);
	if (mask & 0x10) graph_fill_rect(g, x, y + h / 2, t, h / 2 - t, color);
	if (mask & 0x20) graph_fill_rect(g, x, y + t, t, h / 2 - t, color);
	if (mask & 0x40) graph_fill_rect(g, x + t, y + h / 2 - t / 2, w - 2 * t, t, color);
}

static void draw_number(graph_t* g, int x, int y, int scale, int value, uint32_t color) {
	if (value < 0) value = 0;
	int digits[5];
	int count = 0;
	do {
		digits[count++] = value % 10;
		value /= 10;
	} while (value > 0 && count < 5);
	for (int i = count - 1; i >= 0; --i) {
		draw_seven_segment_digit(g, x, y, scale, digits[i], color);
		x += 8 * scale;
	}
}

static void render_minimap(graph_t* g) {
	const int size = 96;
	const int x0 = 10;
	const int y0 = 10;
	graph_fill_rect(g, x0, y0, size, size, make_color(180, 0, 0, 0));
	for (int y = 0; y < MAP_H; ++y) {
		for (int x = 0; x < MAP_W; ++x) {
			uint32_t c = rgb(32, 32, 36);
			if (g_map[y][x] >= '1' && g_map[y][x] <= '4') c = g_wall_textures[g_map[y][x] - '0'].at(8, 8);
			graph_fill_rect(g, x0 + x * (size / MAP_W), y0 + y * (size / MAP_H), size / MAP_W, size / MAP_H, c);
		}
	}
	for (size_t i = 0; i < g_enemies.size(); ++i) {
		const Enemy& e = g_enemies[i];
		int ex = x0 + (int)(e.x / (float)MAP_W * (float)size);
		int ey = y0 + (int)(e.y / (float)MAP_H * (float)size);
		uint32_t c = e.dead ? rgb(60, 60, 60) : (e.archetype == 0 ? rgb(255, 96, 64) : rgb(64, 240, 220));
		graph_fill_rect(g, ex - 1, ey - 1, 3, 3, c);
	}
	int px = x0 + (int)(g_player_x / (float)MAP_W * (float)size);
	int py = y0 + (int)(g_player_y / (float)MAP_H * (float)size);
	graph_fill_rect(g, px - 2, py - 2, 5, 5, rgb(255, 255, 255));
	graph_line(g, px, py, px + (int)(cosf(g_player_angle) * 10.0f), py + (int)(sinf(g_player_angle) * 10.0f), rgb(255, 255, 180));
	graph_rect(g, x0, y0, size, size, rgb(255, 170, 110));
}

static void render_weapon(graph_t* g) {
	float bob = sinf(g_walk_phase) * 5.0f;
	int weapon_w = g->w / 3;
	int weapon_h = weapon_w * g_weapon_sprite.h / g_weapon_sprite.w;
	int wx = g->w / 2 - weapon_w / 2;
	int wy = g->h - weapon_h + (int)bob + (g_shoot_cooldown > 0.18f ? 6 : 0);
	draw_scaled_sprite(g, g_weapon_sprite, wx, wy, weapon_w, weapon_h, 1.0f);
	if (g_muzzle_timer > 0.0f) {
		int mw = weapon_w / 2;
		int mh = mw * g_muzzle_sprite.h / g_muzzle_sprite.w;
		int mx = g->w / 2 - mw / 2;
		int my = wy - mh / 2 + 8;
		draw_scaled_sprite(g, g_muzzle_sprite, mx, my, mw, mh, 1.0f);
	}
}

static void render_overlays(graph_t* g) {
	draw_crosshair(g);
	draw_bar(g, 18, g->h - 32, 160, 16, g_player_hp / 100.0f, rgb(210, 54, 42), rgb(255, 180, 120));
	draw_bar(g, 18, g->h - 54, 160, 12, (float)g_player_ammo / 36.0f, rgb(80, 180, 255), rgb(180, 220, 255));
	draw_number(g, 186, g->h - 38, 2, (int)(g_player_hp + 0.5f), rgb(255, 210, 180));
	draw_number(g, 186, g->h - 58, 2, g_player_ammo, rgb(180, 230, 255));
	render_minimap(g);

	if (g_damage_flash > 0.0f) {
		uint8_t a = (uint8_t)(g_damage_flash * 110.0f);
		graph_fill_rect(g, 0, 0, g->w, g->h, make_color(a, 160, 20, 20));
	}

	if (g_game_over || g_victory) {
		uint32_t panel = g_game_over ? make_color(210, 60, 10, 10) : make_color(190, 18, 60, 22);
		uint32_t accent = g_game_over ? rgb(255, 160, 140) : rgb(180, 255, 180);
		graph_fill_rect(g, g->w / 2 - 150, g->h / 2 - 60, 300, 120, panel);
		graph_rect(g, g->w / 2 - 150, g->h / 2 - 60, 300, 120, accent);
		graph_fill_rect(g, g->w / 2 - 110, g->h / 2 - 12, 220, 24, accent);
		graph_fill_rect(g, g->w / 2 - 106, g->h / 2 - 8, 212, 16, panel);
		if (g_victory) {
			graph_fill_rect(g, g->w / 2 - 96, g->h / 2 - 40, 24, 48, accent);
			graph_fill_rect(g, g->w / 2 - 64, g->h / 2 - 40, 24, 48, accent);
			graph_fill_rect(g, g->w / 2 - 96, g->h / 2 + 16, 56, 12, accent);
			graph_fill_rect(g, g->w / 2 + 24, g->h / 2 - 40, 18, 68, accent);
			graph_fill_rect(g, g->w / 2 + 46, g->h / 2 - 40, 18, 68, accent);
			graph_fill_rect(g, g->w / 2 + 24, g->h / 2 - 8, 40, 12, accent);
		} else {
			graph_fill_rect(g, g->w / 2 - 98, g->h / 2 - 44, 24, 72, accent);
			graph_fill_rect(g, g->w / 2 - 66, g->h / 2 - 44, 24, 72, accent);
			graph_fill_rect(g, g->w / 2 - 98, g->h / 2 - 44, 56, 12, accent);
			graph_fill_rect(g, g->w / 2 - 98, g->h / 2 - 10, 56, 12, accent);
			graph_fill_rect(g, g->w / 2 + 20, g->h / 2 - 44, 24, 72, accent);
			graph_fill_rect(g, g->w / 2 + 52, g->h / 2 - 44, 24, 72, accent);
		}
	}
}

static void render_scene(graph_t* g) {
	graph_fill_rect(g, 0, 0, g->w, g->h, rgb(0, 0, 0));
	render_floor_and_ceiling(g);
	render_walls(g);
	render_enemies(g);
	render_weapon(g);
	render_overlays(g);
}

static void tick_game(void) {
	uint64_t now_ms = kernel_tic_ms(0);
	if (g_last_frame_ms == 0) g_last_frame_ms = now_ms;
	float dt = (float)(now_ms - g_last_frame_ms) / 1000.0f;
	g_last_frame_ms = now_ms;
	dt = clampf(dt, 0.001f, 0.033f);
	update_game(dt);
}

static void on_repaint(xwin_t* xwin, graph_t* g) {
	(void)xwin;
	tick_game();
	render_scene(g);
}

static void on_event(xwin_t* xwin, xevent_t* ev) {
	if (ev->type == XEVT_IM) {
		int key = ev->value.im.value;
		bool down = (ev->state == XIM_STATE_PRESS);
		set_key(key, down);
		if (key >= 'A' && key <= 'Z') set_key(key - 'A' + 'a', down);
		if (key >= 'a' && key <= 'z') set_key(key - 'a' + 'A', down);

		if (down && key == KEY_ESC) {
			xwin_close(xwin);
			return;
		}
		if (down && (key == 'r' || key == 'R')) {
			g_restart_pressed = true;
		}
	} else if (ev->type == XEVT_MOUSE) {
		gpos_t pos = xwin_get_inside_pos(xwin, ev->value.mouse.x, ev->value.mouse.y);
		if (g_mouse_seen) {
			int dx = pos.x - g_mouse_x;
			if (dx > -120 && dx < 120) g_mouse_turn += (float)dx * 0.004f;
		}
		g_mouse_x = pos.x;
		g_mouse_seen = true;
		if (g_game_over && ev->state == MOUSE_STATE_DOWN) {
			g_restart_pressed = true;
		}
		if (ev->value.mouse.button == MOUSE_BUTTON_LEFT) {
			g_mouse_fire = (ev->state == MOUSE_STATE_DOWN || ev->state == MOUSE_STATE_DRAG);
		}
	}
}

static void on_resize(xwin_t* xwin) {
	if (xwin == NULL || xwin->xinfo == NULL) return;
	win_width = xwin->xinfo->wsr.w;
	win_height = xwin->xinfo->wsr.h;
	if (win_width < 320) win_width = 320;
	if (win_height < 240) win_height = 240;
}

static void loop(void* p) {
	xwin_t* xwin = (xwin_t*)p;
	xwin_repaint(xwin);
	g_restart_pressed = false;
	proc_usleep(12000);
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;

	generate_assets();
	reset_game();

	x_t x;
	x_init(&x, NULL);
	x.on_loop = loop;

	xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "Doom Demo", XWIN_STYLE_NORMAL);
	if (!xwin) {
		printf("failed to open window\n");
		return 1;
	}

	g_xwin = xwin;
	xwin->on_repaint = on_repaint;
	xwin->on_event = on_event;
	xwin->on_resize = on_resize;

	xwin_set_visible(xwin, true);
	x_run(&x, xwin);
	xwin_destroy(xwin);
	return 0;
}
