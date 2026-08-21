// Simplified grass demo using standard OpenGL
#include "glcommon/gltools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
#include <x/x.h>
#include <x/xwin.h>
#include <graph/graph.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define sinf pgl_fast_sin
#define cosf pgl_fast_cos
#define sqrtf pgl_fast_sqrt
#define tanf pgl_fast_tan
#define acosf pgl_fast_acos
#define asinf pgl_fast_asin
#define atanf pgl_fast_atan
#define atanf2 pgl_fast_atan2

// Window dimensions
static int win_width = 640;
static int win_height = 480;

static glContext the_Context;
static pix_t* backbuf = NULL;
static xwin_t* g_xwin = NULL;

// Simple camera structure
struct Camera {
    float pos[3];
    float rot[3];
    
    Camera() {
        pos[0] = 0; pos[1] = 0.5f; pos[2] = 3.0f;
        rot[0] = 0; rot[1] = 0; rot[2] = 0;
    }
    
    void get_matrix(float* mat) {
        float eye[3] = {pos[0], pos[1], pos[2]};
        float center[3] = {pos[0], pos[1], pos[2] - 1.0f};
        float up[3] = {0, 1, 0};
        
        float f[3] = {center[0]-eye[0], center[1]-eye[1], center[2]-eye[2]};
        float len = sqrtf(f[0]*f[0] + f[1]*f[1] + f[2]*f[2]);
        if (len > 0.0001f) { f[0]/=len; f[1]/=len; f[2]/=len; }
        
        float s[3] = {f[1]*up[2] - f[2]*up[1], f[2]*up[0] - f[0]*up[2], f[0]*up[1] - f[1]*up[0]};
        len = sqrtf(s[0]*s[0] + s[1]*s[1] + s[2]*s[2]);
        if (len > 0.0001f) { s[0]/=len; s[1]/=len; s[2]/=len; }
        
        float u[3] = {s[1]*f[2] - s[2]*f[1], s[2]*f[0] - s[0]*f[2], s[0]*f[1] - s[1]*f[0]};
        
        mat[0] = s[0]; mat[1] = u[0]; mat[2] = -f[0]; mat[3] = 0;
        mat[4] = s[1]; mat[5] = u[1]; mat[6] = -f[1]; mat[7] = 0;
        mat[8] = s[2]; mat[9] = u[2]; mat[10] = -f[2]; mat[11] = 0;
        mat[12] = -(s[0]*eye[0] + s[1]*eye[1] + s[2]*eye[2]);
        mat[13] = -(u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]);
        mat[14] = f[0]*eye[0] + f[1]*eye[1] + f[2]*eye[2];
        mat[15] = 1.0f;
    }
    
    void move_forward(float dist) {
        float rad = rot[1] * M_PI / 180.0f;
        pos[0] -= sinf(rad) * dist;
        pos[2] -= cosf(rad) * dist;
    }
    
    void move_right(float dist) {
        float rad = rot[1] * M_PI / 180.0f;
        pos[0] += cosf(rad) * dist;
        pos[2] -= sinf(rad) * dist;
    }
    
    void rotate_y(float deg) {
        rot[1] += deg;
    }
    
    void rotate_x(float deg) {
        rot[0] += deg;
    }
};

// Matrix operations
void mat4_multiply(float* result, const float* a, const float* b) {
    float temp[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            temp[j*4+i] = 0;
            for (int k = 0; k < 4; k++) {
                temp[j*4+i] += a[k*4+i] * b[j*4+k];
            }
        }
    }
    memcpy(result, temp, sizeof(temp));
}

void mat4_perspective(float* m, float fovy, float aspect, float near, float far) {
    float f = 1.0f / tanf(fovy / 2.0f);
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1.0f;
    m[14] = (2.0f * far * near) / (near - far);
}

typedef struct My_Uniforms {
    float mvp_mat[16];
    float color[4];
} My_Uniforms;

My_Uniforms the_uniforms;
Camera camera;
float proj_mat[16];

int random(int seed, int iterations) {
    unsigned int val = seed;
    for (int n = 0; n < iterations; ++n) {
        val = ((val >> 7) ^ (val << 9)) * 15485863;
    }
    return (int)val;
}

void grass_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms);
void simple_color_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);

static void on_repaint(xwin_t* xwin, graph_t* g)
{
    (void)xwin;
    
    // Clear the framebuffer
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Update view matrix
    float view_mat[16];
    camera.get_matrix(view_mat);
    mat4_multiply(the_uniforms.mvp_mat, proj_mat, view_mat);
    
    // Draw grass
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 6, 256*256);
    
    // Blit to screen
    graph_t bg;
    graph_init(&bg, backbuf, win_width, win_height);
    graph_blt(&bg, 0, 0, win_width, win_height, g, 0, 0, win_width, win_height);
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    (void)xwin;
    
    if (ev->type == XEVT_IM) {
        int key = ev->value.im.value;
        int32_t state = ev->state;
        
        if (state == XIM_STATE_PRESS) {
            switch (key) {
            case KEY_ESC:
                xwin_close(xwin);
                break;
            case KEY_LEFT:
                camera.rotate_y(-5.0f);
                break;
            case KEY_RIGHT:
                camera.rotate_y(5.0f);
                break;
            case KEY_UP:
                camera.move_forward(0.5f);
                break;
            case KEY_DOWN:
                camera.move_forward(-0.5f);
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
    
    // Update projection matrix
    mat4_perspective(proj_mat, 45.0f * M_PI / 180.0f, (float)win_width/(float)win_height, 0.01f, 100.0f);
    
    // Clear the framebuffer after resize
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void loop(void* p)
{
    xwin_t* xwin = (xwin_t*)p;
    xwin_repaint(xwin);
    proc_usleep(16000); // ~60 FPS
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    
    x_t x;
    x_init(&x, NULL);
    
    x.on_loop = loop;
    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "Grass", XWIN_STYLE_NORMAL);
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
    
    // Initialize camera
    camera.pos[0] = 0; camera.pos[1] = 5.0f; camera.pos[2] = 20.0f;
    
    GLfloat grass_blade[] = {
        -0.3f,  0.0f,
         0.3f,  0.0f,
        -0.20f, 1.0f,
         0.1f, 1.3f,
        -0.05f, 2.3f,
         0.0f,  3.3f
    };

    GLuint vao, buffer;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(grass_blade), grass_blade, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);

    GLuint myshader = CreateProgram(grass_vs, simple_color_fs, 0, NULL, GL_FALSE);
    glUseProgram(myshader);

    mat4_perspective(proj_mat, 45.0f * M_PI / 180.0f, (float)win_width/(float)win_height, 0.01f, 100.0f);

    SetUniform(&the_uniforms);
    the_uniforms.color[0] = 0.0f;
    the_uniforms.color[1] = 1.0f;
    the_uniforms.color[2] = 0.0f;
    the_uniforms.color[3] = 1.0f;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    
    // Make window visible
    xwin_set_visible(xwin, true);
    
    x_run(&x, xwin);
    
    // Cleanup
    xwin_destroy(xwin);
    free_glContext(&the_Context);
    
    return 0;
}

void grass_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms) {
    My_Uniforms* u = (My_Uniforms*)uniforms;
    GLint inst = builtins->gl_InstanceID;

    float ox = float(inst >> 8) - 128.0f;
    float oz = float(inst & 0xFF) - 128.0f;

    int num1 = random(inst, 3);
    int num2 = random(num1, 2);

    ox += float(num1 & 0xFF) / 128.0f;
    oz += float(num2 & 0xFF) / 128.0f;

    float angle1 = float(num2);
    float st = sinf(angle1);
    float ct = cosf(angle1);

    float vx = vertex_attribs[0].x;
    float vy = vertex_attribs[0].y;

    float rx = ct * vx + st * vy;
    float rz = -st * vx + ct * vy;

    vec4 pos = make_v4(rx + ox, vy, rz + oz, 1.0f);

    float* m = u->mvp_mat;
    builtins->gl_Position.x = m[0]*pos.x + m[4]*pos.y + m[8]*pos.z + m[12]*pos.w;
    builtins->gl_Position.y = m[1]*pos.x + m[5]*pos.y + m[9]*pos.z + m[13]*pos.w;
    builtins->gl_Position.z = m[2]*pos.x + m[6]*pos.y + m[10]*pos.z + m[14]*pos.w;
    builtins->gl_Position.w = m[3]*pos.x + m[7]*pos.y + m[11]*pos.z + m[15]*pos.w;
}

void simple_color_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms) {
    (void)fs_input;
    My_Uniforms* u = (My_Uniforms*)uniforms;
    builtins->gl_FragColor = make_v4(u->color[0], u->color[1], u->color[2], u->color[3]);
}
