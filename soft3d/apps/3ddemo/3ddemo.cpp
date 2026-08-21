
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

static int win_width = 640;
static int win_height = 480;
static glContext the_Context;
static pix_t* backbuf = NULL;
static xwin_t* g_xwin = NULL;

#include "src/obj.c"
#include "src/texture.c"

static GLuint program, vao, vbo;
static GLuint texture;

struct My_Uniforms {
	mat4 mvp_mat;
	GLuint tex0;
};
static My_Uniforms the_uniforms;

void basic_transform_vp(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms) {
    (void)vs_output;
    My_Uniforms* u = (My_Uniforms*)uniforms;
    builtins->gl_Position = mult_m4_v4(u->mvp_mat, vertex_attribs[0]);
    vs_output[0] = vertex_attribs[1].x;
    vs_output[1] = 1.0f - vertex_attribs[1].y; // Flip V coordinate
}

void texture_frag_fp(float* fs_input, Shader_Builtins* builtins, void* uniforms) {
    My_Uniforms* u = (My_Uniforms*)uniforms;
    float u_coord = fs_input[0];
    float v_coord = fs_input[1];
    GLuint tex = u->tex0;
    builtins->gl_FragColor = texture2D(tex, u_coord, v_coord);
}

static void init_mesh() {
    float* vertex_data = (float*)malloc(num_faces * 3 * 5 * sizeof(float));
    int idx = 0;
    for (int i = 0; i < num_faces; i++) {
        for (int j = 0; j < 3; j++) {
            int vidx;
            float u, v;
            if (j == 0) {
                vidx = faces[i].a - 1;
                u = faces[i].a_uv.u;
                v = faces[i].a_uv.v;
            } else if (j == 1) {
                vidx = faces[i].b - 1;
                u = faces[i].b_uv.u;
                v = faces[i].b_uv.v;
            } else {
                vidx = faces[i].c - 1;
                u = faces[i].c_uv.u;
                v = faces[i].c_uv.v;
            }

            vertex_data[idx++] = vertices[vidx].x;
            vertex_data[idx++] = vertices[vidx].y;
            vertex_data[idx++] = vertices[vidx].z;
            vertex_data[idx++] = u;
            vertex_data[idx++] = v;
        }
    }

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, num_faces * 3 * 5 * sizeof(float), vertex_data, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    free(vertex_data);
}

static void init_texture() {
    extern int texture_width;
    extern int texture_height;
    extern uint32_t* mesh_texture;

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    int num_pixels = texture_width * texture_height;
    uint8_t* rgba_data = (uint8_t*)malloc(num_pixels * 4);
    for (int i = 0; i < num_pixels; i++) {
        uint32_t argb = ((uint32_t*)mesh_texture)[i];
        uint8_t a = (argb >> 24) & 0xFF;
        uint8_t r = (argb >> 16) & 0xFF;
        uint8_t g = (argb >> 8) & 0xFF;
        uint8_t b = argb & 0xFF;
        rgba_data[i * 4 + 0] = r;
        rgba_data[i * 4 + 1] = g;
        rgba_data[i * 4 + 2] = b;
        rgba_data[i * 4 + 3] = a;
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture_width, texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    free(rgba_data);
}

void init_gl() {
	GLenum interpolation[2] = { PGL_SMOOTH, PGL_SMOOTH };
	program = CreateProgram(basic_transform_vp, texture_frag_fp, 2, interpolation, GL_FALSE);
	glUseProgram(program);
	SetUniform(&the_uniforms);

	init_mesh();
	init_texture();

	glViewport(0, 0, win_width, win_height);
	glEnable(GL_DEPTH_TEST);

	the_uniforms.tex0 = texture;
	printf("Texture ID: %u\n", texture);
}

static float camera_angle = 0;

static void render_mesh() {
    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    mat4 view_mat, proj_mat, model_mat, mvp_mat, scale_mat;

    // 摄像机围绕模型水平旋转（类似gears）
    float cam_radius = 4.0f;
    float cam_x = sinf(camera_angle) * cam_radius;
    float cam_z = cosf(camera_angle) * cam_radius;
    float cam_y = 1.0f;
    
    // 摄像机始终看向模型中心
    lookAt(view_mat, make_v3(cam_x, cam_y, cam_z), make_v3(0.0f, 0.0f, 0.0f), make_v3(0.0f, 1.0f, 0.0f));
    make_perspective_m4(proj_mat, DEG_TO_RAD(60.0f), win_width / (float)win_height, 0.1f, 100.0f);

    // 模型不旋转，只缩放（模型中心已通过顶点数据偏移）
    scale_m4(scale_mat, 1.0f, 1.0f, 1.0f);
    memcpy(model_mat, scale_mat, sizeof(mat4));

    mult_m4_m4(mvp_mat, proj_mat, view_mat);
    mult_m4_m4(mvp_mat, mvp_mat, model_mat);

    memcpy(the_uniforms.mvp_mat, mvp_mat, sizeof(float) * 16);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, num_faces * 3);
}

static void on_repaint(xwin_t* xwin, graph_t* g) {
    (void)xwin;
    camera_angle += 0.05f;
    render_mesh();
    graph_fill_rect(g, 0, 0, win_width, win_height, 0xFF1a1a2e);
    graph_t bg;
    graph_init(&bg, backbuf, win_width, win_height);
    graph_blt(&bg, 0, 0, win_width, win_height, g, 0, 0, win_width, win_height);
}

static void on_event(xwin_t* xwin, xevent_t* ev) {
    if (ev->type == XEVT_IM && ev->state == XIM_STATE_PRESS && ev->value.im.value == KEY_ESC)
        xwin_close(xwin);
}

static void on_resize(xwin_t* xwin) {
    if (!xwin || !xwin->xinfo) return;
    win_width = xwin->xinfo->wsr.w;
    win_height = xwin->xinfo->wsr.h;
    if (win_width <= 0 || win_height <= 0) return;
    ResizeFramebuffer(win_width, win_height);
    backbuf = (pix_t*)GetBackBuffer();
    glViewport(0, 0, win_width, win_height);
    glClearColor(0.1f, 0.1f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void loop(void* p) {
    xwin_t* xwin = (xwin_t*)p;
    xwin_repaint(xwin);
    proc_usleep(3000);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    x_t x;
    x_init(&x, NULL);
    x.on_loop = loop;
    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "3D F22 Demo", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin_set_alpha(xwin, false);
    g_xwin = xwin;
    xwin->on_repaint = on_repaint;
    xwin->on_event = on_event;
    xwin->on_resize = on_resize;

    pgl_set_max_vertices(PGL_TINY_MAX_VERTICES);
    if (!init_glContext(&the_Context, (u32**)&backbuf, win_width, win_height)) {
        printf("Failed to initialize glContext\n");
        return 1;
    }

    init_gl();
    xwin_set_visible(xwin, true);
    x_run(&x, xwin);
    xwin_destroy(xwin);
    free_glContext(&the_Context);
    return 0;
}
