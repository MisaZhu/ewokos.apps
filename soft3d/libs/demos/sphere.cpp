
#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "glcommon/gltools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>

#include <ewoksys/klog.h>

#define sinf pgl_fast_sin
#define cosf pgl_fast_cos
#define sqrtf pgl_fast_sqrt
#define tanf pgl_fast_tan
#define acosf pgl_fast_acos
#define asinf pgl_fast_asin
#define atanf pgl_fast_atan
#define atanf2 pgl_fast_atan2

#define WIDTH 640
#define HEIGHT 480

#define PIX_FORMAT SDL_PIXELFORMAT_ARGB8888

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Forward declarations for math functions
void mat4_multiply(float* result, const float* a, const float* b);
void mat4_perspective(float* m, float fovy, float aspect, float near, float far);
void mat4_identity(float* m);
void mat4_translation(float* m, float x, float y, float z);
void mat4_rotation(float* m, float angle, float x, float y, float z);
void mat4_scale(float* m, float x, float y, float z);
void mat3_from_mat4(float* m3, const float* m4);

// Use vec3 and vec4 from portablegl
// They are already defined in portablegl.h

struct Camera {
    float pos[3];
    float rot[3];

    Camera() {
        pos[0] = 0; pos[1] = 0.3f; pos[2] = 2.5f;
        rot[0] = 0; rot[1] = 0; rot[2] = 0;
    }

    void get_matrix(float* mat) {
        float rx = rot[0] * M_PI / 180.0f;
        float ry = rot[1] * M_PI / 180.0f;
        float rz = rot[2] * M_PI / 180.0f;

        float cx = cosf(rx), sx = sinf(rx);
        float cy = cosf(ry), sy = sinf(ry);
        float cz = cosf(rz), sz = sinf(rz);

        float rot_mat[16];
        rot_mat[0] = cy*cz; rot_mat[1] = cy*sz; rot_mat[2] = -sy; rot_mat[3] = 0;
        rot_mat[4] = sx*sy*cz - cx*sz; rot_mat[5] = sx*sy*sz + cx*cz; rot_mat[6] = sx*cy; rot_mat[7] = 0;
        rot_mat[8] = cx*sy*cz + sx*sz; rot_mat[9] = cx*sy*sz - sx*cz; rot_mat[10] = cx*cy; rot_mat[11] = 0;
        rot_mat[12] = 0; rot_mat[13] = 0; rot_mat[14] = 0; rot_mat[15] = 1;

        float trans_mat[16];
        trans_mat[0] = 1; trans_mat[1] = 0; trans_mat[2] = 0; trans_mat[3] = 0;
        trans_mat[4] = 0; trans_mat[5] = 1; trans_mat[6] = 0; trans_mat[7] = 0;
        trans_mat[8] = 0; trans_mat[9] = 0; trans_mat[10] = 1; trans_mat[11] = 0;
        trans_mat[12] = -pos[0]; trans_mat[13] = -pos[1]; trans_mat[14] = -pos[2]; trans_mat[15] = 1;

        mat4_multiply(mat, rot_mat, trans_mat);
    }

    void move_right(float dist) {
        float rad = rot[1] * M_PI / 180.0f;
        pos[0] += cosf(rad) * dist;
        pos[2] -= sinf(rad) * dist;
    }

    void move_up(float dist) {
        pos[1] += dist;
    }

    void move_forward(float dist) {
        float rad = rot[1] * M_PI / 180.0f;
        pos[0] -= sinf(rad) * dist;
        pos[2] -= cosf(rad) * dist;
    }

    void rotate_local_x(float deg) {
        rot[0] += deg;
    }

    void rotate_local_y(float deg) {
        rot[1] += deg;
    }

    void rotate_local_z(float deg) {
        rot[2] += deg;
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

void mat4_identity(float* m) {
    for (int i = 0; i < 16; i++) m[i] = 0;
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void mat4_translation(float* m, float x, float y, float z) {
    mat4_identity(m);
    m[12] = x; m[13] = y; m[14] = z;
}

void mat4_rotation(float* m, float angle, float x, float y, float z) {
    float c = cosf(angle);
    float s = sinf(angle);
    float t = 1.0f - c;

    float len = sqrtf(x*x + y*y + z*z);
    if (len > 0.0001f) {
        x /= len; y /= len; z /= len;
    }

    m[0] = t*x*x + c;     m[1] = t*x*y + s*z;   m[2] = t*x*z - s*y;   m[3] = 0;
    m[4] = t*x*y - s*z;   m[5] = t*y*y + c;     m[6] = t*y*z + s*x;   m[7] = 0;
    m[8] = t*x*z + s*y;   m[9] = t*y*z - s*x;   m[10] = t*z*z + c;    m[11] = 0;
    m[12] = 0;            m[13] = 0;            m[14] = 0;            m[15] = 1;
}

void mat4_scale(float* m, float x, float y, float z) {
    mat4_identity(m);
    m[0] = x; m[5] = y; m[10] = z;
}

void mat3_from_mat4(float* m3, const float* m4) {
    m3[0] = m4[0]; m3[1] = m4[1]; m3[2] = m4[2];
    m3[3] = m4[4]; m3[4] = m4[5]; m3[5] = m4[6];
    m3[6] = m4[8]; m3[7] = m4[9]; m3[8] = m4[10];
}

vec3 vec3_add(vec3 a, vec3 b) {
    return make_v3(a.x + b.x, a.y + b.y, a.z + b.z);
}

vec3 vec3_scale(vec3 v, float s) {
    return make_v3(v.x * s, v.y * s, v.z * s);
}

float vec3_dot(vec3 a, vec3 b) {
    return a.x*b.x + a.y*b.y + a.z*b.z;
}

vec3 vec3_cross(vec3 a, vec3 b) {
    return make_v3(
        a.y*b.z - a.z*b.y,
        a.z*b.x - a.x*b.z,
        a.x*b.y - a.y*b.x
    );
}

void vec3_normalize(vec3& v) {
    float len = sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
    if (len > 0.0001f) {
        v.x /= len; v.y /= len; v.z /= len;
    }
}

vec3 vec3_reflect(vec3 i, vec3 n) {
    float d = 2.0f * vec3_dot(i, n);
    return make_v3(i.x - d*n.x, i.y - d*n.y, i.z - d*n.z);
}

vec3 vec3_neg(vec3 v) {
    return make_v3(-v.x, -v.y, -v.z);
}

float randf_range(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

void make_torus(std::vector<vec3>& verts, std::vector<int>& tris, std::vector<vec2>& texcoords,
                float major_radius, float minor_radius, int major_segments, int minor_segments) {
    // Create vertex grid
    // Torus is closed in both i (major circle) and j (minor circle) directions
    // Both directions need texture coordinate wrapping
    
    // Vertex array size: (major_segments) * (minor_segments)
    // Each vertex has unique 3D position and texture coordinates
    for (int i = 0; i < major_segments; i++) {
        float theta = 2.0f * M_PI * i / major_segments;
        float cos_theta = cosf(theta);
        float sin_theta = sinf(theta);

        for (int j = 0; j < minor_segments; j++) {
            float phi = 2.0f * M_PI * j / minor_segments;
            float cos_phi = cosf(phi);
            float sin_phi = sinf(phi);

            float x = (major_radius + minor_radius * cos_phi) * cos_theta;
            float y = minor_radius * sin_phi;
            float z = (major_radius + minor_radius * cos_phi) * sin_theta;

            verts.push_back(make_v3(x, y, z));
            // Texture coordinates range [0,1), properly wrapped at edges
            texcoords.push_back({(float)i / major_segments, (float)j / minor_segments});
        }
    }

    // Generate triangle indices with proper wrapping in both directions
    for (int i = 0; i < major_segments; i++) {
        int next_i = (i + 1) % major_segments;
        for (int j = 0; j < minor_segments; j++) {
            int next_j = (j + 1) % minor_segments;
            
            int curr = i * minor_segments + j;
            int right = i * minor_segments + next_j;
            int down = next_i * minor_segments + j;
            int diag = next_i * minor_segments + next_j;

            // First triangle
            tris.push_back(curr);
            tris.push_back(down);
            tris.push_back(right);
            
            // Second triangle
            tris.push_back(right);
            tris.push_back(down);
            tris.push_back(diag);
        }
    }
}

void make_sphere(std::vector<vec3>& verts, std::vector<int>& tris, std::vector<vec2>& texcoords,
                 float radius, int segments, int rings) {
    for (int ring = 0; ring <= rings; ring++) {
        float phi = M_PI * ring / rings;
        for (int seg = 0; seg <= segments; seg++) {
            float theta = 2.0f * M_PI * seg / segments;
            float x = sinf(phi) * cosf(theta);
            float y = cosf(phi);
            float z = sinf(phi) * sinf(theta);
            verts.push_back(make_v3(x * radius, y * radius, z * radius));
            texcoords.push_back({(float)seg / segments, (float)ring / rings});
        }
    }

    for (int ring = 0; ring < rings; ring++) {
        for (int seg = 0; seg < segments; seg++) {
            int curr = ring * (segments + 1) + seg;
            int next = curr + segments + 1;
            tris.push_back(curr); tris.push_back(next); tris.push_back(curr + 1);
            tris.push_back(curr + 1); tris.push_back(next); tris.push_back(next + 1);
        }
    }
}

void compute_normals(const std::vector<vec3>& verts, const std::vector<int>& tris,
                     std::vector<vec3>& normals, float smooth_angle) {
    normals.resize(verts.size());
    for (size_t i = 0; i < normals.size(); i++) normals[i] = make_v3(0, 0, 0);

    std::vector<int> counts(verts.size(), 0);

    for (size_t i = 0; i < tris.size(); i += 3) {
        int i0 = tris[i], i1 = tris[i+1], i2 = tris[i+2];
        vec3 v0 = verts[i0], v1 = verts[i1], v2 = verts[i2];

        vec3 edge1 = make_v3(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
        vec3 edge2 = make_v3(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);
        vec3 n = vec3_cross(edge1, edge2);
        vec3_normalize(n);

        normals[i0] = vec3_add(normals[i0], n);
        normals[i1] = vec3_add(normals[i1], n);
        normals[i2] = vec3_add(normals[i2], n);
        counts[i0]++; counts[i1]++; counts[i2]++;
    }

    for (size_t i = 0; i < normals.size(); i++) {
        if (counts[i] > 0) {
            normals[i] = vec3_scale(normals[i], 1.0f / counts[i]);
            vec3_normalize(normals[i]);
        }
    }
}

struct My_Uniforms {
    float mvp_mat[16];
    float normal_mat[9];
    vec4 color;
    vec3 light_dir;
    vec3 Ka, Kd, Ks;
    float shininess;
};

SDL_Window* window;
SDL_Renderer* ren;
SDL_Texture* tex;
glContext the_Context;
pix_t* bbufpix;

int width, height;
float proj_mat[16];

enum {
    ATTR_VERTEX = 0,
    ATTR_NORMAL = 1,
    ATTR_TEXCOORD = 2,
    ATTR_INSTANCE = 3
};

struct vert_attribs {
    float pos[3];
    float normal[3];
};

#define NUM_SHADERS 2

int polygon_mode;
GLuint shaders[NUM_SHADERS];
int cur_shader;
My_Uniforms the_uniforms;

void basic_transform_vp(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms);
void uniform_color_fp(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void gouraud_ads_vp(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms);
void gouraud_ads_fp(float* fs_input, Shader_Builtins* builtins, void* uniforms);
void phong_ads_vp(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms);
void phong_ads_fp(float* fs_input, Shader_Builtins* builtins, void* uniforms);

void setup_context() {
    if (SDL_Init(SDL_INIT_VIDEO)) {
        printf("SDL_Init error: %s\n", SDL_GetError());
        exit(0);
    }

    width = WIDTH;
    height = HEIGHT;

    window = SDL_CreateWindow("Sphereworld Color", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) exit(0);

    ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    tex = SDL_CreateTexture(ren, PIX_FORMAT, SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);

    if (!init_glContext(&the_Context, &bbufpix, WIDTH, HEIGHT)) {
        puts("Failed to initialize glContext");
        exit(0);
    }
}

void cleanup() {
    free_glContext(&the_Context);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

int handle_events(Camera& camera, unsigned int last_time, unsigned int cur_time);

#define FLOOR_SIZE 40
#define NUM_SPHERES 50

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    setup_context();

    polygon_mode = 2;
    mat4_perspective(proj_mat, DEG_TO_RAD(35.0f), WIDTH/(float)HEIGHT, 0.3f, 100.0f);

    std::vector<vec3> line_verts;
    for (int i = 0, j = -FLOOR_SIZE/2; i < 11; ++i, j += FLOOR_SIZE/10) {
        line_verts.push_back(make_v3(j, -1, -FLOOR_SIZE/2));
        line_verts.push_back(make_v3(j, -1, FLOOR_SIZE/2));
        line_verts.push_back(make_v3(-FLOOR_SIZE/2, -1, j));
        line_verts.push_back(make_v3(FLOOR_SIZE/2, -1, j));
    }

    GLuint line_vao, line_buf;
    glGenVertexArrays(1, &line_vao);
    glBindVertexArray(line_vao);

    glGenBuffers(1, &line_buf);
    glBindBuffer(GL_ARRAY_BUFFER, line_buf);
    glBufferData(GL_ARRAY_BUFFER, line_verts.size()*3*sizeof(float), &line_verts[0], GL_STATIC_DRAW);
    glEnableVertexAttribArray(ATTR_VERTEX);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);

    std::vector<vec3> torus_verts, torus_normals;
    std::vector<int> torus_tris;
    std::vector<vec2> torus_texcoords;

    std::vector<vec3> sphere_verts, sphere_normals;
    std::vector<int> sphere_tris;
    std::vector<vec2> sphere_texcoords;

    make_torus(torus_verts, torus_tris, torus_texcoords, 0.3f, 0.1f, 40, 20);
    make_sphere(sphere_verts, sphere_tris, sphere_texcoords, 0.1f, 26, 13);

    // Rotate torus 90 degrees around X
    for (size_t i = 0; i < torus_verts.size(); ++i) {
        float y = torus_verts[i].y;
        float z = torus_verts[i].z;
        torus_verts[i].y = y * 0 - z * 1;
        torus_verts[i].z = y * 1 + z * 0;
    }

    compute_normals(torus_verts, torus_tris, torus_normals, DEG_TO_RAD(30));
    compute_normals(sphere_verts, sphere_tris, sphere_normals, DEG_TO_RAD(30));

    std::vector<vert_attribs> vert_data;

    for (size_t i = 0; i < torus_tris.size(); i += 3) {
        int v0 = torus_tris[i], v1 = torus_tris[i+1], v2 = torus_tris[i+2];
        vert_attribs va0 = {{torus_verts[v0].x, torus_verts[v0].y, torus_verts[v0].z},
                           {torus_normals[v0].x, torus_normals[v0].y, torus_normals[v0].z}};
        vert_attribs va1 = {{torus_verts[v1].x, torus_verts[v1].y, torus_verts[v1].z},
                           {torus_normals[v1].x, torus_normals[v1].y, torus_normals[v1].z}};
        vert_attribs va2 = {{torus_verts[v2].x, torus_verts[v2].y, torus_verts[v2].z},
                           {torus_normals[v2].x, torus_normals[v2].y, torus_normals[v2].z}};
        vert_data.push_back(va0);
        vert_data.push_back(va1);
        vert_data.push_back(va2);
    }

    size_t sphere_offset = vert_data.size();

    for (size_t i = 0; i < sphere_tris.size(); i += 3) {
        int v0 = sphere_tris[i], v1 = sphere_tris[i+1], v2 = sphere_tris[i+2];
        vert_attribs va0 = {{sphere_verts[v0].x, sphere_verts[v0].y, sphere_verts[v0].z},
                           {sphere_normals[v0].x, sphere_normals[v0].y, sphere_normals[v0].z}};
        vert_attribs va1 = {{sphere_verts[v1].x, sphere_verts[v1].y, sphere_verts[v1].z},
                           {sphere_normals[v1].x, sphere_normals[v1].y, sphere_normals[v1].z}};
        vert_attribs va2 = {{sphere_verts[v2].x, sphere_verts[v2].y, sphere_verts[v2].z},
                           {sphere_normals[v2].x, sphere_normals[v2].y, sphere_normals[v2].z}};
        vert_data.push_back(va0);
        vert_data.push_back(va1);
        vert_data.push_back(va2);
    }

    std::vector<float> instance_pos;
    for (int i = 0; i < NUM_SPHERES + 1; ++i) {
        float rx = randf_range(-FLOOR_SIZE/2.0f, FLOOR_SIZE/2.0f);
        float rz = randf_range(-FLOOR_SIZE/2.0f, FLOOR_SIZE/2.0f);
        if (i) {
            instance_pos.push_back(rx); instance_pos.push_back(0.4f); instance_pos.push_back(rz);
        } else {
            instance_pos.push_back(0); instance_pos.push_back(0); instance_pos.push_back(0);
        }
    }

    GLuint vao, buffer;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, vert_data.size() * sizeof(vert_attribs), &vert_data[0], GL_STATIC_DRAW);

    glEnableVertexAttribArray(ATTR_VERTEX);
    glVertexAttribPointer(ATTR_VERTEX, 3, GL_FLOAT, GL_FALSE, sizeof(vert_attribs), 0);
    glEnableVertexAttribArray(ATTR_NORMAL);
    glVertexAttribPointer(ATTR_NORMAL, 3, GL_FLOAT, GL_FALSE, sizeof(vert_attribs), (void*)(3*sizeof(float)));

    GLuint inst_buf;
    glGenBuffers(1, &inst_buf);
    glBindBuffer(GL_ARRAY_BUFFER, inst_buf);
    glBufferData(GL_ARRAY_BUFFER, instance_pos.size() * sizeof(float), &instance_pos[0], GL_STATIC_DRAW);
    glEnableVertexAttribArray(ATTR_INSTANCE);
    glVertexAttribPointer(ATTR_INSTANCE, 3, GL_FLOAT, GL_FALSE, 0, 0);
    glVertexAttribDivisor(ATTR_INSTANCE, 1);

    GLenum interpolation[5] = { PGL_SMOOTH3 };

    GLuint basic_shader = pglCreateProgram(basic_transform_vp, uniform_color_fp, 0, NULL, GL_FALSE);
    glUseProgram(basic_shader);
    pglSetUniform(&the_uniforms);

    shaders[0] = pglCreateProgram(gouraud_ads_vp, gouraud_ads_fp, 3, interpolation, GL_FALSE);
    glUseProgram(shaders[0]);
    pglSetUniform(&the_uniforms);

    shaders[1] = pglCreateProgram(phong_ads_vp, phong_ads_fp, 3, interpolation, GL_FALSE);
    glUseProgram(shaders[1]);
    pglSetUniform(&the_uniforms);

    cur_shader = 0;

    Camera camera;
    camera.pos[0] = 0; camera.pos[1] = 0.3f; camera.pos[2] = 2.5f;

    vec4 floor_color = make_v4(0, 1, 0, 1);
    vec3 torus_ambient = make_v3(0.0f, 0, 0);
    vec3 torus_diffuse = make_v3(1.0f, 0, 0);
    vec3 torus_specular = make_v3(0, 0, 0);
    vec3 sphere_ambient = make_v3(0, 0, 0.2f);
    vec3 sphere_diffuse = make_v3(0, 0, 0.7f);
    vec3 sphere_specular = make_v3(1, 1, 1);

    the_uniforms.color = floor_color;
    the_uniforms.shininess = 128.0f;
    vec3 light_direction = make_v3(0, 10, 5);
    vec3_normalize(light_direction);

    glUseProgram(basic_shader);

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    SDL_SetRelativeMouseMode(SDL_TRUE);

    unsigned int old_time = 0, counter = 0, last_time = SDL_GetTicks();
    float total_time = 0;

    while (1) {
        unsigned int new_time = SDL_GetTicks();
        if (handle_events(camera, last_time, new_time)) break;

        last_time = new_time;
        total_time = new_time / 1000.0f;
        if (new_time - old_time > 3000) {
            printf("%f FPS\n", counter * 1000.f / (new_time - old_time));
            old_time = new_time;
            counter = 0;
        }

        bbufpix = (pix_t*)GetBackBuffer();
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        for (int i = 0; i < width * height; ++i) {
            bbufpix[i] = 0xFF000000;
        }

        float view_mat[16];
        camera.get_matrix(view_mat);

        float mvp_mat[16];
        mat4_multiply(mvp_mat, proj_mat, view_mat);

        glUseProgram(basic_shader);
        memcpy(the_uniforms.mvp_mat, mvp_mat, sizeof(mvp_mat));
        glBindVertexArray(line_vao);
        glDrawArrays(GL_LINES, 0, line_verts.size());

        glBindVertexArray(vao);
        glUseProgram(shaders[cur_shader]);

        the_uniforms.light_dir = light_direction;
        mat3_from_mat4(the_uniforms.normal_mat, view_mat);
        the_uniforms.Ka = sphere_ambient;
        the_uniforms.Kd = sphere_diffuse;
        the_uniforms.Ks = sphere_specular;

        glDrawArraysInstanced(GL_TRIANGLES, sphere_offset, sphere_tris.size(), NUM_SPHERES);

        // Rotating sphere
        float rot_mat[16], trans_mat[16], temp_mat[16];
        mat4_rotation(rot_mat, -total_time * DEG_TO_RAD(60.0f), 0, 1, 0);
        mat4_translation(trans_mat, 0.8f, 0.4f, 0.0f);
        mat4_multiply(temp_mat, rot_mat, trans_mat);
        mat4_multiply(temp_mat, view_mat, temp_mat);
        mat4_multiply(the_uniforms.mvp_mat, proj_mat, temp_mat);
        mat3_from_mat4(the_uniforms.normal_mat, temp_mat);

        glDrawArrays(GL_TRIANGLES, sphere_offset, sphere_tris.size());

        // Rotating torus
        mat4_rotation(rot_mat, total_time * DEG_TO_RAD(60.0f), 0, 1, 0);
        mat4_translation(trans_mat, 0.0f, 0.3f, -1.2f);
        mat4_multiply(temp_mat, rot_mat, trans_mat);
        mat4_multiply(temp_mat, view_mat, temp_mat);
        mat4_multiply(the_uniforms.mvp_mat, proj_mat, temp_mat);
        mat3_from_mat4(the_uniforms.normal_mat, temp_mat);

        the_uniforms.Ka = torus_ambient;
        the_uniforms.Kd = torus_diffuse;
        the_uniforms.Ks = torus_specular;

        glDrawArrays(GL_TRIANGLES, 0, torus_tris.size());

        SDL_UpdateTexture(tex, NULL, bbufpix, width * sizeof(pix_t));
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderFillRect(ren, NULL);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        ++counter;
    }

    cleanup();
    return 0;
}

int handle_events(Camera& camera, unsigned int last_time, unsigned int cur_time) {
    SDL_Event event;
    int sc;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_KEYDOWN:
            sc = event.key.keysym.sym;
            if (sc == SDLK_ESCAPE) return 1;
            else if (sc == SDLK_p) {
                polygon_mode = (polygon_mode + 1) % 3;
                if (polygon_mode == 0) glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
                else if (polygon_mode == 1) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                else glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
            } else if (sc == SDLK_l) {
                cur_shader = (cur_shader + 1) % NUM_SHADERS;
            }
            break;

        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                width = event.window.data1;
                height = event.window.data2;
                ResizeFramebuffer(width, height);
                bbufpix = (pix_t*)GetBackBuffer();
                glViewport(0, 0, width, height);
                SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, PIX_FORMAT, SDL_TEXTUREACCESS_STREAMING, width, height);
                mat4_perspective(proj_mat, DEG_TO_RAD(35.0f), width/(float)height, 0.3f, 100.0f);
            }
            break;

        case SDL_MOUSEMOTION: {
            float degx = event.motion.xrel / 20.0f;
            float degy = event.motion.yrel / 20.0f;
            camera.rotate_local_y(-DEG_TO_RAD(degx));
            camera.rotate_local_x(DEG_TO_RAD(degy));
            break;
        }

        case SDL_QUIT:
            return 1;
        }
    }

    const Uint8 *state = SDL_GetKeyboardState(NULL);
    float time = (cur_time - last_time) / 1000.0f;
    float move_speed = 5.0f;

    if (state[SDL_SCANCODE_A]) camera.move_right(time * move_speed);
    if (state[SDL_SCANCODE_D]) camera.move_right(time * -move_speed);
    if (state[SDL_SCANCODE_LSHIFT]) camera.move_up(time * move_speed);
    if (state[SDL_SCANCODE_SPACE]) camera.move_up(time * -move_speed);
    if (state[SDL_SCANCODE_W]) camera.move_forward(time * move_speed);
    if (state[SDL_SCANCODE_S]) camera.move_forward(time * -move_speed);
    if (state[SDL_SCANCODE_Q]) camera.rotate_local_z(-DEG_TO_RAD(60 * time));
    if (state[SDL_SCANCODE_E]) camera.rotate_local_z(DEG_TO_RAD(60 * time));

    return 0;
}

void basic_transform_vp(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms) {
    My_Uniforms* u = (My_Uniforms*)uniforms;
    vec4 v = vertex_attribs[0];
    builtins->gl_Position.x = u->mvp_mat[0]*v.x + u->mvp_mat[4]*v.y + u->mvp_mat[8]*v.z + u->mvp_mat[12]*v.w;
    builtins->gl_Position.y = u->mvp_mat[1]*v.x + u->mvp_mat[5]*v.y + u->mvp_mat[9]*v.z + u->mvp_mat[13]*v.w;
    builtins->gl_Position.z = u->mvp_mat[2]*v.x + u->mvp_mat[6]*v.y + u->mvp_mat[10]*v.z + u->mvp_mat[14]*v.w;
    builtins->gl_Position.w = u->mvp_mat[3]*v.x + u->mvp_mat[7]*v.y + u->mvp_mat[11]*v.z + u->mvp_mat[15]*v.w;
}

void uniform_color_fp(float* fs_input, Shader_Builtins* builtins, void* uniforms) {
    My_Uniforms* u = (My_Uniforms*)uniforms;
    builtins->gl_FragColor.x = u->color.x;
    builtins->gl_FragColor.y = u->color.y;
    builtins->gl_FragColor.z = u->color.z;
    builtins->gl_FragColor.w = u->color.w;
}

void gouraud_ads_vp(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms) {
    My_Uniforms* u = (My_Uniforms*)uniforms;

    float nx = vertex_attribs[ATTR_NORMAL].x;
    float ny = vertex_attribs[ATTR_NORMAL].y;
    float nz = vertex_attribs[ATTR_NORMAL].z;

    float eye_nx = u->normal_mat[0]*nx + u->normal_mat[3]*ny + u->normal_mat[6]*nz;
    float eye_ny = u->normal_mat[1]*nx + u->normal_mat[4]*ny + u->normal_mat[7]*nz;
    float eye_nz = u->normal_mat[2]*nx + u->normal_mat[5]*ny + u->normal_mat[8]*nz;

    float len = sqrtf(eye_nx*eye_nx + eye_ny*eye_ny + eye_nz*eye_nz);
    if (len > 0.0001f) { eye_nx /= len; eye_ny /= len; eye_nz /= len; }

    vec3 light_dir = u->light_dir;
    vec3_normalize(light_dir);
    vec3 eye_dir = make_v3(0, 0, 1);

    vec3 out_light = u->Ka;

    float diff = maxf(0.0f, eye_nx*light_dir.x + eye_ny*light_dir.y + eye_nz*light_dir.z);
    out_light.x += u->Kd.x * diff;
    out_light.y += u->Kd.y * diff;
    out_light.z += u->Kd.z * diff;

    if (diff > 0) {
        vec3 r = vec3_reflect(vec3_neg(light_dir), make_v3(eye_nx, eye_ny, eye_nz));
        float spec = maxf(0.0f, r.x*eye_dir.x + r.y*eye_dir.y + r.z*eye_dir.z);
        float fSpec = powf(spec, u->shininess);
        out_light.x += u->Ks.x * fSpec;
        out_light.y += u->Ks.y * fSpec;
        out_light.z += u->Ks.z * fSpec;
    }

    vs_output[0] = out_light.x;
    vs_output[1] = out_light.y;
    vs_output[2] = out_light.z;

    vec4 v = vertex_attribs[ATTR_VERTEX];
    vec4 inst = vertex_attribs[ATTR_INSTANCE];
    builtins->gl_Position.x = u->mvp_mat[0]*(v.x+inst.x) + u->mvp_mat[4]*(v.y+inst.y) + u->mvp_mat[8]*(v.z+inst.z) + u->mvp_mat[12];
    builtins->gl_Position.y = u->mvp_mat[1]*(v.x+inst.x) + u->mvp_mat[5]*(v.y+inst.y) + u->mvp_mat[9]*(v.z+inst.z) + u->mvp_mat[13];
    builtins->gl_Position.z = u->mvp_mat[2]*(v.x+inst.x) + u->mvp_mat[6]*(v.y+inst.y) + u->mvp_mat[10]*(v.z+inst.z) + u->mvp_mat[14];
    builtins->gl_Position.w = u->mvp_mat[3]*(v.x+inst.x) + u->mvp_mat[7]*(v.y+inst.y) + u->mvp_mat[11]*(v.z+inst.z) + u->mvp_mat[15];
}

void gouraud_ads_fp(float* fs_input, Shader_Builtins* builtins, void* uniforms) {
    (void)uniforms;
    builtins->gl_FragColor.x = fs_input[0];
    builtins->gl_FragColor.y = fs_input[1];
    builtins->gl_FragColor.z = fs_input[2];
    builtins->gl_FragColor.w = 1.0f;
}

void phong_ads_vp(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms) {
    My_Uniforms* u = (My_Uniforms*)uniforms;

    float nx = vertex_attribs[ATTR_NORMAL].x;
    float ny = vertex_attribs[ATTR_NORMAL].y;
    float nz = vertex_attribs[ATTR_NORMAL].z;

    float eye_nx = u->normal_mat[0]*nx + u->normal_mat[3]*ny + u->normal_mat[6]*nz;
    float eye_ny = u->normal_mat[1]*nx + u->normal_mat[4]*ny + u->normal_mat[7]*nz;
    float eye_nz = u->normal_mat[2]*nx + u->normal_mat[5]*ny + u->normal_mat[8]*nz;

    vs_output[0] = eye_nx;
    vs_output[1] = eye_ny;
    vs_output[2] = eye_nz;

    vec4 v = vertex_attribs[ATTR_VERTEX];
    vec4 inst = vertex_attribs[ATTR_INSTANCE];
    builtins->gl_Position.x = u->mvp_mat[0]*(v.x+inst.x) + u->mvp_mat[4]*(v.y+inst.y) + u->mvp_mat[8]*(v.z+inst.z) + u->mvp_mat[12];
    builtins->gl_Position.y = u->mvp_mat[1]*(v.x+inst.x) + u->mvp_mat[5]*(v.y+inst.y) + u->mvp_mat[9]*(v.z+inst.z) + u->mvp_mat[13];
    builtins->gl_Position.z = u->mvp_mat[2]*(v.x+inst.x) + u->mvp_mat[6]*(v.y+inst.y) + u->mvp_mat[10]*(v.z+inst.z) + u->mvp_mat[14];
    builtins->gl_Position.w = u->mvp_mat[3]*(v.x+inst.x) + u->mvp_mat[7]*(v.y+inst.y) + u->mvp_mat[11]*(v.z+inst.z) + u->mvp_mat[15];
}

void phong_ads_fp(float* fs_input, Shader_Builtins* builtins, void* uniforms) {
    My_Uniforms* u = (My_Uniforms*)uniforms;

    vec3 eye_normal = make_v3(fs_input[0], fs_input[1], fs_input[2]);
    vec3_normalize(eye_normal);

    vec3 light_dir = u->light_dir;
    vec3_normalize(light_dir);
    vec3 eye_dir = make_v3(0, 0, 1);

    vec3 out_light = u->Ka;

    float lambertian = maxf(0.0f, vec3_dot(light_dir, eye_normal));
    if (lambertian > 0) {
        out_light.x += u->Kd.x * lambertian;
        out_light.y += u->Kd.y * lambertian;
        out_light.z += u->Kd.z * lambertian;

        vec3 r = vec3_reflect(vec3_neg(light_dir), eye_normal);
        float spec = maxf(0.0f, vec3_dot(r, eye_dir));
        float shine = powf(spec, u->shininess);
        out_light.x += u->Ks.x * shine;
        out_light.y += u->Ks.y * shine;
        out_light.z += u->Ks.z * shine;
    }

    builtins->gl_FragColor.x = out_light.x;
    builtins->gl_FragColor.y = out_light.y;
    builtins->gl_FragColor.z = out_light.z;
    builtins->gl_FragColor.w = 1.0f;
}
