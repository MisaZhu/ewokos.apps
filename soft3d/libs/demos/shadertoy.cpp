
#include "glcommon/gltools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/timer.h>
#include <x/x.h>
#include <x/xwin.h>
#include <graph/graph.h>

#define sinf pgl_fast_sin
#define cosf pgl_fast_cos
#define sqrtf pgl_fast_sqrt
#define tanf pgl_fast_tan
#define acosf pgl_fast_acos
#define asinf pgl_fast_asin
#define atanf pgl_fast_atan
#define atanf2 pgl_fast_atan2

// Window dimensions (can be resized)
static int win_width = 640;
static int win_height = 480;

static glContext the_Context;
static pix_t* backbuf = NULL;
static xwin_t* g_xwin = NULL;

float iGlobalTime;

// FPS counter
static int fps = 0;
static int frame_count = 0;
static uint32_t last_tic = 0;

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);

    if (last_tic == 0 || (low - last_tic) >= 3000000) {
        last_tic = low;
        fps = frame_count / 3;
        printf("FPS: %d\n", fps);
        frame_count = 0;
    }
    frame_count++;
}

typedef struct My_Uniforms
{
	float globaltime;
	GLuint tex0;
	GLuint tex1;
	GLuint tex2;
	GLuint tex6;
	GLuint tex9;
} My_Uniforms;

#define NUM_TEXTURES 5
GLuint textures[NUM_TEXTURES];

#define NUM_SHADERS 11
GLuint shaders[NUM_SHADERS];
int cur_shader = 0;
uint32_t start_time = 0;

My_Uniforms the_uniforms;

void graphing_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void graphing_lines_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void my_tunnel_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void square_tunnel_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void deform_tunnel_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void the_cave_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void tileable_water_caustic_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void flame_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void running_in_the_night_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void voronoise_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void iqs_eyeball(float* fs_input, Shader_Builtins* builtins, void* uniforms);

frag_func frag_funcs[NUM_SHADERS] =
{
	graphing_lines_fs,
	graphing_fs,
	my_tunnel_fs,
	running_in_the_night_fs,
	square_tunnel_fs,
	deform_tunnel_fs,
	tileable_water_caustic_fs,
	iqs_eyeball,
	voronoise_fs,
	the_cave_fs,
	flame_fs
};

// Helper functions for C-style vector operations
// Note: make_v2/make_v3/make_v4 are already defined in portablegl.h

inline vec2 v2_add(vec2 a, vec2 b) { return make_v2(a.x + b.x, a.y + b.y); }
inline vec2 v2_sub(vec2 a, vec2 b) { return make_v2(a.x - b.x, a.y - b.y); }
inline vec2 v2_mul(vec2 a, vec2 b) { return make_v2(a.x * b.x, a.y * b.y); }
inline vec2 v2_scale(vec2 a, float s) { return make_v2(a.x * s, a.y * s); }
inline float v2_dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
inline float v2_len(vec2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
inline vec2 v2_norm(vec2 a) { float l = v2_len(a); return make_v2(a.x / l, a.y / l); }

inline vec3 v3_add(vec3 a, vec3 b) { return make_v3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline vec3 v3_sub(vec3 a, vec3 b) { return make_v3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline vec3 v3_mul(vec3 a, vec3 b) { return make_v3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline vec3 v3_scale(vec3 a, float s) { return make_v3(a.x * s, a.y * s, a.z * s); }
inline float v3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float v3_len(vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }
inline vec3 v3_norm(vec3 a) { float l = v3_len(a); return make_v3(a.x / l, a.y / l, a.z / l); }
inline vec3 v3_cross(vec3 a, vec3 b) {
	return make_v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline vec4 v4_add(vec4 a, vec4 b) { return make_v4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
inline vec4 v4_sub(vec4 a, vec4 b) { return make_v4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w); }
inline vec4 v4_mul(vec4 a, vec4 b) { return make_v4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w); }
inline vec4 v4_scale(vec4 a, float s) { return make_v4(a.x * s, a.y * s, a.z * s, a.w * s); }
inline float v4_dot(vec4 a, vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

inline float clamp_f(float x, float minVal, float maxVal) {
	if (x < minVal) return minVal;
	if (x > maxVal) return maxVal;
	return x;
}

inline float smoothstep_f(float edge0, float edge1, float x) {
	float t = clamp_f((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

inline float mix_f(float x, float y, float a) {
	return x * (1.0f - a) + y * a;
}

inline vec3 v3_mix(vec3 x, vec3 y, float a) {
	return make_v3(mix_f(x.x, y.x, a), mix_f(x.y, y.y, a), mix_f(x.z, y.z, a));
}

inline vec4 v4_mix(vec4 x, vec4 y, float a) {
	return make_v4(mix_f(x.x, y.x, a), mix_f(x.y, y.y, a), mix_f(x.z, y.z, a), mix_f(x.w, y.w, a));
}

inline float fract_f(float x) { return x - floorf(x); }

inline vec2 v2_fract(vec2 v) { return make_v2(fract_f(v.x), fract_f(v.y)); }
inline vec3 v3_fract(vec3 v) { return make_v3(fract_f(v.x), fract_f(v.y), fract_f(v.z)); }

inline vec2 v2_floor(vec2 v) { return make_v2(floorf(v.x), floorf(v.y)); }
inline vec3 v3_floor(vec3 v) { return make_v3(floorf(v.x), floorf(v.y), floorf(v.z)); }

inline float v2_length(vec2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
inline float v3_length(vec3 a) { return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z); }

static void init_textures(void)
{
	glGenTextures(NUM_TEXTURES, textures);
	glBindTexture(GL_TEXTURE_2D, textures[0]);
	if (!load_texture2D("/data/media/textures/tex00.jpg", GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_FALSE, GL_FALSE, NULL, NULL)) {
		printf("failed to load texture tex00.jpg\n");
	}
	glBindTexture(GL_TEXTURE_2D, textures[1]);
	if (!load_texture2D("/data/media/textures/tex02.jpg", GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_FALSE, GL_FALSE, NULL, NULL)) {
		printf("failed to load texture tex02.jpg\n");
	}
	glBindTexture(GL_TEXTURE_2D, textures[2]);
	if (!load_texture2D("/data/media/textures/tex06.jpg", GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_FALSE, GL_FALSE, NULL, NULL)) {
		printf("failed to load texture tex06.jpg\n");
	}
	glBindTexture(GL_TEXTURE_2D, textures[3]);
	if (!load_texture2D("/data/media/textures/tex01.jpg", GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_FALSE, GL_FALSE, NULL, NULL)) {
		printf("failed to load texture tex01.jpg\n");
	}
	glBindTexture(GL_TEXTURE_2D, textures[4]);
	if (!load_texture2D("/data/media/textures/tex09.jpg", GL_NEAREST, GL_NEAREST, GL_REPEAT, GL_FALSE, GL_FALSE, NULL, NULL)) {
		printf("failed to load texture tex09.jpg\n");
	}

	the_uniforms.tex0 = textures[0];
	the_uniforms.tex2 = textures[1];
	the_uniforms.tex6 = textures[2];
	the_uniforms.tex1 = textures[3];
	the_uniforms.tex9 = textures[4];
}

static void init_shaders(void)
{
	for (int i=0; i<NUM_SHADERS; ++i) {
		shaders[i] = pglCreateFragProgram(frag_funcs[i], GL_FALSE);
		glUseProgram(shaders[i]);
		SetUniform(&the_uniforms);
	}
	glUseProgram(shaders[cur_shader]);
}

static void on_repaint(xwin_t* xwin, graph_t* g)
{
	(void)xwin;
	
	uint32_t new_time = kernel_tic_ms(0);
	
	iGlobalTime = (new_time - start_time) / 1000.0f;  // convert ms to seconds
	the_uniforms.globaltime = (new_time - start_time) / 1000.0f;  // convert ms to seconds

	// Set viewport to match window size
	glViewport(0, 0, win_width, win_height);
	
	// Clear the framebuffer
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// Draw the shader
	pglDrawFrame2(frag_funcs[cur_shader], &the_uniforms);

	// Clear destination graph with black before blitting
	graph_fill_rect(g, 0, 0, win_width, win_height, 0xFF000000);
	
	// Blit to screen
	graph_t bg;
	graph_init(&bg, backbuf, win_width, win_height);
	graph_blt(&bg, 0, 0, win_width, win_height, g, 0, 0, win_width, win_height);
	
	update_fps();
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
	(void)xwin;
	
	if (ev->type == XEVT_IM) {
		int key = ev->value.im.value;
		int32_t state = ev->state;
		
		if (state == XIM_STATE_PRESS) {
			switch (key) {
			case KEY_ESC: // ESC
				xwin_close(xwin);
				break;
			case KEY_LEFT: // Left arrow
				cur_shader = (cur_shader) ? cur_shader-1 : NUM_SHADERS-1;
				start_time = kernel_tic_ms(0);
				glUseProgram(shaders[cur_shader]);
				break;
			case KEY_RIGHT: // Right arrow
				cur_shader = (cur_shader + 1) % NUM_SHADERS;
				start_time = kernel_tic_ms(0);
				glUseProgram(shaders[cur_shader]);
				break;
			}
		}
	}
}

static void on_resize(xwin_t* xwin)
{
	if(xwin == NULL || xwin->xinfo == NULL)
		return;
	
	int width = xwin->xinfo->wsr.w;
	int height = xwin->xinfo->wsr.h;
	
	if (width <= 0 || height <= 0)
		return;
	
	win_width = width;
	win_height = height;
	
	// Resize PortableGL framebuffer
	ResizeFramebuffer(win_width, win_height);
	backbuf = (pix_t*)GetBackBuffer();
	
	// Update viewport
	glViewport(0, 0, win_width, win_height);
	
	// Clear the framebuffer after resize
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

static void loop(void* p)
{
	xwin_t* xwin = (xwin_t*)p;
	xwin_repaint(xwin);
	proc_usleep(1000);
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	
	x_t x;
	x_init(&x, NULL);
	
	x.on_loop = loop;
	xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "Shadertoy", XWIN_STYLE_NORMAL);
	if (!xwin) {
		printf("Failed to open window\n");
		return 1;
	}
	xwin_set_alpha(xwin, false);
	
	g_xwin = xwin;
	xwin->on_repaint = on_repaint;
	xwin->on_event = on_event;
	xwin->on_resize = on_resize;
	
	// Initialize PortableGL context
	if (!init_glContext(&the_Context, (u32**)&backbuf, win_width, win_height)) {
		printf("Failed to initialize glContext\n");
		return 1;
	}
	
	// Set viewport
	glViewport(0, 0, win_width, win_height);
	
	// Create and bind VAO
	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	
	// Create screen quad
	float points[] = {
		-1.0f,  1.0f, 0.0f,
		-1.0f, -1.0f, 0.0f,
		 1.0f,  1.0f, 0.0f,
		 1.0f, -1.0f, 0.0f
	};
	
	GLuint screen_quad;
	glGenBuffers(1, &screen_quad);
	glBindBuffer(GL_ARRAY_BUFFER, screen_quad);
	glBufferData(GL_ARRAY_BUFFER, sizeof(points), points, GL_STATIC_DRAW);
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
	
	// Initialize textures and shaders
	init_textures();
	init_shaders();
	
	// Record start time
	start_time = kernel_tic_ms(0);
	
	// Make window visible
	xwin_set_visible(xwin, true);
	
	x_run(&x, xwin);
	
	// Cleanup
	xwin_destroy(xwin);
	free_glContext(&the_Context);
	
	return 0;
}

void graphing_lines_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
	vec2 frag = *(vec2*)(&builtins->gl_FragCoord);
	frag.x /= win_width;
	frag.y /= win_height;
	frag = v2_scale(frag, 2.0f);
	frag = v2_sub(frag, make_v2(1.0f, 1.0f));

	float fragx2 = frag.x * frag.x;
	float x2 = frag.y - fragx2;
	float x3 = frag.y - fragx2 * frag.x + 0.25f * frag.x;
	float x4 = frag.y - fragx2 * fragx2;
	float incr = 2.0f / win_height;

	if (x2 >= -incr && x2 <= incr) {
		*(vec4*)&builtins->gl_FragColor = make_v4(1, 0, 0, 1);
	} else if (x3 >= -incr && x3 <= incr) {
		*(vec4*)&builtins->gl_FragColor = make_v4(0, 1, 0, 1);
	} else if (x4 >= -incr && x4 <= incr) {
		*(vec4*)&builtins->gl_FragColor = make_v4(0, 0, 1, 1);
	} else {
		*(vec4*)&builtins->gl_FragColor = make_v4(0, 0, 0, 1);
	}
}

void graphing_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
	vec2 frag = *(vec2*)(&builtins->gl_FragCoord);
	frag.x /= win_width;
	frag.y /= win_height;
	frag = v2_scale(frag, 2.0f);
	frag = v2_sub(frag, make_v2(1.0f, 1.0f));

	float fragx2 = frag.x * frag.x;
	float x2 = frag.y - fragx2;

	if (x2 > 0)
		*(vec4*)&builtins->gl_FragColor = make_v4(x2, 0, 0, 1);
	else
		*(vec4*)&builtins->gl_FragColor = make_v4(0, 0, 0, 1);
}

void my_tunnel_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
	float globaltime = ((My_Uniforms*)uniforms)->globaltime;

	vec2 frag = *(vec2*)(&builtins->gl_FragCoord);
	frag.x /= win_width;
	frag.y /= win_height;
	frag = v2_scale(frag, 2.0f);
	frag = v2_sub(frag, make_v2(1.0f, 1.0f));

	float r = powf(powf(frag.x, 16.0f) + powf(frag.y, 16.0f), 1.0f/16.0f);
	float wave = (0.5f * sinf(10.0f * globaltime * (1.0f - r)) + 0.5f);

	*(vec4*)&builtins->gl_FragColor = make_v4(r * wave, 0, r * (1.0f - wave), 1);
}

void square_tunnel_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
	float globaltime = ((My_Uniforms*)uniforms)->globaltime;
	GLuint channel0 = ((My_Uniforms*)uniforms)->tex0;

	vec2 resolution = make_v2(win_width, win_height);
	vec2 frag = *(vec2*)(&builtins->gl_FragCoord);
	
	vec2 p = v2_sub(v2_scale(frag, 2.0f), resolution);
	p = v2_scale(p, 1.0f / resolution.y);

	float a = atan2f(p.y, p.x);
	float r = powf(powf(p.x * p.x, 16.0f) + powf(p.y * p.y, 16.0f), 1.0f/32.0f);

	vec2 uv = make_v2(0.5f/r + 0.5f * globaltime, a / 3.1416f);

	vec4 tmp = texture2D(channel0, uv.x, uv.y);
	*(vec4*)&builtins->gl_FragColor = make_v4(tmp.x * r, tmp.y * r, tmp.z * r, 1.0f);
}

void deform_tunnel_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
	float globaltime = ((My_Uniforms*)uniforms)->globaltime;
	GLuint channel0 = ((My_Uniforms*)uniforms)->tex2;
	GLuint channel1 = ((My_Uniforms*)uniforms)->tex0;

	vec2 resolution = make_v2(win_width, win_height);
	vec2 frag = *(vec2*)(&builtins->gl_FragCoord);

	vec2 p = v2_sub(v2_scale(v2_scale(frag, 2.0f), 1.0f/resolution.x), make_v2(1.0f, 1.0f));
	p.y = p.y * (resolution.y / resolution.x);
	
	float r = powf(powf(p.x * p.x, 16.0f) + powf(p.y * p.y, 16.0f), 1.0f/32.0f);
	
	vec2 uv;
	uv.x = 0.5f * globaltime + 0.5f / r;
	uv.y = atan2f(p.y, p.x) / 3.1416f;

	float h = sinf(32.0f * uv.y);
	uv.x += 0.85f * smoothstep_f(-0.1f, 0.1f, h);
	
	vec4 ch1_ = texture2D(channel1, 2.0f * uv.x, 2.0f * uv.y);
	vec4 ch0_ = texture2D(channel0, uv.x, uv.y);

	vec3 ch0 = make_v3(ch0_.x, ch0_.y, ch0_.z);
	vec3 ch1 = make_v3(ch1_.x, ch1_.y, ch1_.z);
	float aa = smoothstep_f(0.9f, 1.1f, fabsf(p.x / p.y));
	vec3 col = v3_mix(ch1, ch0, aa);

	r *= 1.0f - 0.3f * (smoothstep_f(0.0f, 0.3f, h) - smoothstep_f(0.3f, 0.96f, h));

	*(vec4*)&builtins->gl_FragColor = make_v4(col.x * r * r * 1.2f, col.y * r * r * 1.2f, col.z * r * r * 1.2f, 1.0f);
}

void tileable_water_caustic_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
#define TAU 6.28318530718f
#define MAX_ITER 5

	vec2 iResolution = make_v2(win_width, win_height);
	vec2 gl_FragCoord = *(vec2*)(&builtins->gl_FragCoord);

	float time = iGlobalTime * 0.5f + 23.0f;
	vec2 sp;
	sp.x = gl_FragCoord.x / iResolution.x;
	sp.y = gl_FragCoord.y / iResolution.y;

	vec2 p = v2_sub(v2_scale(sp, TAU), make_v2(250.0f, 250.0f));
	vec2 i = p;
	float c = 1.0f;
	float inten = 0.005f;

	for (int n = 0; n < MAX_ITER; n++)
	{
		float t = time * (1.0f - (3.5f / (float)(n+1)));
		i.x = p.x + cosf(t - i.x) + sinf(t + i.y);
		i.y = p.y + sinf(t - i.y) + cosf(t + i.x);
		vec2 len_vec = make_v2(
			p.x / (sinf(i.x + t) / inten),
			p.y / (cosf(i.y + t) / inten)
		);
		c += 1.0f / v2_length(len_vec);
	}
	c /= (float)MAX_ITER;
	c = 1.2f - powf(c, 1.2f);
	
	float colour_val = powf(fabsf(c), 6.0f);
	vec3 colour = make_v3(colour_val, colour_val, colour_val);
	
	vec3 final_col = v3_add(colour, make_v3(0.0f, 0.35f, 0.5f));
	final_col.x = clamp_f(final_col.x, 0.0f, 1.0f);
	final_col.y = clamp_f(final_col.y, 0.0f, 1.0f);
	final_col.z = clamp_f(final_col.z, 0.0f, 1.0f);

    *(vec4*)&builtins->gl_FragColor = make_v4(final_col.x, final_col.y, final_col.z, 1.0f);
}

void running_in_the_night_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
	vec2 iResolution = make_v2(win_width, win_height);
	vec2 gl_FragCoord = *(vec2*)(&builtins->gl_FragCoord);
	GLuint iChannel0 = ((My_Uniforms*)uniforms)->tex2;

	float time = 45.0f * 3.14159f / 180.0f + cosf(iGlobalTime * 12.0f) / 40.0f;
	float time1 = 45.0f * 3.14159f / 180.0f + cosf(iGlobalTime * 22.0f) / 30.0f;
	
	vec2 uv = v2_sub(v2_scale(gl_FragCoord, 2.0f / iResolution.x), make_v2(1.0f, 1.0f));
	uv.y = uv.y * (iResolution.y / iResolution.x);
	
	if (uv.y < 0.0f) {
		vec2 tex;
		vec2 rot = make_v2(cosf(time), sinf(time));
		vec2 mat;
		mat.x = (uv.x * rot.x + (uv.y - 1.0f) * rot.y);
		mat.y = ((uv.y - 1.0f) * rot.x - uv.x * rot.y);
		tex.x = mat.x * time1 / uv.y + iGlobalTime * 2.0f;
		tex.y = mat.y * time1 / uv.y + iGlobalTime * 2.0f;

		vec4 tmp = texture2D(iChannel0, tex.x * 2.0f, tex.y * 2.0f);
		*(vec4*)&builtins->gl_FragColor = make_v4(tmp.x * (-uv.y), tmp.y * (-uv.y), tmp.z * (-uv.y), 1.0f);
	} else {
		*(vec4*)&builtins->gl_FragColor = make_v4(0, 0, 0, 1.0f);
	}
}

vec3 hash3(vec2 p)
{
    vec3 q = make_v3(
		v2_dot(p, make_v2(127.1f, 311.7f)),
		v2_dot(p, make_v2(269.5f, 183.3f)),
		v2_dot(p, make_v2(419.2f, 371.9f))
	);
	return v3_fract(v3_scale(make_v3(sinf(q.x), sinf(q.y), sinf(q.z)), 43758.5453f));
}

float iqnoise(vec2 x, float u, float v)
{
    vec2 p = v2_floor(x);
    vec2 f = v2_fract(x);

	float k = 1.0f + 63.0f * powf(1.0f - v, 4.0f);

	float va = 0.0f;
	float wt = 0.0f;
    for (int j = -2; j <= 2; j++)
    for (int i = -2; i <= 2; i++)
    {
        vec2 g = make_v2((float)i, (float)j);
		vec3 o = v3_mul(hash3(v2_add(p, g)), make_v3(u, u, 1.0f));
		vec2 r = v2_add(v2_sub(g, f), make_v2(o.x, o.y));
		float d = v2_dot(r, r);
		float ww = powf(1.0f - smoothstep_f(0.0f, 1.414f, sqrtf(d)), k);
		va += o.z * ww;
		wt += ww;
    }

    return va / wt;
}

void voronoise_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
	vec2 iResolution = make_v2(win_width, win_height);
	vec2 gl_FragCoord = *(vec2*)(&builtins->gl_FragCoord);

	vec2 uv;
	uv.x = gl_FragCoord.x / iResolution.x;
	uv.y = gl_FragCoord.y / iResolution.x;

    vec2 p;
	p.x = 0.5f - 0.5f * sinf(iGlobalTime * 1.01f);
	p.y = 0.5f - 0.5f * sinf(iGlobalTime * 1.71f);

	float t1 = 3.0f - 2.0f * p.x;
	float t2 = 3.0f - 2.0f * p.y;
	p.x = p.x * p.x * t1;
	p.y = p.y * p.y * t2;
	p.x = p.x * p.x * (3.0f - 2.0f * p.x);
	p.y = p.y * p.y * (3.0f - 2.0f * p.y);
	p.x = p.x * p.x * (3.0f - 2.0f * p.x);
	p.y = p.y * p.y * (3.0f - 2.0f * p.y);

	float f = iqnoise(v2_scale(uv, 24.0f), p.x, p.y);

	*(vec4*)&builtins->gl_FragColor = make_v4(f, f, f, 1.0f);
}

float noise(vec3 p)
{
	vec3 i = v3_floor(p);
	vec3 v1 = make_v3(1.0f, 57.0f, 21.0f);
	vec3 v2 = make_v3(0.0f, 57.0f, 21.0f);
	vec3 v3 = make_v3(78.0f, 78.0f, 78.0f);
	vec4 a = v4_add(make_v4(v3_dot(i, v1), 0, 0, 0), make_v4(v2.x, v2.y, v2.z, v3.x));
	
	vec3 f = v3_scale(v3_sub(p, i), acosf(-1.0f));
	f.x = cosf(f.x) * (-0.5f) + 0.5f;
	f.y = cosf(f.y) * (-0.5f) + 0.5f;
	f.z = cosf(f.z) * (-0.5f) + 0.5f;
	
	float a1 = sinf(cosf(a.x) * a.x);
	float a2 = sinf(cosf(a.y) * a.y);
	float a3 = sinf(cosf(1.0f + a.z) * (1.0f + a.z));
	float a4 = sinf(cosf(1.0f + a.w) * (1.0f + a.w));
	
	float m1 = mix_f(a1, a3, f.x);
	float m2 = mix_f(a2, a4, f.x);
	
	return mix_f(m1, m2, f.z);
}

float sphere(vec3 p, vec4 spr)
{
	return v3_length(v3_sub(make_v3(spr.x, spr.y, spr.z), p)) - spr.w;
}

float flame_func(vec3 p)
{
	vec3 scaled_p = make_v3(p.x, p.y * 0.5f, p.z);
	vec4 spr = make_v4(0.0f, -1.0f, 0.0f, 1.0f);
	float d = sphere(scaled_p, spr);
	return d + (noise(v3_add(p, make_v3(0.0f, iGlobalTime * 2.0f, 0.0f))) + noise(v3_scale(p, 3.0f)) * 0.5f) * 0.25f * p.y;
}

float scene(vec3 p)
{
	return fminf(100.0f - v3_length(p), fabsf(flame_func(p)));
}

vec4 raymarch(vec3 org, vec3 dir)
{
	float d = 0.0f, glow = 0.0f, eps = 0.02f;
	vec3 p = org;
	bool glowed = GL_FALSE;

	for(int i = 0; i < 64; i++)
	{
		d = scene(p) + eps;
		p = v3_add(p, v3_scale(dir, d));
		if (d > eps)
		{
			if(flame_func(p) < 0.0f)
				glowed = true;
			if(glowed)
       			glow = (float)i / 64.0f;
		}
	}
	return make_v4(p.x, p.y, p.z, glow);
}

void flame_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
	vec2 iResolution = make_v2(win_width, win_height);
	vec2 gl_FragCoord = *(vec2*)(&builtins->gl_FragCoord);

	vec2 v;
	v.x = -1.0f + 2.0f * gl_FragCoord.x / iResolution.x;
	v.y = -1.0f + 2.0f * gl_FragCoord.y / iResolution.y;
	v.x *= iResolution.x / iResolution.y;

	vec3 org = make_v3(0.0f, -2.0f, 4.0f);
	vec3 dir = v3_norm(make_v3(v.x * 1.6f, -v.y, -1.5f));

	vec4 p = raymarch(org, dir);
	float glow = p.w;

	vec4 col1 = make_v4(1.0f, 0.5f, 0.1f, 1.0f);
	vec4 col2 = make_v4(0.1f, 0.5f, 1.0f, 1.0f);
	vec4 col = v4_mix(col1, col2, p.y * 0.02f + 0.4f);

    float intensity = powf(glow * 2.0f, 4.0f);
    if (intensity > 1.0f) intensity = 1.0f;
    *(vec4*)&builtins->gl_FragColor = make_v4(col.x * intensity, col.y * intensity, col.z * intensity, 1.0f);
}

// Constants for the cave shader
const vec2 cama = {-2.6943f, 3.0483f};
const vec2 camb = {0.2516f, 0.1749f};
const vec2 camc = {-3.7902f, 2.4478f};
const vec2 camd = {0.0865f, -0.1664f};

const vec2 lighta = {1.4301f, 4.0985f};
const vec2 lightb = {-0.1276f, 0.2347f};
const vec2 lightc = {-2.2655f, 1.5066f};
const vec2 lightd = {-0.1284f, 0.0731f};

inline vec2 Position(float z, vec2 a, vec2 b, vec2 c, vec2 d)
{
	return v2_add(v2_scale(make_v2(sinf(z * a.x), sinf(z * a.y)), b.x), 
	              v2_scale(make_v2(cosf(z * c.x), cosf(z * c.y)), d.x));
}

inline vec3 Position3D(float time, vec2 a, vec2 b, vec2 c, vec2 d)
{
	vec2 pos = Position(time, a, b, c, d);
	return make_v3(pos.x, pos.y, time);
}

inline float Distance_(vec3 p, vec2 a, vec2 b, vec2 c, vec2 d, vec2 e, float r)
{
	vec2 pos = Position(p.z, a, b, c, d);
	float radius = fmaxf(5.0f, r + sinf(p.z * e.x) * e.y) / 10000.0f;
	vec2 diff = v2_sub(make_v2(p.x, p.y), pos);
	return radius / v2_dot(diff, diff);
}

float Dist2D(vec3 pos)
{
	float d = 0.0f;

	d += Distance_(pos, cama, camb, camc, camd, make_v2(2.1913f, 15.4634f), 70.0000f);
	d += Distance_(pos, lighta, lightb, lightc, lightd, make_v2(0.3814f, 12.7206f), 17.0590f);
	
	return d;
}

inline vec3 nmap(vec2 t, GLuint tx, float str)
{
	float d = 1.0f / 1024.0f;

	float xy = texture2D(tx, t.x, t.y).x;
	float x2 = texture2D(tx, t.x + d, t.y).x;
	float y2 = texture2D(tx, t.x, t.y + d).x;

	float s = (1.0f - str) * 1.2f;
	s *= s;
	s *= s;

	return v3_norm(make_v3(x2 - xy, y2 - xy, s / 8.0f));
}

void the_cave_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
	float globaltime = ((My_Uniforms*)uniforms)->globaltime;
	GLuint iChannel0 = ((My_Uniforms*)uniforms)->tex6;
	GLuint iChannel1 = ((My_Uniforms*)uniforms)->tex1;
	GLuint iChannel2 = ((My_Uniforms*)uniforms)->tex9;

	vec2 iResolution = make_v2(win_width, win_height);
	vec2 gl_FragCoord = *(vec2*)(&builtins->gl_FragCoord);

	float time = globaltime / 12.0f + 291.0f;

	vec2 p1 = Position(time + 0.05f, cama, camb, camc, camd);
	vec3 Pos = Position3D(time, cama, camb, camc, camd);
	vec3 oPos = Pos;

	vec3 CamDir = v3_norm(make_v3(p1.x - Pos.x, -p1.y + Pos.y, 0.1f));
	vec3 CamRight = v3_norm(v3_cross(CamDir, make_v3(0, 1, 0)));
	vec3 CamUp = v3_norm(v3_cross(CamRight, CamDir));

	vec2 uv = v2_sub(v2_scale(gl_FragCoord, 2.0f / iResolution.x), make_v2(1.0f, 1.0f));
	uv.y = uv.y * (iResolution.y / iResolution.x);
	float aspect = iResolution.x / iResolution.y;

	vec3 Dir = v3_norm(make_v3(uv.x * aspect, uv.y, 1.0f));
	// Apply camera rotation (simplified)
	Dir = v3_add(v3_add(v3_scale(CamRight, Dir.x), v3_scale(CamUp, Dir.y)), v3_scale(CamDir, Dir.z));

	float fade = 0.0f;
	const float numit = 75.0f;
	const float threshold = 1.20f;
	const float scale = 1.5f;

	vec3 Posm1 = Pos;

	for (float x = 0.0f; x < numit; x++)
	{
		if (Dist2D(Pos) < threshold)
		{
			fade = 1.0f - x / numit;
			break;
		}
		Posm1 = Pos;
		Pos = v3_add(Pos, v3_scale(Dir, scale / numit));
	}

	for (int x = 0; x < 6; x++)
	{
		vec3 p2 = v3_scale(v3_add(Posm1, Pos), 0.5f);
		if (Dist2D(p2) < threshold)
			Pos = p2;
		else
			Posm1 = p2;
	}

	// Simplified lighting
	vec3 lp = Position3D(time + 0.5f, cama, camb, camc, camd);
	vec3 ld = v3_sub(lp, Pos);
	float lv = 1.0f;

	const float ShadowIT = 15.0f;
	for (float x = 1.0f; x < ShadowIT; x++) {
		if (Dist2D(v3_add(Pos, v3_scale(ld, x / ShadowIT))) < threshold) {
			lv = 0.0f;
			break;
		}
	}

	vec3 tuv = make_v3(Pos.x * 3.0f, Pos.y * 3.0f, Pos.z * 1.5f);
	float nms = 0.19f;
	
	vec4 tx_ = texture2D(iChannel0, tuv.y, tuv.z);
	vec4 ty_ = texture2D(iChannel1, tuv.x, tuv.z);
	vec4 tz_ = texture2D(iChannel2, tuv.x, tuv.y);
	
	vec4 col = make_v4(
		(tx_.x + ty_.x + tz_.x) * 0.33f,
		(tx_.y + ty_.y + tz_.y) * 0.33f,
		(tx_.z + ty_.z + tz_.z) * 0.33f,
		1.0f
	);

	vec4 diff = make_v4(lv * 1.2f + 0.2f, lv * 1.2f + 0.2f, lv * 1.2f + 0.2f, 1.0f);
	
	float ff = fminf(1.0f, fade * 10.0f);
	*(vec4*)&builtins->gl_FragColor = make_v4(
		col.x * diff.x * ff,
		col.y * diff.y * ff,
		col.z * diff.z * ff,
		1.0f
	);
}

float eye_hash(float n)
{
	float x = sinf(n) * 43758.5453123f;
	return x - floorf(x);
}

float eye_noise(vec2 x)
{
	vec2 p = v2_floor(x);
	vec2 f = v2_sub(x, p);
	f = v2_mul(f, v2_mul(f, v2_sub(make_v2(3.0f, 3.0f), v2_scale(f, 2.0f))));

	float n = p.x + p.y * 57.0f;

	return mix_f(
		mix_f(eye_hash(n + 0.0f), eye_hash(n + 1.0f), f.x),
		mix_f(eye_hash(n + 57.0f), eye_hash(n + 58.0f), f.x),
		f.y
	);
}

float fbm(vec2 p)
{
	// Simplified fbm
	float f = 0.0f;
	f += 0.5000f * eye_noise(p); 
	f += 0.2500f * eye_noise(v2_scale(p, 2.0f));
	f += 0.1250f * eye_noise(v2_scale(p, 4.0f));
	f += 0.0625f * eye_noise(v2_scale(p, 8.0f));
	return f / 0.9375f;
}

void iqs_eyeball(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
	float time = ((My_Uniforms*)uniforms)->globaltime;

	vec2 uv;
	uv.x = (*(vec2*)(&builtins->gl_FragCoord)).x / win_width;
	uv.y = (*(vec2*)(&builtins->gl_FragCoord)).y / win_height;
	
	vec2 p;
	p.x = -1.0f + 2.0f * uv.x;
	p.y = -1.0f + 2.0f * uv.y;
	p.x *= win_width / (float)win_height;

	float r = sqrtf(v2_dot(p, p));
	float a = atan2f(p.y, p.x);

	vec3 bg_col = make_v3(1.0f, 1.0f, 1.0f);
	vec3 col = bg_col;

	float ss = 0.5f + 0.5f * sinf(time);
	float anim = 1.0f + 0.1f * ss * clamp_f(1.0f - r, 0.0f, 1.0f);
	r *= anim;

	if (r < 0.8f) {
		col = make_v3(0.0f, 0.3f, 0.4f);

		float f = fbm(v2_scale(p, 5.0f));
		col = v3_mix(col, make_v3(0.2f, 0.5f, 0.4f), f);

		f = 1.0f - smoothstep_f(0.2f, 0.5f, r);
		col = v3_mix(col, make_v3(0.9f, 0.6f, 0.2f), f);

		a += 0.05f * fbm(v2_scale(p, 20.0f));

		f = smoothstep_f(0.3f, 1.0f, fbm(make_v2(10.0f * r, 20.0f * a)));
		col = v3_mix(col, make_v3(1.0f, 1.0f, 1.0f), f);

		f = smoothstep_f(0.4f, 0.9f, fbm(make_v2(10.0f * r, 15.0f * a)));
		col = v3_scale(col, 1.0f - 0.5f * f);

		f = smoothstep_f(0.6f, 0.8f, r);
		col = v3_scale(col, 1.0f - 0.5f * f);

		f = smoothstep_f(0.2f, 0.25f, r);
		col = v3_scale(col, f);

		vec2 pp = v2_sub(p, make_v2(0.24f, 0.2f));
		f = 1.0f - smoothstep_f(0.0f, 0.3f, sqrtf(v2_dot(pp, pp)));
		col = v3_add(col, v3_scale(make_v3(1.0f, 0.9f, 0.8f), f * 0.8f));

		f = smoothstep_f(0.75f, 0.8f, r);
		col = v3_mix(col, bg_col, f);
	}

	*(vec4*)&builtins->gl_FragColor = make_v4(col.x, col.y, col.z, 1.0f);
}
