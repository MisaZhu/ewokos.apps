#include "glcommon/gltools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>

#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/keydef.h>
#include <ewoksys/kernel_tic.h>
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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG_TO_RAD(x)   ((x) * 0.017453292519943296)

// Forward declarations
void mat4_multiply(float* result, const float* a, const float* b);

static int win_width = 640;
static int win_height = 480;

static glContext the_Context;
static pix_t* backbuf = NULL;
static xwin_t* g_xwin = NULL;

static GLuint sphere_vao;
static GLuint skybox_vao;
static int sphere_vertex_count;
static int skybox_vertex_count;

struct Camera {
    float pos[3];
    float rot[3];

    Camera() {
        pos[0] = 0; pos[1] = 0; pos[2] = 4.0f;
        rot[0] = 0; rot[1] = 0; rot[2] = 0;
    }

    void get_matrix(float* mat) {
        // Build rotation matrix from euler angles
        float rx = rot[0] * M_PI / 180.0f;
        float ry = rot[1] * M_PI / 180.0f;
        float rz = rot[2] * M_PI / 180.0f;

        float cx = cosf(rx), sx = sinf(rx);
        float cy = cosf(ry), sy = sinf(ry);
        float cz = cosf(rz), sz = sinf(rz);

        // Combined rotation matrix (Z * Y * X)
        float rot_mat[16];
        rot_mat[0] = cy*cz; rot_mat[1] = cy*sz; rot_mat[2] = -sy; rot_mat[3] = 0;
        rot_mat[4] = sx*sy*cz - cx*sz; rot_mat[5] = sx*sy*sz + cx*cz; rot_mat[6] = sx*cy; rot_mat[7] = 0;
        rot_mat[8] = cx*sy*cz + sx*sz; rot_mat[9] = cx*sy*sz - sx*cz; rot_mat[10] = cx*cy; rot_mat[11] = 0;
        rot_mat[12] = 0; rot_mat[13] = 0; rot_mat[14] = 0; rot_mat[15] = 1;

        // Translation matrix
        float trans_mat[16];
        trans_mat[0] = 1; trans_mat[1] = 0; trans_mat[2] = 0; trans_mat[3] = 0;
        trans_mat[4] = 0; trans_mat[5] = 1; trans_mat[6] = 0; trans_mat[7] = 0;
        trans_mat[8] = 0; trans_mat[9] = 0; trans_mat[10] = 1; trans_mat[11] = 0;
        trans_mat[12] = -pos[0]; trans_mat[13] = -pos[1]; trans_mat[14] = -pos[2]; trans_mat[15] = 1;

        // View = rot * trans
        mat4_multiply(mat, rot_mat, trans_mat);
    }

    void get_rotation_only(float* mat) {
        float rx = rot[0] * M_PI / 180.0f;
        float ry = rot[1] * M_PI / 180.0f;
        float rz = rot[2] * M_PI / 180.0f;

        float cx = cosf(rx), sx = sinf(rx);
        float cy = cosf(ry), sy = sinf(ry);
        float cz = cosf(rz), sz = sinf(rz);

        mat[0] = cy*cz; mat[1] = cy*sz; mat[2] = -sy; mat[3] = 0;
        mat[4] = sx*sy*cz - cx*sz; mat[5] = sx*sy*sz + cx*cz; mat[6] = sx*cy; mat[7] = 0;
        mat[8] = cx*sy*cz + sx*sz; mat[9] = cx*sy*sz - sx*cz; mat[10] = cx*cy; mat[11] = 0;
        mat[12] = 0; mat[13] = 0; mat[14] = 0; mat[15] = 1;
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

    void rotate_local_y(float rad) {
        rot[1] += rad * 180.0f / M_PI;
    }

    void rotate_local_x(float rad) {
        rot[0] += rad * 180.0f / M_PI;
    }
};

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

void mat3_from_mat4(float* m3, const float* m4) {
    m3[0] = m4[0]; m3[1] = m4[1]; m3[2] = m4[2];
    m3[3] = m4[4]; m3[4] = m4[5]; m3[5] = m4[6];
    m3[6] = m4[8]; m3[7] = m4[9]; m3[8] = m4[10];
}

void mat4_invert(float* inv, const float* m) {
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];

    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (fabsf(det) < 0.0001f) return;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) inv[i] *= det;
}

void vec3_reflect(float* r, const float* i, const float* n) {
    float d = 2.0f * (i[0]*n[0] + i[1]*n[1] + i[2]*n[2]);
    r[0] = i[0] - d * n[0];
    r[1] = i[1] - d * n[1];
    r[2] = i[2] - d * n[2];
}

void mat4_vec3_mult(float* v_out, const float* m, const float* v) {
    float x = m[0]*v[0] + m[4]*v[1] + m[8]*v[2] + m[12];
    float y = m[1]*v[0] + m[5]*v[1] + m[9]*v[2] + m[13];
    float z = m[2]*v[0] + m[6]*v[1] + m[10]*v[2] + m[14];
    v_out[0] = x; v_out[1] = y; v_out[2] = z;
}

void mat3_vec3_mult(float* v_out, const float* m, const float* v) {
    float x = m[0]*v[0] + m[3]*v[1] + m[6]*v[2];
    float y = m[1]*v[0] + m[4]*v[1] + m[7]*v[2];
    float z = m[2]*v[0] + m[5]*v[1] + m[8]*v[2];
    v_out[0] = x; v_out[1] = y; v_out[2] = z;
}

typedef struct My_Uniforms {
    float mvp_mat[16];
    float mv_mat[16];
    float normal_mat[9];
    float inverse_camera[16];
    GLuint tex;
} My_Uniforms;

My_Uniforms the_uniforms;
Camera camera;
float proj_mat[16];
float view_mat[16];
float view_rot_mat[16];
GLuint reflection_shader;
GLuint skybox_shader;

void make_box(std::vector<float>& draw_verts, float w, float h, float d) {
    float hw = w / 2.0f;
    float hh = h / 2.0f;
    float hd = d / 2.0f;

    // 6 faces, each with 2 triangles = 12 triangles = 36 vertices
    // Faces are wound clockwise (for skybox we see from inside)

    // Front face (z = -hd) - looking at -Z, normal points to +Z (inside)
    draw_verts.push_back(-hw); draw_verts.push_back(-hh); draw_verts.push_back(-hd);
    draw_verts.push_back(-hw); draw_verts.push_back( hh); draw_verts.push_back(-hd);
    draw_verts.push_back( hw); draw_verts.push_back( hh); draw_verts.push_back(-hd);
    draw_verts.push_back(-hw); draw_verts.push_back(-hh); draw_verts.push_back(-hd);
    draw_verts.push_back( hw); draw_verts.push_back( hh); draw_verts.push_back(-hd);
    draw_verts.push_back( hw); draw_verts.push_back(-hh); draw_verts.push_back(-hd);

    // Back face (z = hd) - looking at +Z, normal points to -Z (inside)
    draw_verts.push_back( hw); draw_verts.push_back(-hh); draw_verts.push_back(hd);
    draw_verts.push_back( hw); draw_verts.push_back( hh); draw_verts.push_back(hd);
    draw_verts.push_back(-hw); draw_verts.push_back( hh); draw_verts.push_back(hd);
    draw_verts.push_back( hw); draw_verts.push_back(-hh); draw_verts.push_back(hd);
    draw_verts.push_back(-hw); draw_verts.push_back( hh); draw_verts.push_back(hd);
    draw_verts.push_back(-hw); draw_verts.push_back(-hh); draw_verts.push_back(hd);

    // Right face (x = hw) - looking at +X, normal points to -X (inside)
    draw_verts.push_back(hw); draw_verts.push_back(-hh); draw_verts.push_back(-hd);
    draw_verts.push_back(hw); draw_verts.push_back( hh); draw_verts.push_back(-hd);
    draw_verts.push_back(hw); draw_verts.push_back( hh); draw_verts.push_back( hd);
    draw_verts.push_back(hw); draw_verts.push_back(-hh); draw_verts.push_back(-hd);
    draw_verts.push_back(hw); draw_verts.push_back( hh); draw_verts.push_back( hd);
    draw_verts.push_back(hw); draw_verts.push_back(-hh); draw_verts.push_back( hd);

    // Left face (x = -hw) - looking at -X, normal points to +X (inside)
    draw_verts.push_back(-hw); draw_verts.push_back(-hh); draw_verts.push_back( hd);
    draw_verts.push_back(-hw); draw_verts.push_back( hh); draw_verts.push_back( hd);
    draw_verts.push_back(-hw); draw_verts.push_back( hh); draw_verts.push_back(-hd);
    draw_verts.push_back(-hw); draw_verts.push_back(-hh); draw_verts.push_back( hd);
    draw_verts.push_back(-hw); draw_verts.push_back( hh); draw_verts.push_back(-hd);
    draw_verts.push_back(-hw); draw_verts.push_back(-hh); draw_verts.push_back(-hd);

    // Top face (y = hh) - looking at +Y, normal points to -Y (inside)
    draw_verts.push_back(-hw); draw_verts.push_back(hh); draw_verts.push_back(-hd);
    draw_verts.push_back(-hw); draw_verts.push_back(hh); draw_verts.push_back( hd);
    draw_verts.push_back( hw); draw_verts.push_back(hh); draw_verts.push_back( hd);
    draw_verts.push_back(-hw); draw_verts.push_back(hh); draw_verts.push_back(-hd);
    draw_verts.push_back( hw); draw_verts.push_back(hh); draw_verts.push_back( hd);
    draw_verts.push_back( hw); draw_verts.push_back(hh); draw_verts.push_back(-hd);

    // Bottom face (y = -hh) - looking at -Y, normal points to +Y (inside)
    draw_verts.push_back(-hw); draw_verts.push_back(-hh); draw_verts.push_back( hd);
    draw_verts.push_back(-hw); draw_verts.push_back(-hh); draw_verts.push_back(-hd);
    draw_verts.push_back( hw); draw_verts.push_back(-hh); draw_verts.push_back(-hd);
    draw_verts.push_back(-hw); draw_verts.push_back(-hh); draw_verts.push_back( hd);
    draw_verts.push_back( hw); draw_verts.push_back(-hh); draw_verts.push_back(-hd);
    draw_verts.push_back( hw); draw_verts.push_back(-hh); draw_verts.push_back( hd);
}

void make_sphere(std::vector<float>& draw_verts, float radius, int segments, int rings) {
    std::vector<float> sphere_pos;

    for (int ring = 0; ring <= rings; ring++) {
        float phi = M_PI * ring / rings;
        for (int seg = 0; seg <= segments; seg++) {
            float theta = 2.0f * M_PI * seg / segments;
            float x = sinf(phi) * cosf(theta);
            float y = cosf(phi);
            float z = sinf(phi) * sinf(theta);
            sphere_pos.push_back(x * radius);
            sphere_pos.push_back(y * radius);
            sphere_pos.push_back(z * radius);
        }
    }

    for (int ring = 0; ring < rings; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            int curr = ring * (segments + 1) + seg;
            int next = curr + segments + 1;

            float v0x = sphere_pos[curr * 3], v0y = sphere_pos[curr * 3 + 1], v0z = sphere_pos[curr * 3 + 2];
            float v1x = sphere_pos[next * 3], v1y = sphere_pos[next * 3 + 1], v1z = sphere_pos[next * 3 + 2];
            float v2x = sphere_pos[(curr + 1) * 3], v2y = sphere_pos[(curr + 1) * 3 + 1], v2z = sphere_pos[(curr + 1) * 3 + 2];
            float v3x = sphere_pos[(next + 1) * 3], v3y = sphere_pos[(next + 1) * 3 + 1], v3z = sphere_pos[(next + 1) * 3 + 2];

            draw_verts.push_back(v0x); draw_verts.push_back(v0y); draw_verts.push_back(v0z);
            draw_verts.push_back(v1x); draw_verts.push_back(v1y); draw_verts.push_back(v1z);
            draw_verts.push_back(v2x); draw_verts.push_back(v2y); draw_verts.push_back(v2z);

            draw_verts.push_back(v1x); draw_verts.push_back(v1y); draw_verts.push_back(v1z);
            draw_verts.push_back(v3x); draw_verts.push_back(v3y); draw_verts.push_back(v3z);
            draw_verts.push_back(v2x); draw_verts.push_back(v2y); draw_verts.push_back(v2z);
        }
    }
}

void reflection_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms);
void reflection_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void skybox_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms);
void skybox_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);

static void on_repaint(xwin_t* xwin, graph_t* g)
{
    (void)xwin;

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera.get_matrix(view_mat);
    camera.get_rotation_only(view_rot_mat);

    // Render skybox first (disable depth test)
    float skybox_mvp[16];
    mat4_multiply(skybox_mvp, proj_mat, view_rot_mat);

    // Use a separate uniform block for skybox
    My_Uniforms skybox_uniforms;
    for (int i = 0; i < 16; i++) skybox_uniforms.mvp_mat[i] = skybox_mvp[i];
    skybox_uniforms.tex = the_uniforms.tex;

    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(skybox_shader);
    pglSetUniform(&skybox_uniforms);
    glBindVertexArray(skybox_vao);
    glDrawArrays(GL_TRIANGLES, 0, skybox_vertex_count);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // Render sphere with reflection
    // Sphere is at origin, model matrix is identity
    float mvp_mat[16];
    mat4_multiply(mvp_mat, proj_mat, view_mat);

    float normal_mat[9];
    mat3_from_mat4(normal_mat, view_mat);

    float inverse_camera[16];
    mat4_invert(inverse_camera, view_rot_mat);

    for (int i = 0; i < 16; i++) the_uniforms.mvp_mat[i] = mvp_mat[i];
    for (int i = 0; i < 16; i++) the_uniforms.mv_mat[i] = view_mat[i];
    for (int i = 0; i < 9; i++) the_uniforms.normal_mat[i] = normal_mat[i];
    for (int i = 0; i < 16; i++) the_uniforms.inverse_camera[i] = inverse_camera[i];

    glUseProgram(reflection_shader);
    pglSetUniform(&the_uniforms);
    glBindVertexArray(sphere_vao);
    glDrawArrays(GL_TRIANGLES, 0, sphere_vertex_count);

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
                camera.rotate_local_y(DEG_TO_RAD(-5.0f));
                break;
            case KEY_RIGHT:
                camera.rotate_local_y(DEG_TO_RAD(5.0f));
                break;
            case KEY_UP:
                camera.rotate_local_x(DEG_TO_RAD(-5.0f));
                break;
            case KEY_DOWN:
                camera.rotate_local_x(DEG_TO_RAD(5.0f));
                break;
            }
        }
    }
}

static void on_resize(xwin_t* xwin)
{
    if (xwin == NULL || xwin->xinfo == NULL)
        return;

    int width = xwin->xinfo->wsr.w;
    int height = xwin->xinfo->wsr.h;

    if (width <= 0 || height <= 0)
        return;

    win_width = width;
    win_height = height;

    ResizeFramebuffer(win_width, win_height);
    backbuf = (pix_t*)GetBackBuffer();

    glViewport(0, 0, win_width, win_height);

    mat4_perspective(proj_mat, 35.0f * M_PI / 180.0f, (float)win_width / (float)win_height, 1.0f, 100.0f);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void loop(void* p)
{
    xwin_t* xwin = (xwin_t*)p;
    xwin_repaint(xwin);
    proc_usleep(16000);
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    x_t x;
    x_init(&x, NULL);

    x.on_loop = loop;
    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "Cubemap", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    xwin_set_alpha(xwin, false);

    g_xwin = xwin;
    xwin->on_repaint = on_repaint;
    xwin->on_event = on_event;
    xwin->on_resize = on_resize;

    if (!init_glContext(&the_Context, (u32**)&backbuf, win_width, win_height)) {
        printf("Failed to initialize glContext\n");
        return 1;
    }

    glViewport(0, 0, win_width, win_height);

    camera.pos[0] = 0; camera.pos[1] = 0; camera.pos[2] = 3.0f;

    const char* cube_map_textures[] = {
        "/data/media/textures/skybox/right.jpg",
        "/data/media/textures/skybox/left.jpg",
        "/data/media/textures/skybox/top.jpg",
        "/data/media/textures/skybox/bottom.jpg",
        "/data/media/textures/skybox/front.jpg",
        "/data/media/textures/skybox/back.jpg"
    };

    GLuint cube_map_tex;
    glGenTextures(1, &cube_map_tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cube_map_tex);
    load_texture_cubemap(cube_map_textures, GL_NEAREST, GL_NEAREST, GL_FALSE, GL_TRUE);
    the_uniforms.tex = cube_map_tex;

    std::vector<float> skybox_verts;
    make_box(skybox_verts, 40.0f, 40.0f, 40.0f);
    skybox_vertex_count = skybox_verts.size() / 3;

    glGenVertexArrays(1, &skybox_vao);
    glBindVertexArray(skybox_vao);

    GLuint skybox_vbo;
    glGenBuffers(1, &skybox_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, skybox_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * skybox_verts.size(), &skybox_verts[0], GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

    std::vector<float> sphere_verts;
    make_sphere(sphere_verts, 0.5f, 32, 16);
    sphere_vertex_count = sphere_verts.size() / 3;

    glGenVertexArrays(1, &sphere_vao);
    glBindVertexArray(sphere_vao);

    GLuint sphere_vbo;
    glGenBuffers(1, &sphere_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, sphere_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * sphere_verts.size(), &sphere_verts[0], GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

    reflection_shader = pglCreateProgram(reflection_vs, reflection_fs, 3, NULL, GL_FALSE);
    skybox_shader = pglCreateProgram(skybox_vs, skybox_fs, 3, NULL, GL_FALSE);

    mat4_perspective(proj_mat, 35.0f * M_PI / 180.0f, (float)win_width / (float)win_height, 1.0f, 100.0f);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glCullFace(GL_BACK);

    xwin_set_visible(xwin, true);

    x_run(&x, xwin);

    xwin_destroy(xwin);
    free_glContext(&the_Context);

    return 0;
}

void reflection_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms) {
    My_Uniforms* u = (My_Uniforms*)uniforms;

    float vx = vertex_attribs[0].x;
    float vy = vertex_attribs[0].y;
    float vz = vertex_attribs[0].z;

    float eye_normal[3];
    eye_normal[0] = u->normal_mat[0] * vx + u->normal_mat[3] * vy + u->normal_mat[6] * vz;
    eye_normal[1] = u->normal_mat[1] * vx + u->normal_mat[4] * vy + u->normal_mat[7] * vz;
    eye_normal[2] = u->normal_mat[2] * vx + u->normal_mat[5] * vy + u->normal_mat[8] * vz;
    float len = sqrtf(eye_normal[0]*eye_normal[0] + eye_normal[1]*eye_normal[1] + eye_normal[2]*eye_normal[2]);
    if (len > 0.0001f) {
        eye_normal[0] /= len; eye_normal[1] /= len; eye_normal[2] /= len;
    }

    float eye_vert[3];
    eye_vert[0] = u->mv_mat[0]*vx + u->mv_mat[4]*vy + u->mv_mat[8]*vz + u->mv_mat[12];
    eye_vert[1] = u->mv_mat[1]*vx + u->mv_mat[5]*vy + u->mv_mat[9]*vz + u->mv_mat[13];
    eye_vert[2] = u->mv_mat[2]*vx + u->mv_mat[6]*vy + u->mv_mat[10]*vz + u->mv_mat[14];
    len = sqrtf(eye_vert[0]*eye_vert[0] + eye_vert[1]*eye_vert[1] + eye_vert[2]*eye_vert[2]);
    if (len > 0.0001f) {
        eye_vert[0] /= len; eye_vert[1] /= len; eye_vert[2] /= len;
    }

    float reflect_vec[3];
    vec3_reflect(reflect_vec, eye_vert, eye_normal);

    float coords[4] = {reflect_vec[0], reflect_vec[1], reflect_vec[2], 1.0f};

    float world_vec[3];
    world_vec[0] = u->inverse_camera[0]*coords[0] + u->inverse_camera[4]*coords[1] + u->inverse_camera[8]*coords[2] + u->inverse_camera[12]*coords[3];
    world_vec[1] = u->inverse_camera[1]*coords[0] + u->inverse_camera[5]*coords[1] + u->inverse_camera[9]*coords[2] + u->inverse_camera[13]*coords[3];
    world_vec[2] = u->inverse_camera[2]*coords[0] + u->inverse_camera[6]*coords[1] + u->inverse_camera[10]*coords[2] + u->inverse_camera[14]*coords[3];
    len = sqrtf(world_vec[0]*world_vec[0] + world_vec[1]*world_vec[1] + world_vec[2]*world_vec[2]);
    if (len > 0.0001f) {
        world_vec[0] /= len; world_vec[1] /= len; world_vec[2] /= len;
    }

    ((float*)vs_output)[0] = world_vec[0];
    ((float*)vs_output)[1] = world_vec[1];
    ((float*)vs_output)[2] = world_vec[2];

    builtins->gl_Position.x = u->mvp_mat[0]*vx + u->mvp_mat[4]*vy + u->mvp_mat[8]*vz + u->mvp_mat[12];
    builtins->gl_Position.y = u->mvp_mat[1]*vx + u->mvp_mat[5]*vy + u->mvp_mat[9]*vz + u->mvp_mat[13];
    builtins->gl_Position.z = u->mvp_mat[2]*vx + u->mvp_mat[6]*vy + u->mvp_mat[10]*vz + u->mvp_mat[14];
    builtins->gl_Position.w = u->mvp_mat[3]*vx + u->mvp_mat[7]*vy + u->mvp_mat[11]*vz + u->mvp_mat[15];
}

void reflection_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms) {
    float* tex_coords = (float*)fs_input;
    builtins->gl_FragColor = texture_cubemap(((My_Uniforms*)uniforms)->tex, tex_coords[0], tex_coords[1], tex_coords[2]);
}

void skybox_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms) {
    My_Uniforms* u = (My_Uniforms*)uniforms;

    float vx = vertex_attribs[0].x;
    float vy = vertex_attribs[0].y;
    float vz = vertex_attribs[0].z;

    // For cubemap, we use the vertex position as the direction vector
    // Flip Y because cubemap textures are usually top-down
    ((float*)vs_output)[0] = vx;
    ((float*)vs_output)[1] = -vy;
    ((float*)vs_output)[2] = vz;

    builtins->gl_Position.x = u->mvp_mat[0]*vx + u->mvp_mat[4]*vy + u->mvp_mat[8]*vz + u->mvp_mat[12];
    builtins->gl_Position.y = u->mvp_mat[1]*vx + u->mvp_mat[5]*vy + u->mvp_mat[9]*vz + u->mvp_mat[13];
    builtins->gl_Position.z = u->mvp_mat[2]*vx + u->mvp_mat[6]*vy + u->mvp_mat[10]*vz + u->mvp_mat[14];
    builtins->gl_Position.w = u->mvp_mat[3]*vx + u->mvp_mat[7]*vy + u->mvp_mat[11]*vz + u->mvp_mat[15];
}

void skybox_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms) {
    float* tex_coords = (float*)fs_input;
    builtins->gl_FragColor = texture_cubemap(((My_Uniforms*)uniforms)->tex, tex_coords[0], tex_coords[1], tex_coords[2]);
}
