/*
 * Red Sphere with Random Rotation
 * Using PortableGL
 */

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "portablegl/portablegl.h"

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#define sinf pgl_fast_sin
#define cosf pgl_fast_cos
#define sqrtf pgl_fast_sqrt
#define tanf pgl_fast_tan
#define acosf pgl_fast_acos
#define asinf pgl_fast_asin
#define atanf pgl_fast_atan
#define atanf2 pgl_fast_atan2

#ifndef M_PI
#define M_PI 3.14159265
#endif

#define WIDTH 640
#define HEIGHT 480

void cleanup(void);
void setup_context(void);
int handle_events(void);

SDL_Window* window;
SDL_Renderer* ren;
SDL_Texture* tex;
pix_t* bbufpix;

glContext the_context;

int polygon_mode;

typedef struct My_Uniforms
{
    mat4 mvp_mat;
    mat4 normal_mat;
    vec3 material_color;
} My_Uniforms;

static My_Uniforms uniforms;

// Sphere data
static GLuint sphere_vbo;
static GLuint sphere_ibo;
static int sphere_vertex_count;
static int sphere_index_count;

// Multiple spheres
#define NUM_SPHERES 4

typedef struct {
    float x, y;           // position
    float vx, vy;         // velocity
    float radius;         // size
    vec3 color;           // color
    float rot_x, rot_y, rot_z;  // rotation
    float rot_speed_x, rot_speed_y, rot_speed_z;  // rotation speed
} Sphere;

static Sphere spheres[NUM_SPHERES];

// Light angles for lighting direction
static float light_rot_x = 0.0f;
static float light_rot_y = 0.0f;

/** The projection matrix */
static GLfloat ProjectionMatrix[16];

static void
multiply(GLfloat *m, const GLfloat *n)
{
    GLfloat tmp[16];
    const GLfloat *row, *column;
    div_t d;
    int i, j;

    for (i = 0; i < 16; i++) {
        tmp[i] = 0;
        d = div(i, 4);
        row = n + d.quot * 4;
        column = m + d.rem;
        for (j = 0; j < 4; j++)
            tmp[i] += row[j] * column[j * 4];
    }
    memcpy(m, &tmp, sizeof tmp);
}

static void
rotate(GLfloat *m, GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    float s = sinf(angle);
    float c = cosf(angle);
    GLfloat r[16] = {
        x * x * (1 - c) + c,     y * x * (1 - c) + z * s, x * z * (1 - c) - y * s, 0,
        x * y * (1 - c) - z * s, y * y * (1 - c) + c,     y * z * (1 - c) + x * s, 0, 
        x * z * (1 - c) + y * s, y * z * (1 - c) - x * s, z * z * (1 - c) + c,     0,
        0, 0, 0, 1
    };
    multiply(m, r);
}

static void
translate(GLfloat *m, GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat t[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  x, y, z, 1 };
    multiply(m, t);
}

static void
identity(GLfloat *m)
{
    GLfloat t[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    };
    memcpy(m, t, sizeof(t));
}

static void
scale(GLfloat *m, GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat s[16] = {
        x,   0.0, 0.0, 0.0,
        0.0, y,   0.0, 0.0,
        0.0, 0.0, z,   0.0,
        0.0, 0.0, 0.0, 1.0,
    };
    multiply(m, s);
}

static void 
transpose(GLfloat *m)
{
    GLfloat t[16] = {
        m[0], m[4], m[8],  m[12],
        m[1], m[5], m[9],  m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]};
    memcpy(m, t, sizeof(t));
}

static void
invert(GLfloat *m)
{
    GLfloat t[16];
    identity(t);
    t[12] = -m[12]; t[13] = -m[13]; t[14] = -m[14];
    m[12] = m[13] = m[14] = 0;
    transpose(m);
    multiply(m, t);
}

void perspective(GLfloat *m, GLfloat fovy, GLfloat aspect, GLfloat zNear, GLfloat zFar)
{
    GLfloat tmp[16];
    identity(tmp);

    float radians = fovy / 2 * M_PI / 180;
    float deltaZ = zFar - zNear;
    float sine = sinf(radians);
    float cosine = cosf(radians);

    if ((deltaZ == 0) || (sine == 0) || (aspect == 0))
        return;

    float cotangent = cosine / sine;

    tmp[0] = cotangent / aspect;
    tmp[5] = cotangent;
    tmp[10] = -(zFar + zNear) / deltaZ;
    tmp[11] = -1;
    tmp[14] = -2 * zNear * zFar / deltaZ;
    tmp[15] = 0;

    memcpy(m, tmp, sizeof(tmp));
}

void ortho(GLfloat *m, GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar)
{
    GLfloat tmp[16];
    identity(tmp);

    tmp[0] = 2.0f / (right - left);
    tmp[5] = 2.0f / (top - bottom);
    tmp[10] = -2.0f / (zFar - zNear);
    tmp[12] = -(right + left) / (right - left);
    tmp[13] = -(top + bottom) / (top - bottom);
    tmp[14] = -(zFar + zNear) / (zFar - zNear);

    memcpy(m, tmp, sizeof(tmp));
}

// Create sphere vertices and indices
static void create_sphere(float radius, int stacks, int slices)
{
    int i, j;
    int vertex_count = (stacks + 1) * (slices + 1);
    int index_count = stacks * slices * 6;

    float* vertices = malloc(vertex_count * 6 * sizeof(float)); // pos(3) + normal(3)
    GLuint* indices = malloc(index_count * sizeof(GLuint));

    if (!vertices || !indices) {
        printf("create_sphere: malloc failed! vertices=%p, indices=%p\n", vertices, indices);
        if (vertices) free(vertices);
        if (indices) free(indices);
        return;
    }
    
    int vidx = 0;
    for (i = 0; i <= stacks; i++) {
        float phi = M_PI * i / stacks;
        float sin_phi = sinf(phi);
        float cos_phi = cosf(phi);
        
        for (j = 0; j <= slices; j++) {
            float theta = 2.0f * M_PI * j / slices;
            float sin_theta = sinf(theta);
            float cos_theta = cosf(theta);
            
            // Normal
            float nx = sin_phi * cos_theta;
            float ny = cos_phi;
            float nz = sin_phi * sin_theta;
            
            // Position = normal * radius
            vertices[vidx++] = nx * radius;
            vertices[vidx++] = ny * radius;
            vertices[vidx++] = nz * radius;
            vertices[vidx++] = nx;
            vertices[vidx++] = ny;
            vertices[vidx++] = nz;
        }
    }
    
    int iidx = 0;
    for (i = 0; i < stacks; i++) {
        for (j = 0; j < slices; j++) {
            int i0 = i * (slices + 1) + j;
            int i1 = i0 + 1;
            int i2 = (i + 1) * (slices + 1) + j;
            int i3 = i2 + 1;
            
            indices[iidx++] = i0;
            indices[iidx++] = i2;
            indices[iidx++] = i1;
            indices[iidx++] = i1;
            indices[iidx++] = i2;
            indices[iidx++] = i3;
        }
    }
    
    // Create VBO
    glGenBuffers(1, &sphere_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, sphere_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertex_count * 6 * sizeof(float), vertices, GL_STATIC_DRAW);
    
    // Create IBO
    glGenBuffers(1, &sphere_ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphere_ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_count * sizeof(GLuint), indices, GL_STATIC_DRAW);
    
    sphere_vertex_count = vertex_count;
    sphere_index_count = index_count;
    
    free(vertices);
    free(indices);
}

static void draw_sphere(Sphere* s)
{
    GLfloat model_view[16];
    GLfloat normal_matrix[16];
    GLfloat model_view_projection[16];
    GLfloat transform[16];
    
    identity(transform);
    
    // Translate to sphere position
    translate(transform, s->x, s->y, 0);
    
    // Scale by sphere radius (default sphere has radius 1)
    scale(transform, s->radius, s->radius, s->radius);
    
    // Rotate
    rotate(transform, s->rot_x, 1, 0, 0);
    rotate(transform, s->rot_y, 0, 1, 0);
    rotate(transform, s->rot_z, 0, 0, 1);
    
    memcpy(model_view, transform, sizeof(model_view));
    
    memcpy(model_view_projection, ProjectionMatrix, sizeof(model_view_projection));
    multiply(model_view_projection, model_view);
    memcpy(&uniforms.mvp_mat, model_view_projection, sizeof(mat4));
    
    memcpy(normal_matrix, model_view, sizeof(normal_matrix));
    invert(normal_matrix);
    transpose(normal_matrix);
    memcpy(&uniforms.normal_mat, normal_matrix, sizeof(mat4));
    
    // Set sphere color
    uniforms.material_color = s->color;
    
    glBindBuffer(GL_ARRAY_BUFFER, sphere_vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphere_ibo);
    
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), 0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (void*)(3 * sizeof(GLfloat)));
    
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    
    glDrawElements(GL_TRIANGLES, sphere_index_count, GL_UNSIGNED_INT, 0);
    
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(0);
}

static void sphere_draw(void)
{
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Draw all spheres
    for (int i = 0; i < NUM_SPHERES; i++) {
        draw_sphere(&spheres[i]);
    }
}

static void sphere_idle(void)
{
    static int frames = 0;
    static double tRot0 = -1.0, tRate0 = -1.0;
    double dt, t = SDL_GetTicks() / 1000.0;
    
    if (tRot0 < 0.0)
        tRot0 = t;
    dt = t - tRot0;
    tRot0 = t;
    
    // Get window bounds
    int window_w, window_h;
    SDL_GetWindowSize(window, &window_w, &window_h);
    float aspect = window_w / (float)window_h;
    float world_y_max = 1.0f;
    float world_x_max = world_y_max * aspect;
    
    // Update all spheres
    for (int i = 0; i < NUM_SPHERES; i++) {
        Sphere* s = &spheres[i];
        
        // Update rotation
        s->rot_x += s->rot_speed_x * dt;
        s->rot_y += s->rot_speed_y * dt;
        s->rot_z += s->rot_speed_z * dt;
        
        // Keep angles in [0, 2*PI]
        while (s->rot_x > 2 * M_PI) s->rot_x -= 2 * M_PI;
        while (s->rot_y > 2 * M_PI) s->rot_y -= 2 * M_PI;
        while (s->rot_z > 2 * M_PI) s->rot_z -= 2 * M_PI;
        
        // Update position
        s->x += s->vx * dt;
        s->y += s->vy * dt;
        
        // Bounce off edges
        int bounced = 0;
        
        // Left/Right bounce
        if (s->x - s->radius < -world_x_max) {
            s->x = -world_x_max + s->radius;
            s->vx = -s->vx;
            bounced = 1;
        } else if (s->x + s->radius > world_x_max) {
            s->x = world_x_max - s->radius;
            s->vx = -s->vx;
            bounced = 1;
        }
        
        // Top/Bottom bounce
        if (s->y - s->radius < -world_y_max) {
            s->y = -world_y_max + s->radius;
            s->vy = -s->vy;
            bounced = 1;
        } else if (s->y + s->radius > world_y_max) {
            s->y = world_y_max - s->radius;
            s->vy = -s->vy;
            bounced = 1;
        }
        
        // Change to random color on bounce
        if (bounced) {
            s->color.x = (rand() % 256) / 255.0f;
            s->color.y = (rand() % 256) / 255.0f;
            s->color.z = (rand() % 256) / 255.0f;
        }
    }
    
    // Sphere-to-sphere collision detection
    for (int i = 0; i < NUM_SPHERES; i++) {
        for (int j = i + 1; j < NUM_SPHERES; j++) {
            Sphere* s1 = &spheres[i];
            Sphere* s2 = &spheres[j];
            
            // Calculate distance between sphere centers
            float dx = s2->x - s1->x;
            float dy = s2->y - s1->y;
            float distance = sqrtf(dx * dx + dy * dy);
            float min_distance = s1->radius + s2->radius;
            
            // Check if spheres are colliding
            if (distance < min_distance && distance > 0.001f) {
                // Calculate collision normal
                float nx = dx / distance;
                float ny = dy / distance;
                
                // Separate spheres to prevent overlap
                float overlap = min_distance - distance;
                float separation_x = nx * overlap * 0.5f;
                float separation_y = ny * overlap * 0.5f;
                s1->x -= separation_x;
                s1->y -= separation_y;
                s2->x += separation_x;
                s2->y += separation_y;
                
                // Calculate relative velocity
                float rvx = s2->vx - s1->vx;
                float rvy = s2->vy - s1->vy;
                
                // Calculate relative velocity along collision normal
                float vel_along_normal = rvx * nx + rvy * ny;
                
                // Only resolve if spheres are moving towards each other
                if (vel_along_normal < 0) {
                    // Elastic collision response
                    float restitution = 1.0f; // Perfectly elastic
                    float impulse = -(1.0f + restitution) * vel_along_normal;
                    impulse /= 2.0f; // Equal mass spheres
                    
                    // Apply impulse to both spheres
                    float impulse_x = impulse * nx;
                    float impulse_y = impulse * ny;
                    
                    s1->vx -= impulse_x;
                    s1->vy -= impulse_y;
                    s2->vx += impulse_x;
                    s2->vy += impulse_y;
                    
                    // Change colors on collision
                    s1->color.x = (rand() % 256) / 255.0f;
                    s1->color.y = (rand() % 256) / 255.0f;
                    s1->color.z = (rand() % 256) / 255.0f;
                    s2->color.x = (rand() % 256) / 255.0f;
                    s2->color.y = (rand() % 256) / 255.0f;
                    s2->color.z = (rand() % 256) / 255.0f;
                }
            }
        }
    }
    
    // Continuous light movement
    light_rot_x += dt * 0.5f;
    light_rot_y += dt * 0.3f;
    
    frames++;
    
    if (tRate0 < 0.0)
        tRate0 = t;
    if (t - tRate0 >= 5.0) {
        GLfloat seconds = t - tRate0;
        GLfloat fps = frames / seconds;
        printf("%d frames in %3.1f seconds = %6.3f FPS\n", frames, seconds, fps);
        tRate0 = t;
        frames = 0;
    }
}

void vertex_shader(float* vs_output, vec4* v_attrs, Shader_Builtins* builtins, void* uniforms)
{
    vec3* vs_out = (vec3*)vs_output;
    My_Uniforms* u = uniforms;
    
    vec4 v4 = mult_m4_v4(u->normal_mat, v_attrs[1]);
    vec3 v3 = { v4.x, v4.y, v4.z };
    vec3 N = norm_v3(v3);
    
    // Calculate dynamic light position based on light_rot_x and light_rot_y
    float radius = 10.0f;
    vec3 light_pos;
    light_pos.x = radius * cosf(light_rot_y) * cosf(light_rot_x);
    light_pos.y = radius * sinf(light_rot_x);
    light_pos.z = radius * sinf(light_rot_y) * cosf(light_rot_x);
    
    vec3 L = norm_v3(light_pos);
    
    float tmp = dot_v3s(N, L);
    float diff_intensity = MAX(tmp, 0.0);
    
    // Add ambient light (0.3) so the dark side isn't completely black
    float ambient = 0.3f;
    float total_intensity = diff_intensity + ambient;
    if (total_intensity > 1.0f) total_intensity = 1.0f;
    
    // Use material color from uniforms
    vs_out[0] = scale_v3(u->material_color, total_intensity);
    
    builtins->gl_Position = mult_m4_v4(u->mvp_mat, v_attrs[0]);
}

void fragment_shader(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    vec3 color = ((vec3*)fs_input)[0];
    
    // ARGB format - no swap needed
    builtins->gl_FragColor.x = color.x;
    builtins->gl_FragColor.y = color.y;
    builtins->gl_FragColor.z = color.z;
    builtins->gl_FragColor.w = 1;
}

static void sphere_init(void)
{
    GLuint program;
    
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    
    GLenum smooth[3] = { PGL_SMOOTH3 };
    
    program = pglCreateProgram(vertex_shader, fragment_shader, 3, smooth, GL_FALSE);
    if (program == 0) {
        printf("sphere_init: pglCreateProgram failed!\n");
        return;
    }
    glUseProgram(program);
    pglSetUniform(&uniforms);
    
    // Get actual window size and use it consistently
    int actual_width, actual_height;
    SDL_GetWindowSize(window, &actual_width, &actual_height);
    float aspect = actual_width / (float)actual_height;
    
    // Use orthographic projection to avoid perspective distortion
    // View volume: x in [-aspect, aspect], y in [-1, 1], z in [-10, 10]
    ortho(ProjectionMatrix, -aspect, aspect, -1.0f, 1.0f, -10.0f, 10.0f);
    glViewport(0, 0, (GLint) actual_width, (GLint) actual_height);
    
    // Create unit sphere (radius will be applied via scaling)
    create_sphere(1.0f, 32, 32);
    
    // Initialize all spheres with random properties
    srand(SDL_GetTicks());
    
    for (int i = 0; i < NUM_SPHERES; i++) {
        Sphere* s = &spheres[i];
        
        // Random radius between 0.1 and 0.4
        s->radius = 0.1f + (rand() % 30) / 100.0f;
        
        // Random position
        s->x = ((rand() % 200) - 100) / 100.0f * aspect * 0.5f;
        s->y = ((rand() % 200) - 100) / 100.0f * 0.5f;
        
        // Random velocity
        s->vx = ((rand() % 100) - 50) / 50.0f * 1.5f;
        s->vy = ((rand() % 100) - 50) / 50.0f * 1.2f;
        
        // Random color
        s->color.x = (rand() % 256) / 255.0f;
        s->color.y = (rand() % 256) / 255.0f;
        s->color.z = (rand() % 256) / 255.0f;
        
        // Random rotation
        s->rot_x = 0.0f;
        s->rot_y = 0.0f;
        s->rot_z = 0.0f;
        
        // Random rotation speed
        s->rot_speed_x = (rand() % 100 - 50) / 20.0f;
        s->rot_speed_y = (rand() % 100 - 50) / 20.0f;
        s->rot_speed_z = (rand() % 100 - 50) / 20.0f;
    }
    
    printf("Initialized %d spheres with random sizes and colors\n", NUM_SPHERES);
}

void cleanup(void)
{
    free_glContext(&the_context);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void setup_context(void)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO)) {
        printf("SDL_init error: %s\n", SDL_GetError());
        exit(0);
    }
    
    window = SDL_CreateWindow("Red Sphere", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              WIDTH, HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        printf("Failed to create window\n");
        SDL_Quit();
        exit(0);
    }
    
    ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    
    // Get actual window size and use it consistently
    int actual_width, actual_height;
    SDL_GetWindowSize(window, &actual_width, &actual_height);
    
    // Disable logical size to prevent scaling distortion
    SDL_RenderSetLogicalSize(ren, 0, 0);
    
    // Reset render scale to 1:1
    SDL_RenderSetScale(ren, 1.0f, 1.0f);
    
    // Set scale quality to nearest pixel for sharp edges
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, actual_width, actual_height);
    
    if (!init_glContext(&the_context, &bbufpix, actual_width, actual_height)) {
        puts("Failed to initialize glContext");
        exit(0);
    }
}

int handle_events(void)
{
    SDL_Event e;
    int sc;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            return 1;
        } else if (e.type == SDL_KEYDOWN) {
            sc = e.key.keysym.sym;
            
            switch (sc) {
            case SDLK_ESCAPE:
                return 1;
            case SDLK_p:
                polygon_mode = (polygon_mode + 1) % 3;
                if (polygon_mode == 0)
                    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
                else if (polygon_mode == 1)
                    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                else
                    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                break;
            case SDLK_r:
                // Randomize all spheres
                for (int i = 0; i < NUM_SPHERES; i++) {
                    Sphere* s = &spheres[i];
                    s->radius = 0.1f + (rand() % 30) / 100.0f;
                    s->color.x = (rand() % 256) / 255.0f;
                    s->color.y = (rand() % 256) / 255.0f;
                    s->color.z = (rand() % 256) / 255.0f;
                    s->rot_speed_x = (rand() % 100 - 50) / 20.0f;
                    s->rot_speed_y = (rand() % 100 - 50) / 20.0f;
                    s->rot_speed_z = (rand() % 100 - 50) / 20.0f;
                }
                printf("Randomized %d spheres\n", NUM_SPHERES);
                break;
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    setup_context();
    polygon_mode = 2;
    
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    
    sphere_init();
    
    // Get actual window size for rendering
    int render_width, render_height;
    SDL_GetWindowSize(window, &render_width, &render_height);
    
    while (1) {
        if (handle_events())
            break;
        
        sphere_idle();
        sphere_draw();
        
        SDL_UpdateTexture(tex, NULL, bbufpix, render_width * sizeof(pix_t));
        
        // Render texture to full window without scaling
        SDL_Rect dest_rect = {0, 0, render_width, render_height};
        SDL_RenderCopy(ren, tex, NULL, &dest_rect);
        SDL_RenderPresent(ren);
    }
    
    cleanup();
    return 0;
}
