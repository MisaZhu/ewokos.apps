#define PGLDEF
#include "../include/portablegl/portablegl.h"

// Fast math function implementations
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif


// Fast sine approximation using lookup table with linear interpolation
#define SIN_TABLE_SIZE 1024
static float sin_table[SIN_TABLE_SIZE];
static int sin_table_initialized = 0;

static void init_sin_table(void) {
    if (sin_table_initialized) return;
    for (int i = 0; i < SIN_TABLE_SIZE; i++) {
        float x = (float)i / SIN_TABLE_SIZE * 2.0f * M_PI;
        sin_table[i] = (float)sin(x);
    }
    sin_table_initialized = 1;
}

inline float pgl_fast_sin(float x) {
    if (!sin_table_initialized) init_sin_table();
    
    // Normalize to [0, 2*PI)
    float x_norm = x / (2.0f * M_PI);
    x_norm = x_norm - (int)x_norm;
    if (x_norm < 0) x_norm += 1.0f;
    
    // Lookup with linear interpolation
    float idx = x_norm * SIN_TABLE_SIZE;
    int i0 = (int)idx;
    int i1 = (i0 + 1) & (SIN_TABLE_SIZE - 1);
    float t = idx - i0;
    
    return sin_table[i0] * (1.0f - t) + sin_table[i1] * t;
}

// Fast cosine using lookup table with linear interpolation
#define COS_TABLE_SIZE 1024
static float cos_table[COS_TABLE_SIZE];
static int cos_table_initialized = 0;

static void init_cos_table(void) {
    if (cos_table_initialized) return;
    for (int i = 0; i < COS_TABLE_SIZE; i++) {
        float x = (float)i / COS_TABLE_SIZE * 2.0f * M_PI;
        cos_table[i] = (float)cos(x);
    }
    cos_table_initialized = 1;
}

inline float pgl_fast_cos(float x) {
    if (!cos_table_initialized) init_cos_table();
    
    // Normalize to [0, 2*PI)
    float x_norm = x / (2.0f * M_PI);
    x_norm = x_norm - (int)x_norm;
    if (x_norm < 0) x_norm += 1.0f;
    
    // Lookup with linear interpolation
    float idx = x_norm * COS_TABLE_SIZE;
    int i0 = (int)idx;
    int i1 = (i0 + 1) & (COS_TABLE_SIZE - 1);
    float t = idx - i0;
    
    return cos_table[i0] * (1.0f - t) + cos_table[i1] * t;
}

// Fast sqrt using Quake III Arena method with Newton iterations for better precision
inline float pgl_fast_sqrt(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float f; int i; } u;
    u.f = x;
    u.i = 0x5f3759df - (u.i >> 1);
    float x2 = x * 0.5f;
    float y = u.f;
    // First Newton iteration
    y = y * (1.5f - (x2 * y * y));
    // Second iteration for better precision
    //y = y * (1.5f - (x2 * y * y));
    // Third iteration for maximum precision
    //y = y * (1.5f - (x2 * y * y));
    return y * x;
}

// Fast tan using sin/cos ratio
inline float pgl_fast_tan(float x) {
    float sx = pgl_fast_sin(x);
    float cx = pgl_fast_cos(x);
    if (cx == 0.0f) return 0.0f;
    return sx / cx;
}

// Fast acos approximation using lookup table with linear interpolation
// Valid for [-1, 1], returns [0, PI]
#define ACOS_TABLE_SIZE 1024
static float acos_table[ACOS_TABLE_SIZE + 1];
static int acos_table_initialized = 0;

static void init_acos_table(void) {
    if (acos_table_initialized) return;
    for (int i = 0; i <= ACOS_TABLE_SIZE; i++) {
        float x = (float)i / ACOS_TABLE_SIZE * 2.0f - 1.0f; // Map [0, 1024] to [-1, 1]
        acos_table[i] = (float)acos(x);
    }
    acos_table_initialized = 1;
}

inline float pgl_fast_acos(float x) {
    if (!acos_table_initialized) init_acos_table();
    
    // Clamp input to [-1, 1]
    if (x < -1.0f) x = -1.0f;
    if (x > 1.0f) x = 1.0f;
    
    // Map [-1, 1] to [0, ACOS_TABLE_SIZE]
    float idx = (x + 1.0f) * 0.5f * ACOS_TABLE_SIZE;
    int i0 = (int)idx;
    int i1 = i0 + 1;
    if (i1 > ACOS_TABLE_SIZE) i1 = ACOS_TABLE_SIZE;
    float t = idx - i0;
    
    return acos_table[i0] * (1.0f - t) + acos_table[i1] * t;
}

// Fast asin using acos: asin(x) = PI/2 - acos(x)
inline float pgl_fast_asin(float x) {
    return M_PI / 2.0f - pgl_fast_acos(x);
}

// Fast atan approximation using lookup table with linear interpolation
// Valid for all real numbers, returns [-PI/2, PI/2]
#define ATAN_TABLE_SIZE 1024
static float atan_table[ATAN_TABLE_SIZE + 1];
static int atan_table_initialized = 0;

static void init_atan_table(void) {
    if (atan_table_initialized) return;
    for (int i = 0; i <= ATAN_TABLE_SIZE; i++) {
        // Map [0, 1024] to [-8, 8] for good coverage
        float x = ((float)i / ATAN_TABLE_SIZE * 16.0f) - 8.0f;
        atan_table[i] = (float)atan(x);
    }
    atan_table_initialized = 1;
}

inline float pgl_fast_atan(float x) {
    if (!atan_table_initialized) init_atan_table();
    
    // Handle special cases
    if (x < -8.0f) return -M_PI / 2.0f;
    if (x > 8.0f) return M_PI / 2.0f;
    
    // Map [-8, 8] to [0, ATAN_TABLE_SIZE]
    float idx = (x + 8.0f) / 16.0f * ATAN_TABLE_SIZE;
    int i0 = (int)idx;
    int i1 = i0 + 1;
    if (i1 > ATAN_TABLE_SIZE) i1 = ATAN_TABLE_SIZE;
    float t = idx - i0;
    
    return atan_table[i0] * (1.0f - t) + atan_table[i1] * t;
}

// Fast atan2 using atan: atan2(y, x) = atan(y/x) with quadrant correction
inline float pgl_fast_atan2(float y, float x) {
    if (x == 0.0f) {
        if (y > 0.0f) return M_PI / 2.0f;
        if (y < 0.0f) return -M_PI / 2.0f;
        return 0.0f;
    }
    
    float atan_val = pgl_fast_atan(y / x);
    
    // Correct quadrant
    if (x < 0.0f) {
        if (y >= 0.0f) return atan_val + M_PI;
        else return atan_val - M_PI;
    }
    
    return atan_val;
}

// Use fast math functions
#define sinf pgl_fast_sin
#define cosf pgl_fast_cos
#define sqrtf pgl_fast_sqrt
#define tanf pgl_fast_tan
#define acosf pgl_fast_acos
#define asinf pgl_fast_asin
#define atanf pgl_fast_atan
#define atan2f pgl_fast_atan2

// Matrix access macros for extract_rotation_m4
#ifndef ROW_MAJOR
#define M44(m, row, col) m[col*4 + row]
#define M33(m, row, col) m[col*3 + row]
#else
#define M44(m, row, col) m[row*4 + col]
#define M33(m, row, col) m[row*3 + col]
#endif

// Runtime configurable vertex limits
static int _pgl_max_vertices = PGL_DEF_MAX_VERTICES;

// Function to set max vertices before init_glContext()
void pgl_set_max_vertices(int max_vertices)
{
    if (max_vertices > 0) {
        _pgl_max_vertices = max_vertices;
    }
}

// Helper function to get actual max vertices
static int get_max_vertices(void)
{
    if (_pgl_max_vertices > 0) {
        return _pgl_max_vertices;
    }
    return PGL_DEF_MAX_VERTICES;
}

// Define PGL_NEON_ENABLED early so texture conversion functions can use it
#ifdef ARCH_BOOST
#if defined(__aarch64__) || defined(__ARM_NEON) || defined(ARCH_ARM)
#include <arm_neon.h>
#define PGL_NEON_ENABLED 1

// Unified ARM NEON optimized implementation for both AARCH64 and ARM 32-bit
// AARCH64 can use wider registers and more parallelism, but the code is compatible

static inline void pgl_neon_fill_line(uint32_t* dst, uint32_t color, int pixels)
{
    uint32x4_t color_vec = vmovq_n_u32(color);
    int i = 0;
#if defined(__aarch64__)
    // AARCH64 can process 16 pixels at once (4 x 128-bit registers)
    for (; i + 16 <= pixels; i += 16) {
        vst1q_u32(dst + i, color_vec);
        vst1q_u32(dst + i + 4, color_vec);
        vst1q_u32(dst + i + 8, color_vec);
        vst1q_u32(dst + i + 12, color_vec);
    }
#else
    // ARM 32-bit processes 8 pixels at once
    for (; i + 8 <= pixels; i += 8) {
        vst1q_u32(dst + i, color_vec);
        vst1q_u32(dst + i + 4, color_vec);
    }
#endif
    // Handle remaining pixels
    for (; i < pixels; i++) {
        dst[i] = color;
    }
}

static inline void pgl_neon_copy_line(uint32_t* dst, uint32_t* src, int pixels)
{
    int i = 0;
#if defined(__aarch64__)
    for (; i + 16 <= pixels; i += 16) {
        uint32x4_t s0 = vld1q_u32(src + i);
        uint32x4_t s1 = vld1q_u32(src + i + 4);
        uint32x4_t s2 = vld1q_u32(src + i + 8);
        uint32x4_t s3 = vld1q_u32(src + i + 12);
        vst1q_u32(dst + i, s0);
        vst1q_u32(dst + i + 4, s1);
        vst1q_u32(dst + i + 8, s2);
        vst1q_u32(dst + i + 12, s3);
    }
#else
    for (; i + 8 <= pixels; i += 8) {
        uint32x4_t s0 = vld1q_u32(src + i);
        uint32x4_t s1 = vld1q_u32(src + i + 4);
        vst1q_u32(dst + i, s0);
        vst1q_u32(dst + i + 4, s1);
    }
#endif
    for (; i < pixels; i++) {
        dst[i] = src[i];
    }
}

static inline void pgl_neon_fill_rect(uint32_t* dst, int stride, uint32_t color, int w, int h)
{
    uint32x4_t color_vec = vmovq_n_u32(color);
    for (int y = 0; y < h; y++) {
        int i = 0;
        uint32_t* row = dst + y * stride;
#if defined(__aarch64__)
        for (; i + 16 <= w; i += 16) {
            vst1q_u32(row + i, color_vec);
            vst1q_u32(row + i + 4, color_vec);
            vst1q_u32(row + i + 8, color_vec);
            vst1q_u32(row + i + 12, color_vec);
        }
#else
        for (; i + 8 <= w; i += 8) {
            vst1q_u32(row + i, color_vec);
            vst1q_u32(row + i + 4, color_vec);
        }
#endif
        for (; i < w; i++) {
            row[i] = color;
        }
    }
}

// Simplified NEON alpha blending - uses scalar calculation
static inline void pgl_neon_blend_pixel_line(uint32_t* dst, uint32_t src_color, int pixels)
{
    uint8_t src_a = (src_color >> 24) & 0xFF;
    uint8_t src_r = (src_color >> 16) & 0xFF;
    uint8_t src_g = (src_color >> 8) & 0xFF;
    uint8_t src_b = src_color & 0xFF;
    uint8_t inv_src_a = 255 - src_a;
    
    // If source alpha is 255, just fill
    if (src_a == 255) {
        pgl_neon_fill_line(dst, src_color, pixels);
        return;
    }
    
    // If source alpha is 0, do nothing
    if (src_a == 0) {
        return;
    }
    
    // Process pixels
    int i = 0;
    for (; i < pixels; i++) {
        uint32_t d = dst[i];
        uint8_t da = (d >> 24) & 0xFF;
        uint8_t dr = (d >> 16) & 0xFF;
        uint8_t dg = (d >> 8) & 0xFF;
        uint8_t db = d & 0xFF;
        
        uint8_t r = (src_a * src_r + inv_src_a * dr) >> 8;
        uint8_t g = (src_a * src_g + inv_src_a * dg) >> 8;
        uint8_t b = (src_a * src_b + inv_src_a * db) >> 8;
        uint8_t a = src_a + ((inv_src_a * da) >> 8);
        
        dst[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
}

// Matrix multiplication NEON optimization - efficient version
static inline void pgl_neon_mult_m4_m4(float* c, const float* a, const float* b)
{
#ifndef ROW_MAJOR
    // Column Major: C = A * B
    // C[row][col] = dot(A[row], B[col])
    // A[row] = {a[row], a[row+4], a[row+8], a[row+12]} in column-major storage

    // Load B columns once
    float32x4_t b_col0 = vld1q_f32(b);
    float32x4_t b_col1 = vld1q_f32(b + 4);
    float32x4_t b_col2 = vld1q_f32(b + 8);
    float32x4_t b_col3 = vld1q_f32(b + 12);

    // Load A rows (each row is spread across columns in column-major)
    float32x4_t a_row0 = vld1q_f32(a);
    float32x4_t a_row1 = vld1q_f32(a + 4);
    float32x4_t a_row2 = vld1q_f32(a + 8);
    float32x4_t a_row3 = vld1q_f32(a + 12);

    // Compute column 0: C[0..3][0] = A * b_col0
    float32x4_t c_col0 = vmulq_f32(vdupq_n_f32(vgetq_lane_f32(b_col0, 0)), a_row0);
    c_col0 = vmlaq_f32(c_col0, vdupq_n_f32(vgetq_lane_f32(b_col0, 1)), a_row1);
    c_col0 = vmlaq_f32(c_col0, vdupq_n_f32(vgetq_lane_f32(b_col0, 2)), a_row2);
    c_col0 = vmlaq_f32(c_col0, vdupq_n_f32(vgetq_lane_f32(b_col0, 3)), a_row3);
    vst1q_f32(c, c_col0);

    // Compute column 1: C[0..3][1] = A * b_col1
    float32x4_t c_col1 = vmulq_f32(vdupq_n_f32(vgetq_lane_f32(b_col1, 0)), a_row0);
    c_col1 = vmlaq_f32(c_col1, vdupq_n_f32(vgetq_lane_f32(b_col1, 1)), a_row1);
    c_col1 = vmlaq_f32(c_col1, vdupq_n_f32(vgetq_lane_f32(b_col1, 2)), a_row2);
    c_col1 = vmlaq_f32(c_col1, vdupq_n_f32(vgetq_lane_f32(b_col1, 3)), a_row3);
    vst1q_f32(c + 4, c_col1);

    // Compute column 2: C[0..3][2] = A * b_col2
    float32x4_t c_col2 = vmulq_f32(vdupq_n_f32(vgetq_lane_f32(b_col2, 0)), a_row0);
    c_col2 = vmlaq_f32(c_col2, vdupq_n_f32(vgetq_lane_f32(b_col2, 1)), a_row1);
    c_col2 = vmlaq_f32(c_col2, vdupq_n_f32(vgetq_lane_f32(b_col2, 2)), a_row2);
    c_col2 = vmlaq_f32(c_col2, vdupq_n_f32(vgetq_lane_f32(b_col2, 3)), a_row3);
    vst1q_f32(c + 8, c_col2);

    // Compute column 3: C[0..3][3] = A * b_col3
    float32x4_t c_col3 = vmulq_f32(vdupq_n_f32(vgetq_lane_f32(b_col3, 0)), a_row0);
    c_col3 = vmlaq_f32(c_col3, vdupq_n_f32(vgetq_lane_f32(b_col3, 1)), a_row1);
    c_col3 = vmlaq_f32(c_col3, vdupq_n_f32(vgetq_lane_f32(b_col3, 2)), a_row2);
    c_col3 = vmlaq_f32(c_col3, vdupq_n_f32(vgetq_lane_f32(b_col3, 3)), a_row3);
    vst1q_f32(c + 12, c_col3);
#else
    // Row-major: C = A * B
    // C[row][col] = dot(A[row], B[col])
    // A[row] = {a[row*4], a[row*4+1], a[row*4+2], a[row*4+3]}

    // Load A rows
    float32x4_t a_row0 = vld1q_f32(a);
    float32x4_t a_row1 = vld1q_f32(a + 4);
    float32x4_t a_row2 = vld1q_f32(a + 8);
    float32x4_t a_row3 = vld1q_f32(a + 12);

    // Load B columns
    float32x4_t b_col0 = vld1q_f32(b);
    float32x4_t b_col1 = vld1q_f32(b + 4);
    float32x4_t b_col2 = vld1q_f32(b + 8);
    float32x4_t b_col3 = vld1q_f32(b + 12);

    // Compute row 0: C[0][0..3] = dot(A[0], B[j])
    float32x4_t c_row0 = vmulq_f32(vdupq_n_f32(vgetq_lane_f32(a_row0, 0)), b_col0);
    c_row0 = vmlaq_f32(c_row0, vdupq_n_f32(vgetq_lane_f32(a_row0, 1)), b_col1);
    c_row0 = vmlaq_f32(c_row0, vdupq_n_f32(vgetq_lane_f32(a_row0, 2)), b_col2);
    c_row0 = vmlaq_f32(c_row0, vdupq_n_f32(vgetq_lane_f32(a_row0, 3)), b_col3);
    vst1q_f32(c, c_row0);

    // Compute row 1
    float32x4_t c_row1 = vmulq_f32(vdupq_n_f32(vgetq_lane_f32(a_row1, 0)), b_col0);
    c_row1 = vmlaq_f32(c_row1, vdupq_n_f32(vgetq_lane_f32(a_row1, 1)), b_col1);
    c_row1 = vmlaq_f32(c_row1, vdupq_n_f32(vgetq_lane_f32(a_row1, 2)), b_col2);
    c_row1 = vmlaq_f32(c_row1, vdupq_n_f32(vgetq_lane_f32(a_row1, 3)), b_col3);
    vst1q_f32(c + 4, c_row1);

    // Compute row 2
    float32x4_t c_row2 = vmulq_f32(vdupq_n_f32(vgetq_lane_f32(a_row2, 0)), b_col0);
    c_row2 = vmlaq_f32(c_row2, vdupq_n_f32(vgetq_lane_f32(a_row2, 1)), b_col1);
    c_row2 = vmlaq_f32(c_row2, vdupq_n_f32(vgetq_lane_f32(a_row2, 2)), b_col2);
    c_row2 = vmlaq_f32(c_row2, vdupq_n_f32(vgetq_lane_f32(a_row2, 3)), b_col3);
    vst1q_f32(c + 8, c_row2);

    // Compute row 3
    float32x4_t c_row3 = vmulq_f32(vdupq_n_f32(vgetq_lane_f32(a_row3, 0)), b_col0);
    c_row3 = vmlaq_f32(c_row3, vdupq_n_f32(vgetq_lane_f32(a_row3, 1)), b_col1);
    c_row3 = vmlaq_f32(c_row3, vdupq_n_f32(vgetq_lane_f32(a_row3, 2)), b_col2);
    c_row3 = vmlaq_f32(c_row3, vdupq_n_f32(vgetq_lane_f32(a_row3, 3)), b_col3);
    vst1q_f32(c + 12, c_row3);
#endif
}

// Matrix 2x2 multiplication NEON optimization
static inline void pgl_neon_mult_m2_m2(float* c, const float* a, const float* b)
{
#ifndef ROW_MAJOR
    // Column Major: C = A * B
    // In column major 2x2:
    // a[0], a[1] = A的第0列 (A[0][0], A[1][0])
    // a[2], a[3] = A的第1列 (A[0][1], A[1][1])
    // 
    // C[row][col] = dot(A的第row行, B的第col列)
    // 
    // C[0][0] = A[0][0]*B[0][0] + A[0][1]*B[1][0] = a[0]*b[0] + a[2]*b[1] -> c[0]
    // C[1][0] = A[1][0]*B[0][0] + A[1][1]*B[1][0] = a[1]*b[0] + a[3]*b[1] -> c[1]
    // C[0][1] = A[0][0]*B[0][1] + A[0][1]*B[1][1] = a[0]*b[2] + a[2]*b[3] -> c[2]
    // C[1][1] = A[1][0]*B[0][1] + A[1][1]*B[1][1] = a[1]*b[2] + a[3]*b[3] -> c[3]
    //
    // C的存储: c[0],c[1]=第0列, c[2],c[3]=第1列
    
    // Load B columns
    float32x4_t b_col0 = {b[0], b[1], 0, 0};  // B第0列
    float32x4_t b_col1 = {b[2], b[3], 0, 0};  // B第1列
    
    // For each column of C
    for (int col = 0; col < 2; col++) {
        float32x4_t b_col = (col == 0) ? b_col0 : b_col1;
        
        // Compute C[0][col] and C[1][col]
        // For row 0: A[0] = {a[0], a[2]} (A的第0行)
        float32x4_t a_row0 = {a[0], a[2], 0, 0};
        float32x4_t prod0 = vmulq_f32(a_row0, b_col);
        float32x2_t sum0_low = vpadd_f32(vget_low_f32(prod0), vget_high_f32(prod0));
        float32x2_t sum0 = vpadd_f32(sum0_low, sum0_low);
        
        // For row 1: A[1] = {a[1], a[3]} (A的第1行)
        float32x4_t a_row1 = {a[1], a[3], 0, 0};
        float32x4_t prod1 = vmulq_f32(a_row1, b_col);
        float32x2_t sum1_low = vpadd_f32(vget_low_f32(prod1), vget_high_f32(prod1));
        float32x2_t sum1 = vpadd_f32(sum1_low, sum1_low);
        
        // Store to c[col*2] and c[col*2+1]
        c[col * 2] = vget_lane_f32(sum0, 0);
        c[col * 2 + 1] = vget_lane_f32(sum1, 0);
    }
#else
    // Row-major: C = A * B
    // In row-major 2x2:
    // a[0], a[1] = A的第0行 (A[0][0], A[0][1])
    // a[2], a[3] = A的第1行 (A[1][0], A[1][1])
    // 
    // C[row][col] = dot(A的第row行, B的第col列)
    // 
    // C[0][0] = A[0][0]*B[0][0] + A[0][1]*B[1][0] = a[0]*b[0] + a[1]*b[2] -> c[0]
    // C[0][1] = A[0][0]*B[0][1] + A[0][1]*B[1][1] = a[0]*b[1] + a[1]*b[3] -> c[1]
    // C[1][0] = A[1][0]*B[0][0] + A[1][1]*B[1][0] = a[2]*b[0] + a[3]*b[2] -> c[2]
    // C[1][1] = A[1][0]*B[0][1] + A[1][1]*B[1][1] = a[2]*b[1] + a[3]*b[3] -> c[3]
    //
    // C的存储: c[0],c[1]=第0行, c[2],c[3]=第1行
    
    // Load A rows
    float32x4_t a_row0 = {a[0], a[1], 0, 0};  // A第0行
    float32x4_t a_row1 = {a[2], a[3], 0, 0};  // A第1行
    
    // Load B columns (transposed access from row-major storage)
    float32x4_t b_col0 = {b[0], b[2], 0, 0};  // B第0列
    float32x4_t b_col1 = {b[1], b[3], 0, 0};  // B第1列
    
    // Compute C[0][0] and C[0][1] (first row of C)
    float32x4_t prod0_0 = vmulq_f32(a_row0, b_col0);
    float32x2_t sum00_low = vpadd_f32(vget_low_f32(prod0_0), vget_high_f32(prod0_0));
    float32x2_t sum00 = vpadd_f32(sum00_low, sum00_low);
    
    float32x4_t prod0_1 = vmulq_f32(a_row0, b_col1);
    float32x2_t sum01_low = vpadd_f32(vget_low_f32(prod0_1), vget_high_f32(prod0_1));
    float32x2_t sum01 = vpadd_f32(sum01_low, sum01_low);
    
    c[0] = vget_lane_f32(sum00, 0);
    c[1] = vget_lane_f32(sum01, 0);
    
    // Compute C[1][0] and C[1][1] (second row of C)
    float32x4_t prod1_0 = vmulq_f32(a_row1, b_col0);
    float32x2_t sum10_low = vpadd_f32(vget_low_f32(prod1_0), vget_high_f32(prod1_0));
    float32x2_t sum10 = vpadd_f32(sum10_low, sum10_low);
    
    float32x4_t prod1_1 = vmulq_f32(a_row1, b_col1);
    float32x2_t sum11_low = vpadd_f32(vget_low_f32(prod1_1), vget_high_f32(prod1_1));
    float32x2_t sum11 = vpadd_f32(sum11_low, sum11_low);
    
    c[2] = vget_lane_f32(sum10, 0);
    c[3] = vget_lane_f32(sum11, 0);
#endif
}

// Matrix-vector multiplication NEON optimizations
static inline void pgl_neon_mult_m4_v4(vec4* r, const float* m, const vec4 v)
{
#ifndef ROW_MAJOR
    float32x4_t vx = vdupq_n_f32(v.x);
    float32x4_t vy = vdupq_n_f32(v.y);
    float32x4_t vz = vdupq_n_f32(v.z);
    float32x4_t vw = vdupq_n_f32(v.w);

    float32x4_t c0 = vld1q_f32(m);
    float32x4_t c1 = vld1q_f32(m + 4);
    float32x4_t c2 = vld1q_f32(m + 8);
    float32x4_t c3 = vld1q_f32(m + 12);

    float32x4_t res = vmulq_f32(vx, c0);
    res = vmlaq_f32(res, vy, c1);
    res = vmlaq_f32(res, vz, c2);
    res = vmlaq_f32(res, vw, c3);

    r->x = vgetq_lane_f32(res, 0);
    r->y = vgetq_lane_f32(res, 1);
    r->z = vgetq_lane_f32(res, 2);
    r->w = vgetq_lane_f32(res, 3);
#else
    // Row-major: r = M * v
    // r.x = dot(M的第0行, v) = m[0]*v.x + m[1]*v.y + m[2]*v.z + m[3]*v.w
    // r.y = dot(M的第1行, v) = m[4]*v.x + m[5]*v.y + m[6]*v.z + m[7]*v.w
    
    // Load matrix rows
    float32x4_t r0 = vld1q_f32(m);      // m[0], m[1], m[2], m[3] - 第0行
    float32x4_t r1 = vld1q_f32(m + 4);  // m[4], m[5], m[6], m[7] - 第1行
    float32x4_t r2 = vld1q_f32(m + 8);  // m[8], m[9], m[10], m[11] - 第2行
    float32x4_t r3 = vld1q_f32(m + 12); // m[12], m[13], m[14], m[15] - 第3行
    
    // Load vector components
    float32x4_t vx = vdupq_n_f32(v.x);
    float32x4_t vy = vdupq_n_f32(v.y);
    float32x4_t vz = vdupq_n_f32(v.z);
    float32x4_t vw = vdupq_n_f32(v.w);
    
    // Compute dot products using pairwise addition
    // r.x = m[0]*v.x + m[1]*v.y + m[2]*v.z + m[3]*v.w
    float32x4_t r0_v = vmulq_f32(r0, vx);
    r0_v = vmlaq_f32(r0_v, r1, vy);
    r0_v = vmlaq_f32(r0_v, r2, vz);
    r0_v = vmlaq_f32(r0_v, r3, vw);
    
    // Wait, that's wrong. We need dot(row, v), not element-wise
    // Correct approach:
    // r0 = [m[0], m[1], m[2], m[3]]
    // r0 * vx = [m[0]*v.x, m[1]*v.x, m[2]*v.x, m[3]*v.x]
    // We need: m[0]*v.x + m[1]*v.y + m[2]*v.z + m[3]*v.w
    
    // Load vector as array for easy access
    float32x4_t v_vec = {v.x, v.y, v.z, v.w};
    
    // Compute r.x = dot(r0, v)
    float32x4_t prod0 = vmulq_f32(r0, v_vec);
    float32x2_t sum0_low = vpadd_f32(vget_low_f32(prod0), vget_high_f32(prod0));
    float32x2_t sum0 = vpadd_f32(sum0_low, sum0_low);
    
    // Compute r.y = dot(r1, v)
    float32x4_t prod1 = vmulq_f32(r1, v_vec);
    float32x2_t sum1_low = vpadd_f32(vget_low_f32(prod1), vget_high_f32(prod1));
    float32x2_t sum1 = vpadd_f32(sum1_low, sum1_low);
    
    // Compute r.z = dot(r2, v)
    float32x4_t prod2 = vmulq_f32(r2, v_vec);
    float32x2_t sum2_low = vpadd_f32(vget_low_f32(prod2), vget_high_f32(prod2));
    float32x2_t sum2 = vpadd_f32(sum2_low, sum2_low);
    
    // Compute r.w = dot(r3, v)
    float32x4_t prod3 = vmulq_f32(r3, v_vec);
    float32x2_t sum3_low = vpadd_f32(vget_low_f32(prod3), vget_high_f32(prod3));
    float32x2_t sum3 = vpadd_f32(sum3_low, sum3_low);
    
    r->x = vget_lane_f32(sum0, 0);
    r->y = vget_lane_f32(sum1, 0);
    r->z = vget_lane_f32(sum2, 0);
    r->w = vget_lane_f32(sum3, 0);
#endif
}

static inline void pgl_neon_mult_m3_v3(vec3* r, const float* m, const vec3 v)
{
#ifndef ROW_MAJOR
    float32x4_t vx = vdupq_n_f32(v.x);
    float32x4_t vy = vdupq_n_f32(v.y);
    float32x4_t vz = vdupq_n_f32(v.z);

    float32x4_t c0 = vld1q_f32(m);
    float32x4_t c1 = vld1q_f32(m + 3);
    float32x4_t c2 = vld1q_f32(m + 6);

    float32x4_t res = vmulq_f32(vx, c0);
    res = vmlaq_f32(res, vy, c1);
    res = vmlaq_f32(res, vz, c2);

    r->x = vgetq_lane_f32(res, 0);
    r->y = vgetq_lane_f32(res, 1);
    r->z = vgetq_lane_f32(res, 2);
#else
    // Row-major: r = M * v
    // r.x = dot(M的第0行, v) = m[0]*v.x + m[1]*v.y + m[2]*v.z
    // r.y = dot(M的第1行, v) = m[3]*v.x + m[4]*v.y + m[5]*v.z
    // r.z = dot(M的第2行, v) = m[6]*v.x + m[7]*v.y + m[8]*v.z
    
    // Load matrix rows (3x3 matrix stored in memory as 3 rows of 3 floats)
    // m[0], m[1], m[2] - 第0行
    // m[3], m[4], m[5] - 第1行
    // m[6], m[7], m[8] - 第2行
    float32x4_t row0 = vld1q_f32(m);      // m[0], m[1], m[2], m[3] - 第0行 + 额外
    float32x4_t row1 = vld1q_f32(m + 3);  // m[3], m[4], m[5], m[6] - 第1行 + 额外
    float32x4_t row2 = vld1q_f32(m + 6);  // m[6], m[7], m[8], m[9] - 第2行 + 额外
    
    // Load vector
    float32x4_t v_vec = {v.x, v.y, v.z, 0.0f};
    
    // Compute r.x = dot(row0, v)
    float32x4_t prod0 = vmulq_f32(row0, v_vec);
    float32x2_t sum0_low = vpadd_f32(vget_low_f32(prod0), vget_high_f32(prod0));
    float32x2_t sum0 = vpadd_f32(sum0_low, sum0_low);
    
    // Compute r.y = dot(row1, v)
    float32x4_t prod1 = vmulq_f32(row1, v_vec);
    float32x2_t sum1_low = vpadd_f32(vget_low_f32(prod1), vget_high_f32(prod1));
    float32x2_t sum1 = vpadd_f32(sum1_low, sum1_low);
    
    // Compute r.z = dot(row2, v)
    float32x4_t prod2 = vmulq_f32(row2, v_vec);
    float32x2_t sum2_low = vpadd_f32(vget_low_f32(prod2), vget_high_f32(prod2));
    float32x2_t sum2 = vpadd_f32(sum2_low, sum2_low);
    
    r->x = vget_lane_f32(sum0, 0);
    r->y = vget_lane_f32(sum1, 0);
    r->z = vget_lane_f32(sum2, 0);
#endif
}

static inline void pgl_neon_mult_m2_v2(vec2* r, const float* m, const vec2 v)
{
#ifndef ROW_MAJOR
    float32x2_t vx = vdup_n_f32(v.x);
    float32x2_t vy = vdup_n_f32(v.y);

    float32x2_t c0 = vld1_f32(m);
    float32x2_t c1 = vld1_f32(m + 2);

    float32x2_t res = vmul_f32(vx, c0);
    res = vmla_f32(res, vy, c1);

    r->x = vget_lane_f32(res, 0);
    r->y = vget_lane_f32(res, 1);
#else
    // Row-major: r = M * v
    // r.x = dot(M的第0行, v) = m[0]*v.x + m[1]*v.y
    // r.y = dot(M的第1行, v) = m[2]*v.x + m[3]*v.y
    
    // Load matrix rows
    float32x2_t row0 = vld1_f32(m);      // m[0], m[1] - 第0行
    float32x2_t row1 = vld1_f32(m + 2);  // m[2], m[3] - 第1行
    
    // Load vector
    float32x2_t v_vec = {v.x, v.y};
    
    // Compute r.x = dot(row0, v)
    float32x2_t prod0 = vmul_f32(row0, v_vec);
    float32x2_t sum0 = vpadd_f32(prod0, prod0);
    
    // Compute r.y = dot(row1, v)
    float32x2_t prod1 = vmul_f32(row1, v_vec);
    float32x2_t sum1 = vpadd_f32(prod1, prod1);
    
    r->x = vget_lane_f32(sum0, 0);
    r->y = vget_lane_f32(sum1, 0);
#endif
}

// Load rotation matrix NEON optimizations
static inline void pgl_neon_load_rotation_m2(float* mat, float s, float c)
{
    float32x2_t cos_vec = vdup_n_f32(c);
    float32x2_t sin_vec = vdup_n_f32(s);
    float32x2_t neg_sin_vec = vdup_n_f32(-s);

#ifndef ROW_MAJOR
    // [ c  -s ]
    // [ s   c ]
    float32x2_t col0 = vzip_f32(cos_vec, sin_vec).val[0];
    float32x2_t col1 = vzip_f32(neg_sin_vec, cos_vec).val[0];
    vst1_f32(mat, col0);
    vst1_f32(mat + 2, col1);
#else
    // [ c   s ]
    // [ -s  c ]
    float32x2_t row0 = vzip_f32(cos_vec, sin_vec).val[0];
    float32x2_t row1 = vzip_f32(neg_sin_vec, cos_vec).val[0];
    vst1_f32(mat, row0);
    vst1_f32(mat + 2, row1);
#endif
}

static inline void pgl_neon_load_rotation_m3(float* mat, vec3 v, float s, float c)
{
    float one_c = 1.0f - c;

    // Compute intermediate values
    float32x4_t v_vec = vld1q_f32(&v.x);
    float32x4_t v_squared = vmulq_f32(v_vec, v_vec);
    float xx = vgetq_lane_f32(v_squared, 0);
    float yy = vgetq_lane_f32(v_squared, 1);
    float zz = vgetq_lane_f32(v_squared, 2);

    // Compute cross products using NEON
    float32x4_t v_yzx = vextq_f32(v_vec, v_vec, 1);  // [y, z, x, w]
    float32x4_t v_zxy = vextq_f32(v_vec, v_vec, 2);  // [z, x, y, w]
    float32x4_t xy_yz_zx = vmulq_f32(v_vec, v_yzx);  // [xy, yz, zx, ...]

    float xy = vgetq_lane_f32(xy_yz_zx, 0);
    float yz = vgetq_lane_f32(xy_yz_zx, 1);
    float zx = vgetq_lane_f32(xy_yz_zx, 2);

    // Compute scaled values
    float32x4_t v_s = vmulq_n_f32(v_vec, s);
    float xs = vgetq_lane_f32(v_s, 0);
    float ys = vgetq_lane_f32(v_s, 1);
    float zs = vgetq_lane_f32(v_s, 2);

    // Compute one_c * products
    float32x4_t one_c_v = vdupq_n_f32(one_c);
    float32x4_t xx_yy_zz = vmulq_n_f32(v_squared, one_c);
    float32x4_t xy_yz_zx_vec = vmulq_n_f32(xy_yz_zx, one_c);

    float one_c_xx = vgetq_lane_f32(xx_yy_zz, 0) + c;
    float one_c_yy = vgetq_lane_f32(xx_yy_zz, 1) + c;
    float one_c_zz = vgetq_lane_f32(xx_yy_zz, 2) + c;
    float one_c_xy = vgetq_lane_f32(xy_yz_zx_vec, 0);
    float one_c_yz = vgetq_lane_f32(xy_yz_zx_vec, 1);
    float one_c_zx = vgetq_lane_f32(xy_yz_zx_vec, 2);

#ifndef ROW_MAJOR
    mat[0] = one_c_xx;           mat[3] = one_c_xy - zs;      mat[6] = one_c_zx + ys;
    mat[1] = one_c_xy + zs;      mat[4] = one_c_yy;           mat[7] = one_c_yz - xs;
    mat[2] = one_c_zx - ys;      mat[5] = one_c_yz + xs;      mat[8] = one_c_zz;
#else
    mat[0] = one_c_xx;           mat[1] = one_c_xy - zs;      mat[2] = one_c_zx + ys;
    mat[3] = one_c_xy + zs;      mat[4] = one_c_yy;           mat[5] = one_c_yz - xs;
    mat[6] = one_c_zx - ys;      mat[7] = one_c_yz + xs;      mat[8] = one_c_zz;
#endif
}

static inline void pgl_neon_load_rotation_m4(float* mat, vec3 v, float s, float c)
{
    float one_c = 1.0f - c;

    // Compute intermediate values using NEON
    float32x4_t v_vec = vld1q_f32(&v.x);
    float32x4_t v_squared = vmulq_f32(v_vec, v_vec);

    // Compute cross products
    float32x4_t v_yzx = vextq_f32(v_vec, v_vec, 1);
    float32x4_t xy_yz_zx = vmulq_f32(v_vec, v_yzx);

    // Compute scaled values
    float32x4_t v_s = vmulq_n_f32(v_vec, s);

    // Compute one_c * products
    float32x4_t one_c_v = vdupq_n_f32(one_c);
    float32x4_t xx_yy_zz = vmlaq_n_f32(vdupq_n_f32(c), v_squared, one_c);
    float32x4_t xy_yz_zx_vec = vmulq_n_f32(xy_yz_zx, one_c);

    float one_c_xx = vgetq_lane_f32(xx_yy_zz, 0);
    float one_c_yy = vgetq_lane_f32(xx_yy_zz, 1);
    float one_c_zz = vgetq_lane_f32(xx_yy_zz, 2);
    float one_c_xy = vgetq_lane_f32(xy_yz_zx_vec, 0);
    float one_c_yz = vgetq_lane_f32(xy_yz_zx_vec, 1);
    float one_c_zx = vgetq_lane_f32(xy_yz_zx_vec, 2);
    float xs = vgetq_lane_f32(v_s, 0);
    float ys = vgetq_lane_f32(v_s, 1);
    float zs = vgetq_lane_f32(v_s, 2);

#ifndef ROW_MAJOR
    mat[0] = one_c_xx;   mat[4] = one_c_xy - zs;  mat[8] = one_c_zx + ys;  mat[12] = 0.0f;
    mat[1] = one_c_xy + zs;  mat[5] = one_c_yy;   mat[9] = one_c_yz - xs;  mat[13] = 0.0f;
    mat[2] = one_c_zx - ys;  mat[6] = one_c_yz + xs;  mat[10] = one_c_zz;  mat[14] = 0.0f;
    mat[3] = 0.0f;       mat[7] = 0.0f;       mat[11] = 0.0f;      mat[15] = 1.0f;
#else
    mat[0] = one_c_xx;   mat[1] = one_c_xy - zs;  mat[2] = one_c_zx + ys;  mat[3] = 0.0f;
    mat[4] = one_c_xy + zs;  mat[5] = one_c_yy;   mat[6] = one_c_yz - xs;  mat[7] = 0.0f;
    mat[8] = one_c_zx - ys;  mat[9] = one_c_yz + xs;  mat[10] = one_c_zz;  mat[11] = 0.0f;
    mat[12] = 0.0f;      mat[13] = 0.0f;      mat[14] = 0.0f;      mat[15] = 1.0f;
#endif
}

// Scale matrix NEON optimizations
static inline void pgl_neon_scale_m3(float* m, float x, float y, float z)
{
    // Create vectors for diagonal and zeros
    float32x4_t diag1 = {x, 0.0f, 0.0f, 0.0f};
    float32x4_t diag2 = {0.0f, y, 0.0f, 0.0f};
    float32x4_t diag3 = {0.0f, 0.0f, z, 0.0f};

#ifndef ROW_MAJOR
    // Column major: store columns
    // Col 0: [x, 0, 0]
    // Col 1: [0, y, 0]
    // Col 2: [0, 0, z]
    vst1q_f32(m, diag1);
    vst1q_f32(m + 3, diag2);
    vst1q_f32(m + 6, diag3);
#else
    // Row major: store rows
    // Row 0: [x, 0, 0]
    // Row 1: [0, y, 0]
    // Row 2: [0, 0, z]
    vst1q_f32(m, diag1);
    vst1q_f32(m + 3, diag2);
    vst1q_f32(m + 6, diag3);
#endif
}

static inline void pgl_neon_scale_m4(float* m, float x, float y, float z)
{
    float32x4_t zero = vdupq_n_f32(0.0f);
    float32x4_t one = vdupq_n_f32(1.0f);

#ifndef ROW_MAJOR
    // Column major layout
    // Col 0: [x, 0, 0, 0]
    // Col 1: [0, y, 0, 0]
    // Col 2: [0, 0, z, 0]
    // Col 3: [0, 0, 0, 1]
    float32x4_t col0 = {x, 0.0f, 0.0f, 0.0f};
    float32x4_t col1 = {0.0f, y, 0.0f, 0.0f};
    float32x4_t col2 = {0.0f, 0.0f, z, 0.0f};
    float32x4_t col3 = {0.0f, 0.0f, 0.0f, 1.0f};

    vst1q_f32(m, col0);
    vst1q_f32(m + 4, col1);
    vst1q_f32(m + 8, col2);
    vst1q_f32(m + 12, col3);
#else
    // Row major layout
    // Row 0: [x, 0, 0, 0]
    // Row 1: [0, y, 0, 0]
    // Row 2: [0, 0, z, 0]
    // Row 3: [0, 0, 0, 1]
    float32x4_t row0 = {x, 0.0f, 0.0f, 0.0f};
    float32x4_t row1 = {0.0f, y, 0.0f, 0.0f};
    float32x4_t row2 = {0.0f, 0.0f, z, 0.0f};
    float32x4_t row3 = {0.0f, 0.0f, 0.0f, 1.0f};

    vst1q_f32(m, row0);
    vst1q_f32(m + 4, row1);
    vst1q_f32(m + 8, row2);
    vst1q_f32(m + 12, row3);
#endif
}

// Translation matrix NEON optimization
static inline void pgl_neon_translation_m4(float* m, float x, float y, float z)
{
#ifndef ROW_MAJOR
    // Column major layout
    // Col 0: [1, 0, 0, 0]
    // Col 1: [0, 1, 0, 0]
    // Col 2: [0, 0, 1, 0]
    // Col 3: [x, y, z, 1]
    float32x4_t col0 = {1.0f, 0.0f, 0.0f, 0.0f};
    float32x4_t col1 = {0.0f, 1.0f, 0.0f, 0.0f};
    float32x4_t col2 = {0.0f, 0.0f, 1.0f, 0.0f};
    float32x4_t col3 = {x, y, z, 1.0f};

    vst1q_f32(m, col0);
    vst1q_f32(m + 4, col1);
    vst1q_f32(m + 8, col2);
    vst1q_f32(m + 12, col3);
#else
    // Row major layout
    // Row 0: [1, 0, 0, x]
    // Row 1: [0, 1, 0, y]
    // Row 2: [0, 0, 1, z]
    // Row 3: [0, 0, 0, 1]
    float32x4_t row0 = {1.0f, 0.0f, 0.0f, x};
    float32x4_t row1 = {0.0f, 1.0f, 0.0f, y};
    float32x4_t row2 = {0.0f, 0.0f, 1.0f, z};
    float32x4_t row3 = {0.0f, 0.0f, 0.0f, 1.0f};

    vst1q_f32(m, row0);
    vst1q_f32(m + 4, row1);
    vst1q_f32(m + 8, row2);
    vst1q_f32(m + 12, row3);
#endif
}

// Extract rotation matrix NEON optimization
static inline void pgl_neon_extract_rotation_m4(float* dst, const float* src, int normalize)
{
    // Load 3 columns/rows from source (first 3 elements of each column/row for column-major)
#ifndef ROW_MAJOR
    // Column major: extract first 3 rows from first 3 columns
    float32x4_t col0 = vld1q_f32(src);
    float32x4_t col1 = vld1q_f32(src + 4);
    float32x4_t col2 = vld1q_f32(src + 8);

    // Extract first 3 elements from each column
    float32x4_t row0 = {vgetq_lane_f32(col0, 0), vgetq_lane_f32(col1, 0), vgetq_lane_f32(col2, 0), 0.0f};
    float32x4_t row1 = {vgetq_lane_f32(col0, 1), vgetq_lane_f32(col1, 1), vgetq_lane_f32(col2, 1), 0.0f};
    float32x4_t row2 = {vgetq_lane_f32(col0, 2), vgetq_lane_f32(col1, 2), vgetq_lane_f32(col2, 2), 0.0f};

    if (normalize) {
        // Normalize each row using NEON
        float32x4_t sq0 = vmulq_f32(row0, row0);
        float32x4_t sq1 = vmulq_f32(row1, row1);
        float32x4_t sq2 = vmulq_f32(row2, row2);

        // Horizontal add for each row's length squared
        float len0 = vgetq_lane_f32(sq0, 0) + vgetq_lane_f32(sq0, 1) + vgetq_lane_f32(sq0, 2);
        float len1 = vgetq_lane_f32(sq1, 0) + vgetq_lane_f32(sq1, 1) + vgetq_lane_f32(sq1, 2);
        float len2 = vgetq_lane_f32(sq2, 0) + vgetq_lane_f32(sq2, 1) + vgetq_lane_f32(sq2, 2);

        len0 = 1.0f / sqrtf(len0);
        len1 = 1.0f / sqrtf(len1);
        len2 = 1.0f / sqrtf(len2);

        row0 = vmulq_n_f32(row0, len0);
        row1 = vmulq_n_f32(row1, len1);
        row2 = vmulq_n_f32(row2, len2);
    }

    // Store to destination (3x3 matrix)
    vst1q_f32(dst, row0);
    vst1q_f32(dst + 3, row1);
    vst1q_f32(dst + 6, row2);
#else
    // Row major: extract first 3 columns from first 3 rows
    float32x4_t row0 = vld1q_f32(src);
    float32x4_t row1 = vld1q_f32(src + 4);
    float32x4_t row2 = vld1q_f32(src + 8);

    // Extract first 3 elements from each row
    float32x4x2_t zip01 = vzipq_f32(row0, row1);
    float32x4x2_t zip2 = vzipq_f32(row2, vdupq_n_f32(0.0f));

    float32x4_t col0 = {vgetq_lane_f32(row0, 0), vgetq_lane_f32(row1, 0), vgetq_lane_f32(row2, 0), 0.0f};
    float32x4_t col1 = {vgetq_lane_f32(row0, 1), vgetq_lane_f32(row1, 1), vgetq_lane_f32(row2, 1), 0.0f};
    float32x4_t col2 = {vgetq_lane_f32(row0, 2), vgetq_lane_f32(row1, 2), vgetq_lane_f32(row2, 2), 0.0f};

    if (normalize) {
        float32x4_t sq0 = vmulq_f32(col0, col0);
        float32x4_t sq1 = vmulq_f32(col1, col1);
        float32x4_t sq2 = vmulq_f32(col2, col2);

        float len0 = vgetq_lane_f32(sq0, 0) + vgetq_lane_f32(sq0, 1) + vgetq_lane_f32(sq0, 2);
        float len1 = vgetq_lane_f32(sq1, 0) + vgetq_lane_f32(sq1, 1) + vgetq_lane_f32(sq1, 2);
        float len2 = vgetq_lane_f32(sq2, 0) + vgetq_lane_f32(sq2, 1) + vgetq_lane_f32(sq2, 2);

        len0 = 1.0f / sqrtf(len0);
        len1 = 1.0f / sqrtf(len1);
        len2 = 1.0f / sqrtf(len2);

        col0 = vmulq_n_f32(col0, len0);
        col1 = vmulq_n_f32(col1, len1);
        col2 = vmulq_n_f32(col2, len2);
    }

    vst1q_f32(dst, col0);
    vst1q_f32(dst + 3, col1);
    vst1q_f32(dst + 6, col2);
#endif
}

// Vertex transformation NEON optimization
static inline void pgl_neon_transform_vertex(vec4* result, const mat4 m, const vec4 v)
{
#ifndef ROW_MAJOR
    float32x4_t vx = vdupq_n_f32(v.x);
    float32x4_t vy = vdupq_n_f32(v.y);
    float32x4_t vz = vdupq_n_f32(v.z);
    float32x4_t vw = vdupq_n_f32(v.w);

    float32x4_t c0 = vld1q_f32(m);
    float32x4_t c1 = vld1q_f32(m + 4);
    float32x4_t c2 = vld1q_f32(m + 8);
    float32x4_t c3 = vld1q_f32(m + 12);

    float32x4_t res = vmulq_f32(vx, c0);
    res = vmlaq_f32(res, vy, c1);
    res = vmlaq_f32(res, vz, c2);
    res = vmlaq_f32(res, vw, c3);

    result->x = vgetq_lane_f32(res, 0);
    result->y = vgetq_lane_f32(res, 1);
    result->z = vgetq_lane_f32(res, 2);
    result->w = vgetq_lane_f32(res, 3);
#else
    float32x4_t vx = vdupq_n_f32(v.x);
    float32x4_t vy = vdupq_n_f32(v.y);
    float32x4_t vz = vdupq_n_f32(v.z);
    float32x4_t vw = vdupq_n_f32(v.w);

    float32x4_t r0 = vld1q_f32(m);
    float32x4_t r1 = vld1q_f32(m + 4);
    float32x4_t r2 = vld1q_f32(m + 8);
    float32x4_t r3 = vld1q_f32(m + 12);

    float32x4_t res = vmulq_f32(vx, r0);
    res = vmlaq_f32(res, vy, r1);
    res = vmlaq_f32(res, vz, r2);
    res = vmlaq_f32(res, vw, r3);

    result->x = vgetq_lane_f32(res, 0);
    result->y = vgetq_lane_f32(res, 1);
    result->z = vgetq_lane_f32(res, 2);
    result->w = vgetq_lane_f32(res, 3);
#endif
}

// Batch transform 3 vertices at once - reduces function call overhead and improves cache locality
static inline void pgl_neon_transform_3vertices(vec4* r0, vec4* r1, vec4* r2, const mat4 m, 
                                                  const vec4 v0, const vec4 v1, const vec4 v2)
{
#ifndef ROW_MAJOR
    // Load matrix columns once
    float32x4_t c0 = vld1q_f32(m);
    float32x4_t c1 = vld1q_f32(m + 4);
    float32x4_t c2 = vld1q_f32(m + 8);
    float32x4_t c3 = vld1q_f32(m + 12);
    
    // Transform vertex 0
    float32x4_t res0 = vmulq_f32(vdupq_n_f32(v0.x), c0);
    res0 = vmlaq_f32(res0, vdupq_n_f32(v0.y), c1);
    res0 = vmlaq_f32(res0, vdupq_n_f32(v0.z), c2);
    res0 = vmlaq_f32(res0, vdupq_n_f32(v0.w), c3);
    
    // Transform vertex 1
    float32x4_t res1 = vmulq_f32(vdupq_n_f32(v1.x), c0);
    res1 = vmlaq_f32(res1, vdupq_n_f32(v1.y), c1);
    res1 = vmlaq_f32(res1, vdupq_n_f32(v1.z), c2);
    res1 = vmlaq_f32(res1, vdupq_n_f32(v1.w), c3);
    
    // Transform vertex 2
    float32x4_t res2 = vmulq_f32(vdupq_n_f32(v2.x), c0);
    res2 = vmlaq_f32(res2, vdupq_n_f32(v2.y), c1);
    res2 = vmlaq_f32(res2, vdupq_n_f32(v2.z), c2);
    res2 = vmlaq_f32(res2, vdupq_n_f32(v2.w), c3);
    
    // Store results
    r0->x = vgetq_lane_f32(res0, 0); r0->y = vgetq_lane_f32(res0, 1); 
    r0->z = vgetq_lane_f32(res0, 2); r0->w = vgetq_lane_f32(res0, 3);
    
    r1->x = vgetq_lane_f32(res1, 0); r1->y = vgetq_lane_f32(res1, 1);
    r1->z = vgetq_lane_f32(res1, 2); r1->w = vgetq_lane_f32(res1, 3);
    
    r2->x = vgetq_lane_f32(res2, 0); r2->y = vgetq_lane_f32(res2, 1);
    r2->z = vgetq_lane_f32(res2, 2); r2->w = vgetq_lane_f32(res2, 3);
#else
    // Row-major: Load matrix rows once
    float32x4_t m0 = vld1q_f32(m);
    float32x4_t m1 = vld1q_f32(m + 4);
    float32x4_t m2 = vld1q_f32(m + 8);
    float32x4_t m3 = vld1q_f32(m + 12);
    
    // Transform vertex 0
    float32x4_t res0 = vmulq_f32(vdupq_n_f32(v0.x), m0);
    res0 = vmlaq_f32(res0, vdupq_n_f32(v0.y), m1);
    res0 = vmlaq_f32(res0, vdupq_n_f32(v0.z), m2);
    res0 = vmlaq_f32(res0, vdupq_n_f32(v0.w), m3);
    
    // Transform vertex 1
    float32x4_t res1 = vmulq_f32(vdupq_n_f32(v1.x), m0);
    res1 = vmlaq_f32(res1, vdupq_n_f32(v1.y), m1);
    res1 = vmlaq_f32(res1, vdupq_n_f32(v1.z), m2);
    res1 = vmlaq_f32(res1, vdupq_n_f32(v1.w), m3);
    
    // Transform vertex 2
    float32x4_t res2 = vmulq_f32(vdupq_n_f32(v2.x), m0);
    res2 = vmlaq_f32(res2, vdupq_n_f32(v2.y), m1);
    res2 = vmlaq_f32(res2, vdupq_n_f32(v2.z), m2);
    res2 = vmlaq_f32(res2, vdupq_n_f32(v2.w), m3);
    
    // Store results
    r0->x = vgetq_lane_f32(res0, 0); r0->y = vgetq_lane_f32(res0, 1); 
    r0->z = vgetq_lane_f32(res0, 2); r0->w = vgetq_lane_f32(res0, 3);
    
    r1->x = vgetq_lane_f32(res1, 0); r1->y = vgetq_lane_f32(res1, 1);
    r1->z = vgetq_lane_f32(res1, 2); r1->w = vgetq_lane_f32(res1, 3);
    
    r2->x = vgetq_lane_f32(res2, 0); r2->y = vgetq_lane_f32(res2, 1);
    r2->z = vgetq_lane_f32(res2, 2); r2->w = vgetq_lane_f32(res2, 3);
#endif
}

// NEON optimized perspective-correct interpolation for 4 vertices at once
static inline void pgl_neon_interp_perspective_4(float* results, const float* a, const float* b,
                                                  const float* c, const float alpha,
                                                  const float beta, const float gamma, const float inv_w_sum)
{
    for (int i = 0; i < 4; i++) {
        results[i] = (a[i] * alpha + b[i] * beta + c[i] * gamma) * inv_w_sum;
    }
}

// NEON optimized reciprocal (1/x) approximation using Newton-Raphson
static inline float32x4_t pgl_neon_rcp_f32(float32x4_t x)
{
    // Initial approximation
    float32x4_t approx = vrecpeq_f32(x);
    // Newton-Raphson refinement: approx = approx * (2 - x * approx)
    approx = vmulq_f32(approx, vsubq_f32(vdupq_n_f32(2.0f), vmulq_f32(x, approx)));
    return approx;
}

// NEON optimized 1/sqrt(x) approximation
static inline float32x4_t pgl_neon_rsqrt_f32(float32x4_t x)
{
    float32x4_t approx = vrsqrteq_f32(x);
    // Newton-Raphson refinement
    approx = vmulq_f32(approx, vsubq_f32(vdupq_n_f32(1.5f),
                              vmulq_f32(vdupq_n_f32(0.5f), vmulq_f32(x, vmulq_f32(approx, approx)))));
    return approx;
}

// NEON batch multiply-add for vertex attribute interpolation
static inline void pgl_neon_mult_add_4(float* dst, const float* src, float scale, int count)
{
    float32x4_t scale_vec = vdupq_n_f32(scale);
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        float32x4_t src_vec = vld1q_f32(src + i);
        float32x4_t dst_vec = vld1q_f32(dst + i);
        vst1q_f32(dst + i, vmlaq_f32(dst_vec, src_vec, scale_vec));
    }
}

// Prefetch next row of pixels for better cache utilization
static inline void pgl_prefetch_row(const void* ptr)
{
    __builtin_prefetch(ptr, 0, 3);  // Read prefetch, high temporal locality
}

// Early-z rejection - check if all 4 pixels fail depth test
static inline int pgl_neon_early_z_reject_batch(uint32_t* src_depths, uint32_t* zbuf, int count)
{
    int reject_count = 0;
    for (int i = 0; i + 4 <= count; i += 4) {
        uint32x4_t src_z = vld1q_u32(src_depths + i);
        uint32x4_t dst_z = vld1q_u32(zbuf + i);
        // If all 4 src_z <= dst_z, reject the whole batch
        uint32x4_t fail_mask = vcleq_u32(src_z, dst_z);
        // Check if all lanes are set (all pixels failed)
        uint32_t low = vgetq_lane_u32(fail_mask, 0) & vgetq_lane_u32(fail_mask, 1);
        uint32_t high = vgetq_lane_u32(fail_mask, 2) & vgetq_lane_u32(fail_mask, 3);
        if (low & high) {
            reject_count += 4;
        }
    }
    return reject_count;
}

// NEON optimized batch depth test - tests 4 depths at once
static inline int pgl_neon_depth_test_batch(uint32_t* zbuf, uint32_t* src_depths, int count)
{
    int pass_count = 0;
    uint32_t z_values[4];
    
    for (int i = 0; i + 4 <= count; i += 4) {
        uint32x4_t src_z = vld1q_u32(src_depths + i);
        uint32x4_t dst_z = vld1q_u32(zbuf + i);
        
        // Test: src_z > dst_z (new depth is closer)
        uint32x4_t result = vcgtq_u32(src_z, dst_z);
        
        // Store pass/fail mask
        vst1q_u32(z_values, result);
        
        // Update z-buffer for passing pixels
        uint32x4_t new_z = vbslq_u32(result, src_z, dst_z);
        vst1q_u32(zbuf + i, new_z);
        
        // Count passing pixels
        pass_count += vgetq_lane_u32(result, 0) + vgetq_lane_u32(result, 1) + 
                      vgetq_lane_u32(result, 2) + vgetq_lane_u32(result, 3);
    }
    
    return pass_count;
}

// NEON optimized batch pixel fill with color
static inline void pgl_neon_fill_pixels_batch(uint32_t* dst, uint32_t color, int count)
{
    uint32x4_t color_vec = vmovq_n_u32(color);
    int i = 0;
    
    for (; i + 16 <= count; i += 16) {
        vst1q_u32(dst + i, color_vec);
        vst1q_u32(dst + i + 4, color_vec);
        vst1q_u32(dst + i + 8, color_vec);
        vst1q_u32(dst + i + 12, color_vec);
    }
    for (; i < count; i++) {
        dst[i] = color;
    }
}

// NEON optimized batch blend - blend 4 pixels at once
static inline void pgl_neon_blend_pixels_batch(uint32_t* dst, uint32_t src_color, int count)
{
    uint8_t src_a = (src_color >> 24) & 0xFF;
    uint8_t src_r = (src_color >> 16) & 0xFF;
    uint8_t src_g = (src_color >> 8) & 0xFF;
    uint8_t src_b = src_color & 0xFF;
    uint8_t inv_src_a = 255 - src_a;
    
    // Fast path: fully opaque
    if (src_a == 255) {
        pgl_neon_fill_pixels_batch(dst, src_color, count);
        return;
    }
    
    // Fast path: fully transparent
    if (src_a == 0) {
        return;
    }
    
    int i = 0;
    for (; i < count; i++) {
        uint32_t d = dst[i];
        uint8_t da = (d >> 24) & 0xFF;
        uint8_t dr = (d >> 16) & 0xFF;
        uint8_t dg = (d >> 8) & 0xFF;
        uint8_t db = d & 0xFF;
        
        uint8_t r = (src_a * src_r + inv_src_a * dr) >> 8;
        uint8_t g = (src_a * src_g + inv_src_a * dg) >> 8;
        uint8_t b = (src_a * src_b + inv_src_a * db) >> 8;
        uint8_t a = src_a + ((inv_src_a * da) >> 8);
        
        dst[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
}

// Convert GL_LUMINANCE to RGBA using NEON
static inline void pgl_neon_convert_luminance_to_rgba(u8* out, u8* input, int w, int h, int pitch)
{
    uint8x16_t alpha = vdupq_n_u8(UINT8_MAX);
    for (int i = 0; i < h; ++i) {
        u8* in_row = input + i * pitch;
        u8* out_row = out + i * w * 4;
        int j = 0;
        // Process 16 pixels at a time
        for (; j + 16 <= w; j += 16) {
            uint8x16_t gray = vld1q_u8(in_row + j);
            // Duplicate gray value to R, G, B channels
            uint8x16x4_t rgba;
            rgba.val[0] = gray;  // R
            rgba.val[1] = gray;  // G
            rgba.val[2] = gray;  // B
            rgba.val[3] = alpha; // A
            vst4q_u8(out_row + j * 4, rgba);
        }
        // Handle remaining pixels
        for (; j < w; ++j) {
            u8 g = in_row[j];
            out_row[j * 4] = g;
            out_row[j * 4 + 1] = g;
            out_row[j * 4 + 2] = g;
            out_row[j * 4 + 3] = UINT8_MAX;
        }
    }
}

// Convert GL_RGB to RGBA using NEON
static inline void pgl_neon_convert_rgb_to_rgba(u8* out, u8* input, int w, int h, int pitch)
{
    uint8x16_t alpha = vdupq_n_u8(UINT8_MAX);
    for (int i = 0; i < h; ++i) {
        u8* in_row = input + i * pitch;
        u8* out_row = out + i * w * 4;
        int j = 0;
        // Process 16 pixels at a time (48 bytes input, 64 bytes output)
        for (; j + 16 <= w; j += 16) {
            // Load 48 bytes (16 RGB pixels)
            uint8x16_t r0 = vld1q_u8(in_row + j * 3);
            uint8x16_t r1 = vld1q_u8(in_row + j * 3 + 16);
            uint8x16_t r2 = vld1q_u8(in_row + j * 3 + 32);
            
            // Deinterleave RGB
            uint8x16_t r, g, b;
            // Use table lookup to extract channels
            uint8x16_t tbl_r = {0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45};
            uint8x16_t tbl_g = {1, 4, 7, 10, 13, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46};
            uint8x16_t tbl_b = {2, 5, 8, 11, 14, 17, 20, 23, 26, 29, 32, 35, 38, 41, 44, 47};
            
            // For simplicity, use scalar fallback for RGB deinterleave
            // NEON shuffle for RGB->RGBA is complex, better to use SVE or scalar
            for (int k = 0; k < 16; ++k) {
                out_row[(j + k) * 4] = in_row[(j + k) * 3];
                out_row[(j + k) * 4 + 1] = in_row[(j + k) * 3 + 1];
                out_row[(j + k) * 4 + 2] = in_row[(j + k) * 3 + 2];
                out_row[(j + k) * 4 + 3] = UINT8_MAX;
            }
        }
        // Handle remaining pixels
        for (; j < w; ++j) {
            out_row[j * 4] = in_row[j * 3];
            out_row[j * 4 + 1] = in_row[j * 3 + 1];
            out_row[j * 4 + 2] = in_row[j * 3 + 2];
            out_row[j * 4 + 3] = UINT8_MAX;
        }
    }
}

// Convert GL_BGR to RGBA using NEON (swap R and B)
static inline void pgl_neon_convert_bgr_to_rgba(u8* out, u8* input, int w, int h, int pitch)
{
    uint8x16_t alpha = vdupq_n_u8(UINT8_MAX);
    for (int i = 0; i < h; ++i) {
        u8* in_row = input + i * pitch;
        u8* out_row = out + i * w * 4;
        int j = 0;
        for (; j + 16 <= w; j += 16) {
            // Similar to RGB, use scalar for now due to complexity
            for (int k = 0; k < 16; ++k) {
                out_row[(j + k) * 4] = in_row[(j + k) * 3 + 2];     // R from B
                out_row[(j + k) * 4 + 1] = in_row[(j + k) * 3 + 1]; // G
                out_row[(j + k) * 4 + 2] = in_row[(j + k) * 3];     // B from R
                out_row[(j + k) * 4 + 3] = UINT8_MAX;
            }
        }
        for (; j < w; ++j) {
            out_row[j * 4] = in_row[j * 3 + 2];
            out_row[j * 4 + 1] = in_row[j * 3 + 1];
            out_row[j * 4 + 2] = in_row[j * 3];
            out_row[j * 4 + 3] = UINT8_MAX;
        }
    }
}

// Convert GL_BGRA to RGBA using NEON (swap R and B channels)
static inline void pgl_neon_convert_bgra_to_rgba(u8* out, u8* input, int w, int h, int pitch)
{
    for (int i = 0; i < h; ++i) {
        u8* in_row = input + i * pitch;
        u8* out_row = out + i * w * 4;
        int j = 0;
        // Process 16 pixels at a time (64 bytes)
        for (; j + 16 <= w; j += 16) {
            uint8x16x4_t bgra = vld4q_u8(in_row + j * 4);
            // bgra.val[0] = B, bgra.val[1] = G, bgra.val[2] = R, bgra.val[3] = A
            uint8x16x4_t rgba;
            rgba.val[0] = bgra.val[2]; // R
            rgba.val[1] = bgra.val[1]; // G
            rgba.val[2] = bgra.val[0]; // B
            rgba.val[3] = bgra.val[3]; // A
            vst4q_u8(out_row + j * 4, rgba);
        }
        for (; j < w; ++j) {
            out_row[j * 4] = in_row[j * 4 + 2];
            out_row[j * 4 + 1] = in_row[j * 4 + 1];
            out_row[j * 4 + 2] = in_row[j * 4];
            out_row[j * 4 + 3] = in_row[j * 4 + 3];
        }
    }
}

// Convert GL_LUMINANCE_ALPHA to RGBA using NEON
static inline void pgl_neon_convert_luminance_alpha_to_rgba(u8* out, u8* input, int w, int h, int pitch)
{
    for (int i = 0; i < h; ++i) {
        u8* in_row = input + i * pitch;
        u8* out_row = out + i * w * 4;
        int j = 0;
        // Process 16 pixels at a time (32 bytes input, 64 bytes output)
        for (; j + 16 <= w; j += 16) {
            uint8x16x2_t la = vld2q_u8(in_row + j * 2);
            // la.val[0] = L, la.val[1] = A
            uint8x16x4_t rgba;
            rgba.val[0] = la.val[0]; // R = L
            rgba.val[1] = la.val[0]; // G = L
            rgba.val[2] = la.val[0]; // B = L
            rgba.val[3] = la.val[1]; // A
            vst4q_u8(out_row + j * 4, rgba);
        }
        for (; j < w; ++j) {
            u8 l = in_row[j * 2];
            out_row[j * 4] = l;
            out_row[j * 4 + 1] = l;
            out_row[j * 4 + 2] = l;
            out_row[j * 4 + 3] = in_row[j * 2 + 1];
        }
    }
}

// Convert GL_RG to RGBA using NEON
static inline void pgl_neon_convert_rg_to_rgba(u8* out, u8* input, int w, int h, int pitch)
{
    uint8x16_t zero = vdupq_n_u8(0);
    uint8x16_t alpha = vdupq_n_u8(UINT8_MAX);
    for (int i = 0; i < h; ++i) {
        u8* in_row = input + i * pitch;
        u8* out_row = out + i * w * 4;
        int j = 0;
        for (; j + 16 <= w; j += 16) {
            uint8x16x2_t rg = vld2q_u8(in_row + j * 2);
            uint8x16x4_t rgba;
            rgba.val[0] = rg.val[0]; // R
            rgba.val[1] = rg.val[1]; // G
            rgba.val[2] = zero;      // B = 0
            rgba.val[3] = alpha;     // A = 255
            vst4q_u8(out_row + j * 4, rgba);
        }
        for (; j < w; ++j) {
            out_row[j * 4] = in_row[j * 2];
            out_row[j * 4 + 1] = in_row[j * 2 + 1];
            out_row[j * 4 + 2] = 0;
            out_row[j * 4 + 3] = UINT8_MAX;
        }
    }
}

// NEON optimized vertex data type conversion
// Convert packed vertex attributes to float vectors using NEON
static inline void pgl_neon_convert_u8_to_float(float* out, const u8* in, int count, int normalized)
{
    int i = 0;
    if (normalized) {
        float inv_255 = 1.0f / 255.0f;
        float32x4_t scale = vdupq_n_f32(inv_255);
        for (; i + 16 <= count; i += 16) {
            uint8x16_t input = vld1q_u8(in + i);
            // Process lower 8 bytes
            uint8x8_t low = vget_low_u8(input);
            uint16x8_t low16 = vmovl_u8(low);
            uint16x4_t low16_low = vget_low_u16(low16);
            uint16x4_t low16_high = vget_high_u16(low16);
            float32x4_t out0 = vmulq_f32(vcvtq_f32_u32(vmovl_u16(low16_low)), scale);
            float32x4_t out1 = vmulq_f32(vcvtq_f32_u32(vmovl_u16(low16_high)), scale);
            vst1q_f32(out + i, out0);
            vst1q_f32(out + i + 4, out1);
            // Process upper 8 bytes
            uint8x8_t high = vget_high_u8(input);
            uint16x8_t high16 = vmovl_u8(high);
            uint16x4_t high16_low = vget_low_u16(high16);
            uint16x4_t high16_high = vget_high_u16(high16);
            float32x4_t out2 = vmulq_f32(vcvtq_f32_u32(vmovl_u16(high16_low)), scale);
            float32x4_t out3 = vmulq_f32(vcvtq_f32_u32(vmovl_u16(high16_high)), scale);
            vst1q_f32(out + i + 8, out2);
            vst1q_f32(out + i + 12, out3);
        }
        for (; i < count; i++) {
            out[i] = in[i] * inv_255;
        }
    } else {
        for (; i + 16 <= count; i += 16) {
            uint8x16_t input = vld1q_u8(in + i);
            uint8x8_t low = vget_low_u8(input);
            uint16x8_t low16 = vmovl_u8(low);
            float32x4_t out0 = vcvtq_f32_u32(vmovl_u16(vget_low_u16(low16)));
            float32x4_t out1 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(low16)));
            vst1q_f32(out + i, out0);
            vst1q_f32(out + i + 4, out1);
            uint8x8_t high = vget_high_u8(input);
            uint16x8_t high16 = vmovl_u8(high);
            float32x4_t out2 = vcvtq_f32_u32(vmovl_u16(vget_low_u16(high16)));
            float32x4_t out3 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(high16)));
            vst1q_f32(out + i + 8, out2);
            vst1q_f32(out + i + 12, out3);
        }
        for (; i < count; i++) {
            out[i] = (float)in[i];
        }
    }
}

// NEON optimized buffer clear functions
static inline void pgl_neon_clear_color_buffer(uint32_t* buf, uint32_t color, int count)
{
    uint32x4_t color_vec = vdupq_n_u32(color);
    int i = 0;
#if defined(__aarch64__)
    for (; i + 16 <= count; i += 16) {
        vst1q_u32(buf + i, color_vec);
        vst1q_u32(buf + i + 4, color_vec);
        vst1q_u32(buf + i + 8, color_vec);
        vst1q_u32(buf + i + 12, color_vec);
    }
#else
    for (; i + 8 <= count; i += 8) {
        vst1q_u32(buf + i, color_vec);
        vst1q_u32(buf + i + 4, color_vec);
    }
#endif
    for (; i < count; i++) {
        buf[i] = color;
    }
}

static inline void pgl_neon_clear_depth_buffer(uint32_t* buf, uint32_t depth, int count)
{
    uint32x4_t depth_vec = vdupq_n_u32(depth);
    int i = 0;
#if defined(__aarch64__)
    for (; i + 16 <= count; i += 16) {
        vst1q_u32(buf + i, depth_vec);
        vst1q_u32(buf + i + 4, depth_vec);
        vst1q_u32(buf + i + 8, depth_vec);
        vst1q_u32(buf + i + 12, depth_vec);
    }
#else
    for (; i + 8 <= count; i += 8) {
        vst1q_u32(buf + i, depth_vec);
        vst1q_u32(buf + i + 4, depth_vec);
    }
#endif
    for (; i < count; i++) {
        buf[i] = depth;
    }
}

// NEON optimized pixel blend function
// Blend a batch of pixels with the same source color
static inline void pgl_neon_blend_pixels(uint32_t* dst, uint32_t src_color, int count)
{
    uint8_t src_a = (src_color >> 24) & 0xFF;
    if (src_a == 255) {
        pgl_neon_clear_color_buffer(dst, src_color, count);
        return;
    }
    if (src_a == 0) return;

    uint8_t src_r = (src_color >> 16) & 0xFF;
    uint8_t src_g = (src_color >> 8) & 0xFF;
    uint8_t src_b = src_color & 0xFF;
    uint16_t inv_src_a = 255 - src_a;

    // Precompute source contributions
    uint16_t src_r_contrib = src_a * src_r;
    uint16_t src_g_contrib = src_a * src_g;
    uint16_t src_b_contrib = src_a * src_b;
    uint16_t src_a_contrib = src_a * 255;

    int i = 0;
    for (; i + 8 <= count; i += 8) {
        uint32x4_t dst0 = vld1q_u32(dst + i);
        uint32x4_t dst1 = vld1q_u32(dst + i + 4);

        // Extract channels using table lookup would be ideal but complex
        // Use scalar fallback for now
        for (int k = 0; k < 8; k++) {
            uint32_t d = dst[i + k];
            uint8_t da = (d >> 24) & 0xFF;
            uint8_t dr = (d >> 16) & 0xFF;
            uint8_t dg = (d >> 8) & 0xFF;
            uint8_t db = d & 0xFF;

            uint8_t r = (src_r_contrib + inv_src_a * dr) >> 8;
            uint8_t g = (src_g_contrib + inv_src_a * dg) >> 8;
            uint8_t b = (src_b_contrib + inv_src_a * db) >> 8;
            uint8_t a = (src_a_contrib + inv_src_a * da) >> 8;

            dst[i + k] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    for (; i < count; i++) {
        uint32_t d = dst[i];
        uint8_t da = (d >> 24) & 0xFF;
        uint8_t dr = (d >> 16) & 0xFF;
        uint8_t dg = (d >> 8) & 0xFF;
        uint8_t db = d & 0xFF;

        uint8_t r = (src_r_contrib + inv_src_a * dr) >> 8;
        uint8_t g = (src_g_contrib + inv_src_a * dg) >> 8;
        uint8_t b = (src_b_contrib + inv_src_a * db) >> 8;
        uint8_t a = (src_a_contrib + inv_src_a * da) >> 8;

        dst[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
}

// NEON optimized barycentric interpolation for triangle rasterization
// Interpolate a single attribute across a triangle
static inline float pgl_neon_interpolate_attribute(float alpha, float beta, float gamma,
                                                    float v0, float v1, float v2,
                                                    float inv_w_sum)
{
    float32x4_t weights = {alpha, beta, gamma, 0.0f};
    float32x4_t values = {v0, v1, v2, 0.0f};
    float32x4_t prod = vmulq_f32(weights, values);
    float32x2_t sum_low = vpadd_f32(vget_low_f32(prod), vget_high_f32(prod));
    float32x2_t sum = vpadd_f32(sum_low, sum_low);
    return vget_lane_f32(sum, 0) * inv_w_sum;
}

// NEON optimized depth interpolation
static inline float pgl_neon_interpolate_depth(float alpha, float beta, float gamma,
                                                float z0, float z1, float z2)
{
    float32x4_t weights = {alpha, beta, gamma, 0.0f};
    float32x4_t depths = {z0, z1, z2, 0.0f};
    float32x4_t prod = vmulq_f32(weights, depths);
    float32x2_t sum_low = vpadd_f32(vget_low_f32(prod), vget_high_f32(prod));
    float32x2_t sum = vpadd_f32(sum_low, sum_low);
    return vget_lane_f32(sum, 0);
}

// NEON optimized vector operations for triangle setup
static inline void pgl_neon_compute_barycentric(float* alpha, float* beta, float* gamma,
                                                 float x, float y,
                                                 float x0, float y0,
                                                 float x1, float y1,
                                                 float x2, float y2,
                                                 float denom)
{
    float32x4_t px = vdupq_n_f32(x);
    float32x4_t py = vdupq_n_f32(y);
    float32x4_t p0 = {x0, x1, x2, 0.0f};
    float32x4_t p1 = {y0, y1, y2, 0.0f};

    // Compute areas using cross products
    float32x4_t v0x = vsubq_f32(p0, px);
    float32x4_t v0y = vsubq_f32(p1, py);

    // Simplified barycentric computation
    *alpha = ((x1 - x) * (y2 - y) - (x2 - x) * (y1 - y)) / denom;
    *beta = ((x2 - x) * (y0 - y) - (x0 - x) * (y2 - y)) / denom;
    *gamma = 1.0f - *alpha - *beta;
}

// NEON optimized texture coordinate interpolation
static inline void pgl_neon_interpolate_texcoord(float* s, float* t,
                                                  float alpha, float beta, float gamma,
                                                  float s0, float t0,
                                                  float s1, float t1,
                                                  float s2, float t2,
                                                  float inv_w_sum)
{
    float32x4_t weights = {alpha, beta, gamma, 0.0f};
    float32x4_t s_vals = {s0, s1, s2, 0.0f};
    float32x4_t t_vals = {t0, t1, t2, 0.0f};

    float32x4_t s_prod = vmulq_f32(weights, s_vals);
    float32x4_t t_prod = vmulq_f32(weights, t_vals);

    float32x2_t s_sum_low = vpadd_f32(vget_low_f32(s_prod), vget_high_f32(s_prod));
    float32x2_t s_sum = vpadd_f32(s_sum_low, s_sum_low);
    *s = vget_lane_f32(s_sum, 0) * inv_w_sum;

    float32x2_t t_sum_low = vpadd_f32(vget_low_f32(t_prod), vget_high_f32(t_prod));
    float32x2_t t_sum = vpadd_f32(t_sum_low, t_sum_low);
    *t = vget_lane_f32(t_sum, 0) * inv_w_sum;
}

// NEON optimized color interpolation
static inline void pgl_neon_interpolate_color(uint8_t* out_r, uint8_t* out_g, uint8_t* out_b, uint8_t* out_a,
                                               float alpha, float beta, float gamma,
                                               uint8_t r0, uint8_t g0, uint8_t b0, uint8_t a0,
                                               uint8_t r1, uint8_t g1, uint8_t b1, uint8_t a1,
                                               uint8_t r2, uint8_t g2, uint8_t b2, uint8_t a2)
{
    float32x4_t weights = {alpha, beta, gamma, 0.0f};

    float32x4_t r_vals = {(float)r0, (float)r1, (float)r2, 0.0f};
    float32x4_t g_vals = {(float)g0, (float)g1, (float)g2, 0.0f};
    float32x4_t b_vals = {(float)b0, (float)b1, (float)b2, 0.0f};
    float32x4_t a_vals = {(float)a0, (float)a1, (float)a2, 0.0f};

    float32x4_t r_prod = vmulq_f32(weights, r_vals);
    float32x4_t g_prod = vmulq_f32(weights, g_vals);
    float32x4_t b_prod = vmulq_f32(weights, b_vals);
    float32x4_t a_prod = vmulq_f32(weights, a_vals);

    float32x2_t r_sum = vpadd_f32(vpadd_f32(vget_low_f32(r_prod), vget_high_f32(r_prod)), vdup_n_f32(0));
    float32x2_t g_sum = vpadd_f32(vpadd_f32(vget_low_f32(g_prod), vget_high_f32(g_prod)), vdup_n_f32(0));
    float32x2_t b_sum = vpadd_f32(vpadd_f32(vget_low_f32(b_prod), vget_high_f32(b_prod)), vdup_n_f32(0));
    float32x2_t a_sum = vpadd_f32(vpadd_f32(vget_low_f32(a_prod), vget_high_f32(a_prod)), vdup_n_f32(0));

    *out_r = (uint8_t)vget_lane_f32(r_sum, 0);
    *out_g = (uint8_t)vget_lane_f32(g_sum, 0);
    *out_b = (uint8_t)vget_lane_f32(b_sum, 0);
    *out_a = (uint8_t)vget_lane_f32(a_sum, 0);
}

// NEON optimized memcpy for large blocks
static inline void pgl_neon_memcpy(void* dst, const void* src, size_t n)
{
    u8* d = (u8*)dst;
    const u8* s = (const u8*)src;
    size_t i = 0;

    // Align to 16 bytes first
    while (i < n && ((uintptr_t)(d + i) & 15)) {
        d[i] = s[i];
        i++;
    }

    // Copy 64 bytes at a time
    for (; i + 64 <= n; i += 64) {
        uint8x16_t v0 = vld1q_u8(s + i);
        uint8x16_t v1 = vld1q_u8(s + i + 16);
        uint8x16_t v2 = vld1q_u8(s + i + 32);
        uint8x16_t v3 = vld1q_u8(s + i + 48);
        vst1q_u8(d + i, v0);
        vst1q_u8(d + i + 16, v1);
        vst1q_u8(d + i + 32, v2);
        vst1q_u8(d + i + 48, v3);
    }

    // Copy remaining 16 byte blocks
    for (; i + 16 <= n; i += 16) {
        vst1q_u8(d + i, vld1q_u8(s + i));
    }

    // Copy remaining bytes
    for (; i < n; i++) {
        d[i] = s[i];
    }
}

// NEON optimized memset for large blocks
static inline void pgl_neon_memset(void* dst, int c, size_t n)
{
    u8* d = (u8*)dst;
    uint8x16_t val = vdupq_n_u8((u8)c);
    size_t i = 0;

    // Align to 16 bytes first
    while (i < n && ((uintptr_t)(d + i) & 15)) {
        d[i] = (u8)c;
        i++;
    }

    // Set 64 bytes at a time
    for (; i + 64 <= n; i += 64) {
        vst1q_u8(d + i, val);
        vst1q_u8(d + i + 16, val);
        vst1q_u8(d + i + 32, val);
        vst1q_u8(d + i + 48, val);
    }

    // Set remaining 16 byte blocks
    for (; i + 16 <= n; i += 16) {
        vst1q_u8(d + i, val);
    }

    // Set remaining bytes
    for (; i < n; i++) {
        d[i] = (u8)c;
    }
}

// NEON optimized vertex attribute conversion for all integer types
static inline void pgl_neon_convert_i8_to_float(float* out, const i8* in, int count, int normalized)
{
    int i = 0;
    if (normalized) {
        float scale = 1.0f / 127.0f;
        float32x4_t scale_vec = vdupq_n_f32(scale);
        for (; i + 16 <= count; i += 16) {
            int8x16_t input = vld1q_s8(in + i);
            // Process lower 8 bytes
            int8x8_t low = vget_low_s8(input);
            int16x8_t low16 = vmovl_s8(low);
            float32x4_t out0 = vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(low16))), scale_vec);
            float32x4_t out1 = vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(low16))), scale_vec);
            vst1q_f32(out + i, out0);
            vst1q_f32(out + i + 4, out1);
            // Process upper 8 bytes
            int8x8_t high = vget_high_s8(input);
            int16x8_t high16 = vmovl_s8(high);
            float32x4_t out2 = vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(high16))), scale_vec);
            float32x4_t out3 = vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(high16))), scale_vec);
            vst1q_f32(out + i + 8, out2);
            vst1q_f32(out + i + 12, out3);
        }
        for (; i < count; i++) {
            out[i] = in[i] * scale;
        }
    } else {
        for (; i + 16 <= count; i += 16) {
            int8x16_t input = vld1q_s8(in + i);
            int8x8_t low = vget_low_s8(input);
            int16x8_t low16 = vmovl_s8(low);
            float32x4_t out0 = vcvtq_f32_s32(vmovl_s16(vget_low_s16(low16)));
            float32x4_t out1 = vcvtq_f32_s32(vmovl_s16(vget_high_s16(low16)));
            vst1q_f32(out + i, out0);
            vst1q_f32(out + i + 4, out1);
            int8x8_t high = vget_high_s8(input);
            int16x8_t high16 = vmovl_s8(high);
            float32x4_t out2 = vcvtq_f32_s32(vmovl_s16(vget_low_s16(high16)));
            float32x4_t out3 = vcvtq_f32_s32(vmovl_s16(vget_high_s16(high16)));
            vst1q_f32(out + i + 8, out2);
            vst1q_f32(out + i + 12, out3);
        }
        for (; i < count; i++) {
            out[i] = (float)in[i];
        }
    }
}

static inline void pgl_neon_convert_i16_to_float(float* out, const i16* in, int count, int normalized)
{
    int i = 0;
    if (normalized) {
        float scale = 1.0f / 32767.0f;
        float32x4_t scale_vec = vdupq_n_f32(scale);
        for (; i + 8 <= count; i += 8) {
            int16x8_t input = vld1q_s16(in + i);
            float32x4_t out0 = vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(input))), scale_vec);
            float32x4_t out1 = vmulq_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(input))), scale_vec);
            vst1q_f32(out + i, out0);
            vst1q_f32(out + i + 4, out1);
        }
        for (; i < count; i++) {
            out[i] = in[i] * scale;
        }
    } else {
        for (; i + 8 <= count; i += 8) {
            int16x8_t input = vld1q_s16(in + i);
            float32x4_t out0 = vcvtq_f32_s32(vmovl_s16(vget_low_s16(input)));
            float32x4_t out1 = vcvtq_f32_s32(vmovl_s16(vget_high_s16(input)));
            vst1q_f32(out + i, out0);
            vst1q_f32(out + i + 4, out1);
        }
        for (; i < count; i++) {
            out[i] = (float)in[i];
        }
    }
}

static inline void pgl_neon_convert_u16_to_float(float* out, const u16* in, int count, int normalized)
{
    int i = 0;
    if (normalized) {
        float scale = 1.0f / 65535.0f;
        float32x4_t scale_vec = vdupq_n_f32(scale);
        for (; i + 8 <= count; i += 8) {
            uint16x8_t input = vld1q_u16(in + i);
            float32x4_t out0 = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(input))), scale_vec);
            float32x4_t out1 = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(input))), scale_vec);
            vst1q_f32(out + i, out0);
            vst1q_f32(out + i + 4, out1);
        }
        for (; i < count; i++) {
            out[i] = in[i] * scale;
        }
    } else {
        for (; i + 8 <= count; i += 8) {
            uint16x8_t input = vld1q_u16(in + i);
            float32x4_t out0 = vcvtq_f32_u32(vmovl_u16(vget_low_u16(input)));
            float32x4_t out1 = vcvtq_f32_u32(vmovl_u16(vget_high_u16(input)));
            vst1q_f32(out + i, out0);
            vst1q_f32(out + i + 4, out1);
        }
        for (; i < count; i++) {
            out[i] = (float)in[i];
        }
    }
}

static inline void pgl_neon_convert_i32_to_float(float* out, const i32* in, int count, int normalized)
{
    int i = 0;
    if (normalized) {
        float scale = 1.0f / 2147483647.0f;
        float32x4_t scale_vec = vdupq_n_f32(scale);
        for (; i + 4 <= count; i += 4) {
            int32x4_t input = vld1q_s32(in + i);
            float32x4_t result = vmulq_f32(vcvtq_f32_s32(input), scale_vec);
            vst1q_f32(out + i, result);
        }
        for (; i < count; i++) {
            out[i] = in[i] * scale;
        }
    } else {
        for (; i + 4 <= count; i += 4) {
            int32x4_t input = vld1q_s32(in + i);
            float32x4_t result = vcvtq_f32_s32(input);
            vst1q_f32(out + i, result);
        }
        for (; i < count; i++) {
            out[i] = (float)in[i];
        }
    }
}

static inline void pgl_neon_convert_u32_to_float(float* out, const u32* in, int count, int normalized)
{
    int i = 0;
    if (normalized) {
        float scale = 1.0f / 4294967295.0f;
        float32x4_t scale_vec = vdupq_n_f32(scale);
        for (; i + 4 <= count; i += 4) {
            uint32x4_t input = vld1q_u32(in + i);
            float32x4_t result = vmulq_f32(vcvtq_f32_u32(input), scale_vec);
            vst1q_f32(out + i, result);
        }
        for (; i < count; i++) {
            out[i] = in[i] * scale;
        }
    } else {
        for (; i + 4 <= count; i += 4) {
            uint32x4_t input = vld1q_u32(in + i);
            float32x4_t result = vcvtq_f32_u32(input);
            vst1q_f32(out + i, result);
        }
        for (; i < count; i++) {
            out[i] = (float)in[i];
        }
    }
}

// NEON optimized 3D texture conversion
static inline void pgl_neon_convert_3d_texture_slices(u8* out, const u8* in, int width, int height, int depth,
                                                       int padded_row_len, int components,
                                                       void (*convert_row)(u8*, const u8*, int, int, int))
{
    for (int z = 0; z < depth; ++z) {
        for (int y = 0; y < height; ++y) {
            u8* out_row = out + (z * height + y) * width * 4;
            const u8* in_row = in + (z * height + y) * padded_row_len;
            convert_row(out_row, in_row, width, 1, padded_row_len);
        }
    }
}

// NEON optimized vector batch operations
static inline void pgl_neon_add_v3_batch(vec3* out, const vec3* a, const vec3* b, int count)
{
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        float32x4_t ax = {a[i].x, a[i+1].x, a[i+2].x, a[i+3].x};
        float32x4_t ay = {a[i].y, a[i+1].y, a[i+2].y, a[i+3].y};
        float32x4_t az = {a[i].z, a[i+1].z, a[i+2].z, a[i+3].z};
        float32x4_t bx = {b[i].x, b[i+1].x, b[i+2].x, b[i+3].x};
        float32x4_t by = {b[i].y, b[i+1].y, b[i+2].y, b[i+3].y};
        float32x4_t bz = {b[i].z, b[i+1].z, b[i+2].z, b[i+3].z};

        float32x4_t rx = vaddq_f32(ax, bx);
        float32x4_t ry = vaddq_f32(ay, by);
        float32x4_t rz = vaddq_f32(az, bz);

        out[i].x = vgetq_lane_f32(rx, 0); out[i].y = vgetq_lane_f32(ry, 0); out[i].z = vgetq_lane_f32(rz, 0);
        out[i+1].x = vgetq_lane_f32(rx, 1); out[i+1].y = vgetq_lane_f32(ry, 1); out[i+1].z = vgetq_lane_f32(rz, 1);
        out[i+2].x = vgetq_lane_f32(rx, 2); out[i+2].y = vgetq_lane_f32(ry, 2); out[i+2].z = vgetq_lane_f32(rz, 2);
        out[i+3].x = vgetq_lane_f32(rx, 3); out[i+3].y = vgetq_lane_f32(ry, 3); out[i+3].z = vgetq_lane_f32(rz, 3);
    }
    for (; i < count; i++) {
        out[i] = add_v3s(a[i], b[i]);
    }
}

static inline void pgl_neon_scale_v3_batch(vec3* out, const vec3* v, float s, int count)
{
    float32x4_t scale = vdupq_n_f32(s);
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        float32x4_t vx = {v[i].x, v[i+1].x, v[i+2].x, v[i+3].x};
        float32x4_t vy = {v[i].y, v[i+1].y, v[i+2].y, v[i+3].y};
        float32x4_t vz = {v[i].z, v[i+1].z, v[i+2].z, v[i+3].z};

        float32x4_t rx = vmulq_f32(vx, scale);
        float32x4_t ry = vmulq_f32(vy, scale);
        float32x4_t rz = vmulq_f32(vz, scale);

        out[i].x = vgetq_lane_f32(rx, 0); out[i].y = vgetq_lane_f32(ry, 0); out[i].z = vgetq_lane_f32(rz, 0);
        out[i+1].x = vgetq_lane_f32(rx, 1); out[i+1].y = vgetq_lane_f32(ry, 1); out[i+1].z = vgetq_lane_f32(rz, 1);
        out[i+2].x = vgetq_lane_f32(rx, 2); out[i+2].y = vgetq_lane_f32(ry, 2); out[i+2].z = vgetq_lane_f32(rz, 2);
        out[i+3].x = vgetq_lane_f32(rx, 3); out[i+3].y = vgetq_lane_f32(ry, 3); out[i+3].z = vgetq_lane_f32(rz, 3);
    }
    for (; i < count; i++) {
        out[i] = scale_v3(v[i], s);
    }
}

// NEON optimized dot product batch
static inline void pgl_neon_dot_v3_batch(float* out, const vec3* a, const vec3* b, int count)
{
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        float32x4_t ax = {a[i].x, a[i+1].x, a[i+2].x, a[i+3].x};
        float32x4_t ay = {a[i].y, a[i+1].y, a[i+2].y, a[i+3].y};
        float32x4_t az = {a[i].z, a[i+1].z, a[i+2].z, a[i+3].z};
        float32x4_t bx = {b[i].x, b[i+1].x, b[i+2].x, b[i+3].x};
        float32x4_t by = {b[i].y, b[i+1].y, b[i+2].y, b[i+3].y};
        float32x4_t bz = {b[i].z, b[i+1].z, b[i+2].z, b[i+3].z};

        float32x4_t rx = vmulq_f32(ax, bx);
        float32x4_t ry = vmulq_f32(ay, by);
        float32x4_t rz = vmulq_f32(az, bz);

        float32x4_t sum = vaddq_f32(vaddq_f32(rx, ry), rz);

        out[i] = vgetq_lane_f32(sum, 0);
        out[i+1] = vgetq_lane_f32(sum, 1);
        out[i+2] = vgetq_lane_f32(sum, 2);
        out[i+3] = vgetq_lane_f32(sum, 3);
    }
    for (; i < count; i++) {
        out[i] = dot_v3s(a[i], b[i]);
    }
}

// NEON optimized matrix-vector multiplication batch
static inline void pgl_neon_mult_m4_v4_batch(vec4* out, const mat4 m, const vec4* v, int count)
{
    int i = 0;
    for (; i + 4 <= count; i += 4) {
        pgl_neon_mult_m4_v4(&out[i], m, v[i]);
        pgl_neon_mult_m4_v4(&out[i+1], m, v[i+1]);
        pgl_neon_mult_m4_v4(&out[i+2], m, v[i+2]);
        pgl_neon_mult_m4_v4(&out[i+3], m, v[i+3]);
    }
    for (; i < count; i++) {
        pgl_neon_mult_m4_v4(&out[i], m, v[i]);
    }
}

// NEON optimized instance matrix computation
static inline void pgl_neon_compute_instance_matrices(mat4* out, const mat4* base_mats, const mat4 mvp, int count)
{
    for (int i = 0; i < count; i++) {
        pgl_neon_mult_m4_m4(out[i], mvp, base_mats[i]);
    }
}

// NEON optimized fullscreen pixel processing
static inline void pgl_neon_prepare_fragcoords(float* out_x, float* out_y, int start_x, int start_y, int width, int height)
{
    for (int y = 0; y < height; ++y) {
        float y_coord = start_y + y + 0.5f;
        float32x4_t y_vec = vdupq_n_f32(y_coord);

        int x = 0;
        for (; x + 4 <= width; x += 4) {
            float32x4_t x_vec = {(float)(start_x + x) + 0.5f, (float)(start_x + x + 1) + 0.5f,
                                 (float)(start_x + x + 2) + 0.5f, (float)(start_x + x + 3) + 0.5f};
            vst1q_f32(out_x + y * width + x, x_vec);
            vst1q_f32(out_y + y * width + x, y_vec);
        }
        for (; x < width; x++) {
            out_x[y * width + x] = start_x + x + 0.5f;
            out_y[y * width + x] = y_coord;
        }
    }
}

// NEON optimized scissor test batch
static inline uint32_t pgl_neon_scissor_test_batch(int* results, int x, int y, int width, int height, int count)
{
    uint32_t pass_mask = 0;
    int i = 0;

    for (; i + 4 <= count; i += 4) {
        // Load 4 positions and test against scissor rect
        // Results stored in lower bits of mask
        for (int k = 0; k < 4; k++) {
            int px = results[i + k] & 0xFFFF;
            int py = (results[i + k] >> 16) & 0xFFFF;
            if (px >= x && px < x + width && py >= y && py < y + height) {
                pass_mask |= (1 << (i + k));
            }
        }
    }

    for (; i < count; i++) {
        int px = results[i] & 0xFFFF;
        int py = (results[i] >> 16) & 0xFFFF;
        if (px >= x && px < x + width && py >= y && py < y + height) {
            pass_mask |= (1 << i);
        }
    }

    return pass_mask;
}

// NEON optimized line drawing
static inline void pgl_neon_draw_line_pixels(uint32_t* buf, int* xs, int* ys, uint32_t color, int count, int stride)
{
    for (int i = 0; i < count; i++) {
        buf[ys[i] * stride + xs[i]] = color;
    }
}

// NEON optimized point sprite expansion
static inline void pgl_neon_expand_points_to_quads(vec4* out_verts, const vec4* in_points, float size, int count)
{
    float half_size = size * 0.5f;
    for (int i = 0; i < count; i++) {
        vec4 p = in_points[i];
        // Create 4 corners of the quad
        out_verts[i*4 + 0] = make_v4(p.x - half_size, p.y - half_size, p.z, p.w);
        out_verts[i*4 + 1] = make_v4(p.x + half_size, p.y - half_size, p.z, p.w);
        out_verts[i*4 + 2] = make_v4(p.x + half_size, p.y + half_size, p.z, p.w);
        out_verts[i*4 + 3] = make_v4(p.x - half_size, p.y + half_size, p.z, p.w);
    }
}

#endif //endif ARM/AARCH64

#else
#define PGL_NEON_ENABLED 0
#endif  //endif ARCH_BOOST

extern inline vec2 make_v2(float x, float y);
extern inline vec2 neg_v2(vec2 v);
extern inline void fprint_v2(FILE* f, vec2 v, const char* append);
extern inline void print_v2(vec2 v, const char* append);
extern inline int fread_v2(FILE* f, vec2* v);
extern inline float len_v2(vec2 a);
extern inline vec2 norm_v2(vec2 a);
extern inline void normalize_v2(vec2* a);
extern inline vec2 add_v2s(vec2 a, vec2 b);
extern inline vec2 sub_v2s(vec2 a, vec2 b);
extern inline vec2 mult_v2s(vec2 a, vec2 b);
extern inline vec2 div_v2s(vec2 a, vec2 b);
extern inline float dot_v2s(vec2 a, vec2 b);
extern inline vec2 scale_v2(vec2 a, float s);
extern inline int equal_v2s(vec2 a, vec2 b);
extern inline int equal_epsilon_v2s(vec2 a, vec2 b, float epsilon);
extern inline float cross_v2s(vec2 a, vec2 b);
extern inline float angle_v2s(vec2 a, vec2 b);


extern inline vec3 make_v3(float x, float y, float z);
extern inline vec3 neg_v3(vec3 v);
extern inline void fprint_v3(FILE* f, vec3 v, const char* append);
extern inline void print_v3(vec3 v, const char* append);
extern inline int fread_v3(FILE* f, vec3* v);
extern inline float len_v3(vec3 a);
extern inline vec3 norm_v3(vec3 a);
extern inline void normalize_v3(vec3* a);
extern inline vec3 add_v3s(vec3 a, vec3 b);
extern inline vec3 sub_v3s(vec3 a, vec3 b);
extern inline vec3 mult_v3s(vec3 a, vec3 b);
extern inline vec3 div_v3s(vec3 a, vec3 b);
extern inline float dot_v3s(vec3 a, vec3 b);
extern inline vec3 scale_v3(vec3 a, float s);
extern inline int equal_v3s(vec3 a, vec3 b);
extern inline int equal_epsilon_v3s(vec3 a, vec3 b, float epsilon);
extern inline vec3 cross_v3s(const vec3 u, const vec3 v);
extern inline float angle_v3s(const vec3 u, const vec3 v);


extern inline vec4 make_v4(float x, float y, float z, float w);
extern inline vec4 neg_v4(vec4 v);
extern inline void fprint_v4(FILE* f, vec4 v, const char* append);
extern inline void print_v4(vec4 v, const char* append);
extern inline int fread_v4(FILE* f, vec4* v);
extern inline float len_v4(vec4 a);
extern inline vec4 norm_v4(vec4 a);
extern inline void normalize_v4(vec4* a);
extern inline vec4 add_v4s(vec4 a, vec4 b);
extern inline vec4 sub_v4s(vec4 a, vec4 b);
extern inline vec4 mult_v4s(vec4 a, vec4 b);
extern inline vec4 div_v4s(vec4 a, vec4 b);
extern inline float dot_v4s(vec4 a, vec4 b);
extern inline vec4 scale_v4(vec4 a, float s);
extern inline int equal_v4s(vec4 a, vec4 b);
extern inline int equal_epsilon_v4s(vec4 a, vec4 b, float epsilon);


extern inline ivec2 make_iv2(int x, int y);
extern inline void fprint_iv2(FILE* f, ivec2 v, const char* append);
extern inline int fread_iv2(FILE* f, ivec2* v);

extern inline ivec3 make_iv3(int x, int y, int z);
extern inline void fprint_iv3(FILE* f, ivec3 v, const char* append);
extern inline int fread_iv3(FILE* f, ivec3* v);

extern inline ivec4 make_iv4(int x, int y, int z, int w);
extern inline void fprint_iv4(FILE* f, ivec4 v, const char* append);
extern inline int fread_iv4(FILE* f, ivec4* v);

extern inline uvec2 make_uv2(unsigned int x, unsigned int y);
extern inline void fprint_uv2(FILE* f, uvec2 v, const char* append);
extern inline int fread_uv2(FILE* f, uvec2* v);

extern inline uvec3 make_uv3(unsigned int x, unsigned int y, unsigned int z);
extern inline void fprint_uv3(FILE* f, uvec3 v, const char* append);
extern inline int fread_uv3(FILE* f, uvec3* v);

extern inline uvec4 make_uv4(unsigned int x, unsigned int y, unsigned int z, unsigned int w);
extern inline void fprint_uv4(FILE* f, uvec4 v, const char* append);
extern inline int fread_uv4(FILE* f, uvec4* v);

extern inline bvec2 make_bv2(int x, int y);
extern inline void fprint_bv2(FILE* f, bvec2 v, const char* append);
extern inline int fread_bv2(FILE* f, bvec2* v);

extern inline bvec3 make_bv3(int x, int y, int z);
extern inline void fprint_bv3(FILE* f, bvec3 v, const char* append);
extern inline int fread_bv3(FILE* f, bvec3* v);

extern inline bvec4 make_bv4(int x, int y, int z, int w);
extern inline void fprint_bv4(FILE* f, bvec4 v, const char* append);
extern inline int fread_bv4(FILE* f, bvec4* v);

extern inline vec2 v4_to_v2(vec4 a);
extern inline vec3 v4_to_v3(vec4 a);
extern inline vec2 v4_to_v2h(vec4 a);
extern inline vec3 v4_to_v3h(vec4 a);

extern inline void fprint_m2(FILE* f, mat2 m, const char* append);
extern inline void fprint_m3(FILE* f, mat3 m, const char* append);
extern inline void fprint_m4(FILE* f, mat4 m, const char* append);
extern inline void print_m2(mat2 m, const char* append);
extern inline void print_m3(mat3 m, const char* append);
extern inline void print_m4(mat4 m, const char* append);
extern inline vec2 mult_m2_v2(mat2 m, vec2 v);
extern inline vec3 mult_m3_v3(mat3 m, vec3 v);
extern inline vec4 mult_m4_v4(mat4 m, vec4 v);
extern inline void scale_m3(mat3 m, float x, float y, float z);
extern inline void scale_m4(mat4 m, float x, float y, float z);
extern inline void translation_m4(mat4 m, float x, float y, float z);
extern inline void extract_rotation_m4(mat3 dst, mat4 src, int normalize);

extern inline vec2 x_m2(mat2 m);
extern inline vec2 y_m2(mat2 m);
extern inline vec2 c1_m2(mat2 m);
extern inline vec2 c2_m2(mat2 m);

extern inline void setc1_m2(mat2 m, vec2 v);
extern inline void setc2_m2(mat2 m, vec2 v);
extern inline void setx_m2(mat2 m, vec2 v);
extern inline void sety_m2(mat2 m, vec2 v);

extern inline vec3 x_m3(mat3 m);
extern inline vec3 y_m3(mat3 m);
extern inline vec3 z_m3(mat3 m);
extern inline vec3 c1_m3(mat3 m);
extern inline vec3 c2_m3(mat3 m);
extern inline vec3 c3_m3(mat3 m);

extern inline void setc1_m3(mat3 m, vec3 v);
extern inline void setc2_m3(mat3 m, vec3 v);
extern inline void setc3_m3(mat3 m, vec3 v);

extern inline void setx_m3(mat3 m, vec3 v);
extern inline void sety_m3(mat3 m, vec3 v);
extern inline void setz_m3(mat3 m, vec3 v);

extern inline vec4 c1_m4(mat4 m);
extern inline vec4 c2_m4(mat4 m);
extern inline vec4 c3_m4(mat4 m);
extern inline vec4 c4_m4(mat4 m);

extern inline vec4 x_m4(mat4 m);
extern inline vec4 y_m4(mat4 m);
extern inline vec4 z_m4(mat4 m);
extern inline vec4 w_m4(mat4 m);

extern inline void setc1_m4v3(mat4 m, vec3 v);
extern inline void setc2_m4v3(mat4 m, vec3 v);
extern inline void setc3_m4v3(mat4 m, vec3 v);
extern inline void setc4_m4v3(mat4 m, vec3 v);

extern inline void setc1_m4v4(mat4 m, vec4 v);
extern inline void setc2_m4v4(mat4 m, vec4 v);
extern inline void setc3_m4v4(mat4 m, vec4 v);
extern inline void setc4_m4v4(mat4 m, vec4 v);

extern inline void setx_m4v3(mat4 m, vec3 v);
extern inline void sety_m4v3(mat4 m, vec3 v);
extern inline void setz_m4v3(mat4 m, vec3 v);
extern inline void setw_m4v3(mat4 m, vec3 v);

extern inline void setx_m4v4(mat4 m, vec4 v);
extern inline void sety_m4v4(mat4 m, vec4 v);
extern inline void setz_m4v4(mat4 m, vec4 v);
extern inline void setw_m4v4(mat4 m, vec4 v);


vec2 mult_m2_v2(mat2 m, vec2 v)
{
    vec2 r;
#if PGL_NEON_ENABLED
    pgl_neon_mult_m2_v2(&r, m, v);
#else
#ifndef ROW_MAJOR
    r.x = m[0]*v.x + m[2]*v.y;
    r.y = m[1]*v.x + m[3]*v.y;
#else
    r.x = m[0]*v.x + m[1]*v.y;
    r.y = m[2]*v.x + m[3]*v.y;
#endif
#endif
    return r;
}

vec3 mult_m3_v3(mat3 m, vec3 v)
{
    vec3 r;
#if PGL_NEON_ENABLED
    pgl_neon_mult_m3_v3(&r, m, v);
#else
#ifndef ROW_MAJOR
    r.x = m[0]*v.x + m[3]*v.y + m[6]*v.z;
    r.y = m[1]*v.x + m[4]*v.y + m[7]*v.z;
    r.z = m[2]*v.x + m[5]*v.y + m[8]*v.z;
#else
    r.x = m[0]*v.x + m[1]*v.y + m[2]*v.z;
    r.y = m[3]*v.x + m[4]*v.y + m[5]*v.z;
    r.z = m[6]*v.x + m[7]*v.y + m[8]*v.z;
#endif
#endif
    return r;
}

vec4 mult_m4_v4(mat4 m, vec4 v)
{
    vec4 r;
#if PGL_NEON_ENABLED
    pgl_neon_mult_m4_v4(&r, m, v);
#else
#ifndef ROW_MAJOR
    r.x = m[0]*v.x + m[4]*v.y + m[8]*v.z + m[12]*v.w;
    r.y = m[1]*v.x + m[5]*v.y + m[9]*v.z + m[13]*v.w;
    r.z = m[2]*v.x + m[6]*v.y + m[10]*v.z + m[14]*v.w;
    r.w = m[3]*v.x + m[7]*v.y + m[11]*v.z + m[15]*v.w;
#else
    r.x = m[0]*v.x + m[1]*v.y + m[2]*v.z + m[3]*v.w;
    r.y = m[4]*v.x + m[5]*v.y + m[6]*v.z + m[7]*v.w;
    r.z = m[8]*v.x + m[9]*v.y + m[10]*v.z + m[11]*v.w;
    r.w = m[12]*v.x + m[13]*v.y + m[14]*v.z + m[15]*v.w;
#endif
#endif
    return r;
}

void scale_m3(mat3 m, float x, float y, float z)
{
#if PGL_NEON_ENABLED
    pgl_neon_scale_m3(m, x, y, z);
#else
#ifndef ROW_MAJOR
    m[0] = x; m[3] = 0; m[6] = 0;
    m[1] = 0; m[4] = y; m[7] = 0;
    m[2] = 0; m[5] = 0; m[8] = z;
#else
    m[0] = x; m[1] = 0; m[2] = 0;
    m[3] = 0; m[4] = y; m[5] = 0;
    m[6] = 0; m[7] = 0; m[8] = z;
#endif
#endif
}

void scale_m4(mat4 m, float x, float y, float z)
{
#if PGL_NEON_ENABLED
    pgl_neon_scale_m4(m, x, y, z);
#else
#ifndef ROW_MAJOR
    m[ 0] = x; m[ 4] = 0; m[ 8] = 0; m[12] = 0;
    m[ 1] = 0; m[ 5] = y; m[ 9] = 0; m[13] = 0;
    m[ 2] = 0; m[ 6] = 0; m[10] = z; m[14] = 0;
    m[ 3] = 0; m[ 7] = 0; m[11] = 0; m[15] = 1;
#else
    m[ 0] = x; m[ 1] = 0; m[ 2] = 0; m[ 3] = 0;
    m[ 4] = 0; m[ 5] = y; m[ 6] = 0; m[ 7] = 0;
    m[ 8] = 0; m[ 9] = 0; m[10] = z; m[11] = 0;
    m[12] = 0; m[13] = 0; m[14] = 0; m[15] = 1;
#endif
#endif
}

void translation_m4(mat4 m, float x, float y, float z)
{
#if PGL_NEON_ENABLED
    pgl_neon_translation_m4(m, x, y, z);
#else
#ifndef ROW_MAJOR
    m[ 0] = 1; m[ 4] = 0; m[ 8] = 0; m[12] = x;
    m[ 1] = 0; m[ 5] = 1; m[ 9] = 0; m[13] = y;
    m[ 2] = 0; m[ 6] = 0; m[10] = 1; m[14] = z;
    m[ 3] = 0; m[ 7] = 0; m[11] = 0; m[15] = 1;
#else
    m[ 0] = 1; m[ 1] = 0; m[ 2] = 0; m[ 3] = x;
    m[ 4] = 0; m[ 5] = 1; m[ 6] = 0; m[ 7] = y;
    m[ 8] = 0; m[ 9] = 0; m[10] = 1; m[11] = z;
    m[12] = 0; m[13] = 0; m[14] = 0; m[15] = 1;
#endif
#endif
}

void extract_rotation_m4(mat3 dst, mat4 src, int normalize)
{
#if PGL_NEON_ENABLED
    pgl_neon_extract_rotation_m4(dst, src, normalize);
#else
    vec3 tmp;
    if (normalize) {
        tmp.x = M44(src, 0, 0);
        tmp.y = M44(src, 1, 0);
        tmp.z = M44(src, 2, 0);
        normalize_v3(&tmp);

        M33(dst, 0, 0) = tmp.x;
        M33(dst, 1, 0) = tmp.y;
        M33(dst, 2, 0) = tmp.z;

        tmp.x = M44(src, 0, 1);
        tmp.y = M44(src, 1, 1);
        tmp.z = M44(src, 2, 1);
        normalize_v3(&tmp);

        M33(dst, 0, 1) = tmp.x;
        M33(dst, 1, 1) = tmp.y;
        M33(dst, 2, 1) = tmp.z;

        tmp.x = M44(src, 0, 2);
        tmp.y = M44(src, 1, 2);
        tmp.z = M44(src, 2, 2);
        normalize_v3(&tmp);

        M33(dst, 0, 2) = tmp.x;
        M33(dst, 1, 2) = tmp.y;
        M33(dst, 2, 2) = tmp.z;
    } else {
        M33(dst, 0, 0) = M44(src, 0, 0);
        M33(dst, 1, 0) = M44(src, 1, 0);
        M33(dst, 2, 0) = M44(src, 2, 0);

        M33(dst, 0, 1) = M44(src, 0, 1);
        M33(dst, 1, 1) = M44(src, 1, 1);
        M33(dst, 2, 1) = M44(src, 2, 1);

        M33(dst, 0, 2) = M44(src, 0, 2);
        M33(dst, 1, 2) = M44(src, 1, 2);
        M33(dst, 2, 2) = M44(src, 2, 2);
    }
#endif
}

void load_rotation_m2(mat2 mat, float angle)
{
    float s = sin(angle);
    float c = cos(angle);
#if PGL_NEON_ENABLED
    pgl_neon_load_rotation_m2(mat, s, c);
#else
#ifndef ROW_MAJOR
    mat[0] = c;
    mat[2] = -s;

    mat[1] = s;
    mat[3] = c;
#else
    mat[0] = c;
    mat[1] = -s;

    mat[2] = s;
    mat[3] = c;
#endif
#endif
}

void mult_m2_m2(mat2 c, mat2 a, mat2 b)
{
#if PGL_NEON_ENABLED
    pgl_neon_mult_m2_m2(c, a, b);
#else
#ifndef ROW_MAJOR
    c[0] = a[0]*b[0] + a[2]*b[1];
    c[2] = a[0]*b[2] + a[2]*b[3];

    c[1] = a[1]*b[0] + a[3]*b[1];
    c[3] = a[1]*b[2] + a[3]*b[3];
#else
    c[0] = a[0]*b[0] + a[1]*b[2];
    c[1] = a[0]*b[1] + a[1]*b[3];

    c[2] = a[2]*b[0] + a[3]*b[2];
    c[3] = a[2]*b[1] + a[3]*b[3];
#endif
#endif
}

extern inline void load_rotation_m2(mat2 mat, float angle);

void mult_m3_m3(mat3 c, mat3 a, mat3 b)
{
#ifndef ROW_MAJOR
    c[0] = a[0]*b[0] + a[3]*b[1] + a[6]*b[2];
    c[3] = a[0]*b[3] + a[3]*b[4] + a[6]*b[5];
    c[6] = a[0]*b[6] + a[3]*b[7] + a[6]*b[8];

    c[1] = a[1]*b[0] + a[4]*b[1] + a[7]*b[2];
    c[4] = a[1]*b[3] + a[4]*b[4] + a[7]*b[5];
    c[7] = a[1]*b[6] + a[4]*b[7] + a[7]*b[8];

    c[2] = a[2]*b[0] + a[5]*b[1] + a[8]*b[2];
    c[5] = a[2]*b[3] + a[5]*b[4] + a[8]*b[5];
    c[8] = a[2]*b[6] + a[5]*b[7] + a[8]*b[8];
#else
    c[0] = a[0]*b[0] + a[1]*b[3] + a[2]*b[6];
    c[1] = a[0]*b[1] + a[1]*b[4] + a[2]*b[7];
    c[2] = a[0]*b[2] + a[1]*b[5] + a[2]*b[8];

    c[3] = a[3]*b[0] + a[4]*b[3] + a[5]*b[6];
    c[4] = a[3]*b[1] + a[4]*b[4] + a[5]*b[7];
    c[5] = a[3]*b[2] + a[4]*b[5] + a[5]*b[8];

    c[6] = a[6]*b[0] + a[7]*b[3] + a[8]*b[6];
    c[7] = a[6]*b[1] + a[7]*b[4] + a[8]*b[7];
    c[8] = a[6]*b[2] + a[7]*b[5] + a[8]*b[8];
#endif
}

void load_rotation_m3(mat3 mat, vec3 v, float angle)
{
    float s, c;

    s = sin(angle);
    c = cos(angle);

    // Rotation matrix is normalized
    normalize_v3(&v);

#if PGL_NEON_ENABLED
    pgl_neon_load_rotation_m3(mat, v, s, c);
#else
    float xx, yy, zz, xy, yz, zx, xs, ys, zs, one_c;

    xx = v.x * v.x;
    yy = v.y * v.y;
    zz = v.z * v.z;
    xy = v.x * v.y;
    yz = v.y * v.z;
    zx = v.z * v.x;
    xs = v.x * s;
    ys = v.y * s;
    zs = v.z * s;
    one_c = 1.0f - c;

#ifndef ROW_MAJOR
    mat[0] = (one_c * xx) + c;
    mat[3] = (one_c * xy) - zs;
    mat[6] = (one_c * zx) + ys;

    mat[1] = (one_c * xy) + zs;
    mat[4] = (one_c * yy) + c;
    mat[7] = (one_c * yz) - xs;

    mat[2] = (one_c * zx) - ys;
    mat[5] = (one_c * yz) + xs;
    mat[8] = (one_c * zz) + c;
#else
    mat[0] = (one_c * xx) + c;
    mat[1] = (one_c * xy) - zs;
    mat[2] = (one_c * zx) + ys;

    mat[3] = (one_c * xy) + zs;
    mat[4] = (one_c * yy) + c;
    mat[5] = (one_c * yz) - xs;

    mat[6] = (one_c * zx) - ys;
    mat[7] = (one_c * yz) + xs;
    mat[8] = (one_c * zz) + c;
#endif
#endif
}



/*
 * mat4
 */

//TODO use restrict?
void mult_m4_m4(mat4 c, mat4 a, mat4 b)
{
#if PGL_NEON_ENABLED
    pgl_neon_mult_m4_m4(c, a, b);
#else
#ifndef ROW_MAJOR
    c[ 0] = a[0]*b[ 0] + a[4]*b[ 1] + a[8]*b[ 2] + a[12]*b[ 3];
    c[ 4] = a[0]*b[ 4] + a[4]*b[ 5] + a[8]*b[ 6] + a[12]*b[ 7];
    c[ 8] = a[0]*b[ 8] + a[4]*b[ 9] + a[8]*b[10] + a[12]*b[11];
    c[12] = a[0]*b[12] + a[4]*b[13] + a[8]*b[14] + a[12]*b[15];

    c[ 1] = a[1]*b[ 0] + a[5]*b[ 1] + a[9]*b[ 2] + a[13]*b[ 3];
    c[ 5] = a[1]*b[ 4] + a[5]*b[ 5] + a[9]*b[ 6] + a[13]*b[ 7];
    c[ 9] = a[1]*b[ 8] + a[5]*b[ 9] + a[9]*b[10] + a[13]*b[11];
    c[13] = a[1]*b[12] + a[5]*b[13] + a[9]*b[14] + a[13]*b[15];

    c[ 2] = a[2]*b[ 0] + a[6]*b[ 1] + a[10]*b[ 2] + a[14]*b[ 3];
    c[ 6] = a[2]*b[ 4] + a[6]*b[ 5] + a[10]*b[ 6] + a[14]*b[ 7];
    c[10] = a[2]*b[ 8] + a[6]*b[ 9] + a[10]*b[10] + a[14]*b[11];
    c[14] = a[2]*b[12] + a[6]*b[13] + a[10]*b[14] + a[14]*b[15];

    c[ 3] = a[3]*b[ 0] + a[7]*b[ 1] + a[11]*b[ 2] + a[15]*b[ 3];
    c[ 7] = a[3]*b[ 4] + a[7]*b[ 5] + a[11]*b[ 6] + a[15]*b[ 7];
    c[11] = a[3]*b[ 8] + a[7]*b[ 9] + a[11]*b[10] + a[15]*b[11];
    c[15] = a[3]*b[12] + a[7]*b[13] + a[11]*b[14] + a[15]*b[15];

#else
    c[0] = a[0]*b[0] + a[1]*b[4] + a[2]*b[8] + a[3]*b[12];
    c[1] = a[0]*b[1] + a[1]*b[5] + a[2]*b[9] + a[3]*b[13];
    c[2] = a[0]*b[2] + a[1]*b[6] + a[2]*b[10] + a[3]*b[14];
    c[3] = a[0]*b[3] + a[1]*b[7] + a[2]*b[11] + a[3]*b[15];

    c[4] = a[4]*b[0] + a[5]*b[4] + a[6]*b[8] + a[7]*b[12];
    c[5] = a[4]*b[1] + a[5]*b[5] + a[6]*b[9] + a[7]*b[13];
    c[6] = a[4]*b[2] + a[5]*b[6] + a[6]*b[10] + a[7]*b[14];
    c[7] = a[4]*b[3] + a[5]*b[7] + a[6]*b[11] + a[7]*b[15];

    c[ 8] = a[8]*b[0] + a[9]*b[4] + a[10]*b[8] + a[11]*b[12];
    c[ 9] = a[8]*b[1] + a[9]*b[5] + a[10]*b[9] + a[11]*b[13];
    c[10] = a[8]*b[2] + a[9]*b[6] + a[10]*b[10] + a[11]*b[14];
    c[11] = a[8]*b[3] + a[9]*b[7] + a[10]*b[11] + a[11]*b[15];

    c[12] = a[12]*b[0] + a[13]*b[4] + a[14]*b[8] + a[15]*b[12];
    c[13] = a[12]*b[1] + a[13]*b[5] + a[14]*b[9] + a[15]*b[13];
    c[14] = a[12]*b[2] + a[13]*b[6] + a[14]*b[10] + a[15]*b[14];
    c[15] = a[12]*b[3] + a[13]*b[7] + a[14]*b[11] + a[15]*b[15];
#endif
#endif
}

void load_rotation_m4(mat4 mat, vec3 v, float angle)
{
    float s, c;

    s = sin(angle);
    c = cos(angle);

    // Rotation matrix is normalized
    normalize_v3(&v);

#if PGL_NEON_ENABLED
    pgl_neon_load_rotation_m4(mat, v, s, c);
#else
    float xx, yy, zz, xy, yz, zx, xs, ys, zs, one_c;

    xx = v.x * v.x;
    yy = v.y * v.y;
    zz = v.z * v.z;
    xy = v.x * v.y;
    yz = v.y * v.z;
    zx = v.z * v.x;
    xs = v.x * s;
    ys = v.y * s;
    zs = v.z * s;
    one_c = 1.0f - c;

#ifndef ROW_MAJOR
    mat[ 0] = (one_c * xx) + c;
    mat[ 4] = (one_c * xy) - zs;
    mat[ 8] = (one_c * zx) + ys;
    mat[12] = 0.0f;

    mat[ 1] = (one_c * xy) + zs;
    mat[ 5] = (one_c * yy) + c;
    mat[ 9] = (one_c * yz) - xs;
    mat[13] = 0.0f;

    mat[ 2] = (one_c * zx) - ys;
    mat[ 6] = (one_c * yz) + xs;
    mat[10] = (one_c * zz) + c;
    mat[14] = 0.0f;

    mat[ 3] = 0.0f;
    mat[ 7] = 0.0f;
    mat[11] = 0.0f;
    mat[15] = 1.0f;
#else
    mat[0] = (one_c * xx) + c;
    mat[1] = (one_c * xy) - zs;
    mat[2] = (one_c * zx) + ys;
    mat[3] = 0.0f;

    mat[4] = (one_c * xy) + zs;
    mat[5] = (one_c * yy) + c;
    mat[6] = (one_c * yz) - xs;
    mat[7] = 0.0f;

    mat[8] = (one_c * zx) - ys;
    mat[9] = (one_c * yz) + xs;
    mat[10] = (one_c * zz) + c;
    mat[11] = 0.0f;

    mat[12] = 0.0f;
    mat[13] = 0.0f;
    mat[14] = 0.0f;
    mat[15] = 1.0f;
#endif
#endif
}


/* TODO
static float det_ij(const mat4 m, const int i, const int j)
{
    float ret, mat[3][3];
    int x = 0, y = 0;

    for (int ii=0; ii<4; ++ii) {
        y = 0;
        if (ii == i) continue;
        for (int jj=0; jj<4; ++jj) {
            if (jj == j) continue;
            mat[x][y] = m[ii*4+jj];
            y++;
        }
        x++;
    }



    ret =  mat[0][0]*(mat[1][1]*mat[2][2]-mat[2][1]*mat[1][2]);
    ret -= mat[0][1]*(mat[1][0]*mat[2][2]-mat[2][0]*mat[1][2]);
    ret += mat[0][2]*(mat[1][0]*mat[2][1]-mat[2][0]*mat[1][1]);

    return ret;
}


void invert_m4(mat4 mInverse, const mat4& m)
{
    int i, j;
    float det, detij;
    mat4 inverse_mat;

    // calculate 4x4 determinant
    det = 0.0f;
    for (i = 0; i < 4; i++) {
        det += (i & 0x1) ? (-m.matrix[i] * det_ij(m, 0, i)) : (m.matrix[i] * det_ij(m, 0, i));
    }
    det = 1.0f / det;

    // calculate inverse
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            detij = det_ij(m, j, i);
            inverse_mat[(i*4)+j] = ((i+j) & 0x1) ? (-detij * det) : (detij *det);
        }
    }

}


*/


////////////////////////////////////////////////////////////////////////////////////////////

//assumes converting from canonical view volume [-1,1]^3
//works just like glViewport, x and y are lower left corner.  opengl should be 1.
void make_viewport_m4(mat4 mat, int x, int y, unsigned int width, unsigned int height, int opengl)
{
    float w, h, l, t, b, r;

    if (opengl) {
        //See glspec page 104, integer grid is lower left pixel corners
        w = width, h = height;
        l = x, b = y;
        //range is [l, l+w) x [b , b+h)
        //TODO pick best epsilon?
        r = l + w - 0.01; //epsilon larger than float precision
        t = b + h - 0.01;

#ifndef ROW_MAJOR
        mat[ 0] = (r - l) / 2;
        mat[ 4] = 0;
        mat[ 8] = 0;
        mat[12] = (l + r) / 2;

        mat[ 1] = 0;
        //see below
        mat[ 5] = (t - b) / 2;
        mat[ 9] = 0;
        mat[13] = (b + t) / 2;

        mat[ 2] = 0;
        mat[ 6] = 0;
        mat[10] = 1;
        mat[14] = 0;

        mat[ 3] = 0;
        mat[ 7] = 0;
        mat[11] = 0;
        mat[15] = 1;
#else
        mat[0] = (r - l) / 2;
        mat[1] = 0;
        mat[2] = 0;
        mat[3] = (l + r) / 2;

        mat[4] = 0;
        //this used to be negative to flip y till I changed glFramebuffer and draw_pixel to accomplish the same thing
        mat[5] = (t - b) / 2;
        mat[6] = 0;
        mat[7] = (b + t) / 2;

        mat[8] = 0;
        mat[9] = 0;
        mat[10] = 1;
        mat[11] = 0;

        mat[12] = 0;
        mat[13] = 0;
        mat[14] = 0;
        mat[15] = 1;
#endif

    } else {
        //old way with pixel centers at integer coordinates
        //see pages 133/4 and 144 of FoCG
        //necessary for fast integer only bresenham line drawing

        w = width, h = height;
        l = x - 0.5f;
        b = y - 0.5f;
        r = l + w;
        t = b + h;

#ifndef ROW_MAJOR
        mat[ 0] = (r - l) / 2;
        mat[ 4] = 0;
        mat[ 8] = 0;
        mat[12] = (l + r) / 2;

        mat[ 1] = 0;
        //see below
        mat[ 5] = (t - b) / 2;
        mat[ 9] = 0;
        mat[13] = (b + t) / 2;

        mat[ 2] = 0;
        mat[ 6] = 0;
        mat[10] = 1;
        mat[14] = 0;

        mat[ 3] = 0;
        mat[ 7] = 0;
        mat[11] = 0;
        mat[15] = 1;
#else
        mat[0] = (r - l) / 2;
        mat[1] = 0;
        mat[2] = 0;
        mat[3] = (l + r) / 2;

        mat[4] = 0;
        //make this negative to reflect y otherwise positive y maps to lower half of the screen
        //this is mapping the unit square [-1,1]^2 to the window size. x is fine because it increases left to right
        //but the screen coordinates (ie framebuffer memory) increase top to bottom opposite of the canonical square
        //negating this is the easiest way to fix it without any side effects.
        mat[5] = (t - b) / 2;
        mat[6] = 0;
        mat[7] = (b + t) / 2;

        mat[8] = 0;
        mat[9] = 0;
        mat[10] = 1;
        mat[11] = 0;

        mat[12] = 0;
        mat[13] = 0;
        mat[14] = 0;
        mat[15] = 1;
#endif
    }
}

//I can't really think of any reason to ever use this matrix alone.
//You'd always do ortho * pers and really if you're doing perspective projection
//just use make_perspective_matrix (or less likely make perspective_proj_matrix)
//
//This function is really just for completeness sake based off of FoCG 3rd edition pg 152
//changed slightly.  z_near and z_far are always positive and z_near < z_far
//
//Inconsistently, to generate an ortho matrix to multiply with that will get the equivalent
//of the other 2 functions you'd use -z_near and -z_far and near > far.
void make_pers_m4(mat4 mat, float z_near, float z_far)
{
#ifndef ROW_MAJOR
    mat[ 0] = z_near;
    mat[ 4] = 0;
    mat[ 8] = 0;
    mat[12] = 0;

    mat[ 1] = 0;
    mat[ 5] = z_near;
    mat[ 9] = 0;
    mat[13] = 0;

    mat[ 2] = 0;
    mat[ 6] = 0;
    mat[10] = z_near + z_far;
    mat[14] = (z_far * z_near);

    mat[ 3] = 0;
    mat[ 7] = 0;
    mat[11] = -1;
    mat[15] = 0;
#else
    mat[0] = z_near;
    mat[1] = 0;
    mat[2] = 0;
    mat[3] = 0;

    mat[4] = 0;
    mat[5] = z_near;
    mat[6] = 0;
    mat[7] = 0;

    mat[ 8] = 0;
    mat[ 9] = 0;
    mat[10] = z_near + z_far;
    mat[11] = (z_far * z_near);

    mat[12] = 0;
    mat[13] = 0;
    mat[14] = -1;
    mat[15] = 0;
#endif
}

// Create a projection matrix
// Similiar to the old gluPerspective... fov is in radians btw...
void make_perspective_m4(mat4 mat, float fov, float aspect, float n, float f)
{
    float t = n * tanf(fov * 0.5f);
    float b = -t;
    float l = b * aspect;
    float r = -l;

    make_perspective_proj_m4(mat, l, r, b, t, n, f);

}

void make_perspective_proj_m4(mat4 mat, float l, float r, float b, float t, float n, float f)
{
#ifndef ROW_MAJOR
    mat[ 0] = (2.0f * n) / (r - l);
    mat[ 4] = 0.0f;
    mat[ 8] = (r + l) / (r - l);
    mat[12] = 0.0f;

    mat[ 1] = 0.0f;
    mat[ 5] = (2.0f * n) / (t - b);
    mat[ 9] = (t + b) / (t - b);
    mat[13] = 0.0f;

    mat[ 2] = 0.0f;
    mat[ 6] = 0.0f;
    mat[10] = -((f + n) / (f - n));
    mat[14] = -((2.0f * (f*n))/(f - n));

    mat[ 3] = 0.0f;
    mat[ 7] = 0.0f;
    mat[11] = -1.0f;
    mat[15] = 0.0f;
#else
    mat[0] = (2.0f * n) / (r - l);
    mat[1] = 0.0f;
    mat[2] = (r + l) / (r - l);
    mat[3] = 0.0f;

    mat[4] = 0.0f;
    mat[5] = (2.0f * n) / (t - b);
    mat[6] = (t + b) / (t - b);
    mat[7] = 0.0f;

    mat[8] = 0.0f;
    mat[9] = 0.0f;
    mat[10] = -((f + n) / (f - n));
    mat[11] = -((2.0f * (f*n))/(f - n));

    mat[12] = 0.0f;
    mat[13] = 0.0f;
    mat[14] = -1.0f;
    mat[15] = 0.0f;
#endif
}

//n and f really are near and far not min and max so if you want the standard looking down the -z axis
// then n > f otherwise n < f
void make_orthographic_m4(mat4 mat, float l, float r, float b, float t, float n, float f)
{
#ifndef ROW_MAJOR
    mat[ 0] = 2.0f / (r - l);
    mat[ 4] = 0;
    mat[ 8] = 0;
    mat[12] = -((r + l)/(r - l));

    mat[ 1] = 0;
    mat[ 5] = 2.0f / (t - b);
    mat[ 9] = 0;
    mat[13] = -((t + b)/(t - b));

    mat[ 2] = 0;
    mat[ 6] = 0;
    mat[10] = 2.0f / (f - n);  //removed - in front of 2 . . . book doesn't have it but superbible did
    mat[14] = -((n + f)/(f - n));

    mat[ 3] = 0;
    mat[ 7] = 0;
    mat[11] = 0;
    mat[15] = 1;
#else
    mat[0] = 2.0f / (r - l);
    mat[1] = 0;
    mat[2] = 0;
    mat[3] = -((r + l)/(r - l));
    mat[4] = 0;
    mat[5] = 2.0f / (t - b);
    mat[6] = 0;
    mat[7] = -((t + b)/(t - b));
    mat[8] = 0;
    mat[9] = 0;
    mat[10] = 2.0f / (f - n);  //removed - in front of 2 . . . book doesn't have it but superbible did
    mat[11] = -((n + f)/(f - n));
    mat[12] = 0;
    mat[13] = 0;
    mat[14] = 0;
    mat[15] = 1;
#endif


    //now I know why the superbible had the -
    //OpenGL uses a left handed canonical view volume [-1,1]^3 when passed the identity matrix
    //ie in Normalized Device Coordinates.  The math/matrix presented in Fundamentals of Computer
    //Graphics assumes a right handed version of the same volume.  The negative isn't necessary
    //if you set n and f correctly as near and far not low and high
}

//per https://www.opengl.org/sdk/docs/man2/xhtml/gluLookAt.xml
//and glm.g-truc.net (glm/gtc/matrix_transform.inl)
void lookAt(mat4 mat, vec3 eye, vec3 center, vec3 up)
{
    SET_IDENTITY_M4(mat);

    vec3 f = norm_v3(sub_v3s(center, eye));
    vec3 s = norm_v3(cross_v3s(f, up));
    vec3 u = cross_v3s(s, f);

    setx_m4v3(mat, s);
    sety_m4v3(mat, u);
    setz_m4v3(mat, neg_v3(f));
    setc4_m4v3(mat, make_v3(-dot_v3s(s, eye), -dot_v3s(u, eye), dot_v3s(f, eye)));
}

extern inline float rsw_randf(void);
extern inline float rsw_randf_range(float min, float max);
extern inline double rsw_map(double x, double a, double b, double c, double d);
extern inline float rsw_mapf(float x, float a, float b, float c, float d);

extern inline Color make_Color(u8 red, u8 green, u8 blue, u8 alpha);
extern inline Color v4_to_Color(vec4 v);
extern inline void print_Color(Color c, const char* append);
extern inline vec4 Color_to_v4(Color c);
extern inline Line make_Line(float x1, float y1, float x2, float y2);
extern inline void normalize_line(Line* line);
extern inline float line_func(Line* line, float x, float y);
extern inline float line_findy(Line* line, float x);
extern inline float line_findx(Line* line, float y);
extern inline float sq_dist_pt_segment2d(vec2 a, vec2 b, vec2 c);
extern inline void closest_pt_pt_segment(vec2 c, vec2 a, vec2 b, float* t, vec2* d);
extern inline float closest_pt_pt_segment_t(vec2 c, vec2 a, vec2 b);


#if defined(CVEC_MALLOC) && defined(CVEC_FREE) && defined(CVEC_REALLOC)
/* ok */
#elif !defined(CVEC_MALLOC) && !defined(CVEC_FREE) && !defined(CVEC_REALLOC)
/* ok */
#else
#error "Must define all or none of CVEC_MALLOC, CVEC_FREE, and CVEC_REALLOC."
#endif

#ifndef CVEC_MALLOC
#include <stdlib.h>
#define CVEC_MALLOC(sz)      malloc(sz)
#define CVEC_REALLOC(p, sz)  realloc(p, sz)
#define CVEC_FREE(p)         free(p)
#endif

#ifndef CVEC_MEMMOVE
#include <string.h>
#define CVEC_MEMMOVE(dst, src, sz)  memmove(dst, src, sz)
#endif

#ifndef CVEC_ASSERT
#include <assert.h>
#define CVEC_ASSERT(x)       assert(x)
#endif

cvec_sz CVEC_glVertex_Array_SZ = 50;

#define CVEC_glVertex_Array_ALLOCATOR(x) ((x+1) * 2)

cvector_glVertex_Array* cvec_glVertex_Array_heap(cvec_sz size, cvec_sz capacity)
{
    cvector_glVertex_Array* vec;
    if (!(vec = (cvector_glVertex_Array*)CVEC_MALLOC(sizeof(cvector_glVertex_Array)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glVertex_Array_SZ;

    if (!(vec->a = (glVertex_Array*)CVEC_MALLOC(vec->capacity*sizeof(glVertex_Array)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    return vec;
}

cvector_glVertex_Array* cvec_init_glVertex_Array_heap(glVertex_Array* vals, cvec_sz num)
{
    cvector_glVertex_Array* vec;
    
    if (!(vec = (cvector_glVertex_Array*)CVEC_MALLOC(sizeof(cvector_glVertex_Array)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->capacity = num + CVEC_glVertex_Array_SZ;
    vec->size = num;
    if (!(vec->a = (glVertex_Array*)CVEC_MALLOC(vec->capacity*sizeof(glVertex_Array)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glVertex_Array)*num);

    return vec;
}

int cvec_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz size, cvec_sz capacity)
{
    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glVertex_Array_SZ;

    if (!(vec->a = (glVertex_Array*)CVEC_MALLOC(vec->capacity*sizeof(glVertex_Array)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    return 1;
}

int cvec_init_glVertex_Array(cvector_glVertex_Array* vec, glVertex_Array* vals, cvec_sz num)
{
    vec->capacity = num + CVEC_glVertex_Array_SZ;
    vec->size = num;
    if (!(vec->a = (glVertex_Array*)CVEC_MALLOC(vec->capacity*sizeof(glVertex_Array)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glVertex_Array)*num);

    return 1;
}

int cvec_copyc_glVertex_Array(void* dest, void* src)
{
    cvector_glVertex_Array* vec1 = (cvector_glVertex_Array*)dest;
    cvector_glVertex_Array* vec2 = (cvector_glVertex_Array*)src;

    vec1->a = NULL;
    vec1->size = 0;
    vec1->capacity = 0;

    return cvec_copy_glVertex_Array(vec1, vec2);
}

int cvec_copy_glVertex_Array(cvector_glVertex_Array* dest, cvector_glVertex_Array* src)
{
    glVertex_Array* tmp = NULL;
    if (!(tmp = (glVertex_Array*)CVEC_REALLOC(dest->a, src->capacity*sizeof(glVertex_Array)))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    dest->a = tmp;

    CVEC_MEMMOVE(dest->a, src->a, src->size*sizeof(glVertex_Array));
    dest->size = src->size;
    dest->capacity = src->capacity;
    return 1;
}


int cvec_push_glVertex_Array(cvector_glVertex_Array* vec, glVertex_Array a)
{
    glVertex_Array* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        vec->a[vec->size++] = a;
    } else {
        tmp_sz = CVEC_glVertex_Array_ALLOCATOR(vec->capacity);
        if (!(tmp = (glVertex_Array*)CVEC_REALLOC(vec->a, sizeof(glVertex_Array)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->a[vec->size++] = a;
        vec->capacity = tmp_sz;
    }
    return 1;
}

glVertex_Array cvec_pop_glVertex_Array(cvector_glVertex_Array* vec)
{
    return vec->a[--vec->size];
}

glVertex_Array* cvec_back_glVertex_Array(cvector_glVertex_Array* vec)
{
    return &vec->a[vec->size-1];
}

int cvec_extend_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz num)
{
    glVertex_Array* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glVertex_Array_SZ;
        if (!(tmp = (glVertex_Array*)CVEC_REALLOC(vec->a, sizeof(glVertex_Array)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    vec->size += num;
    return 1;
}

int cvec_insert_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz i, glVertex_Array a)
{
    glVertex_Array* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glVertex_Array));
        vec->a[i] = a;
    } else {
        tmp_sz = CVEC_glVertex_Array_ALLOCATOR(vec->capacity);
        if (!(tmp = (glVertex_Array*)CVEC_REALLOC(vec->a, sizeof(glVertex_Array)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glVertex_Array));
        vec->a[i] = a;
        vec->capacity = tmp_sz;
    }

    vec->size++;
    return 1;
}

int cvec_insert_array_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz i, glVertex_Array* a, cvec_sz num)
{
    glVertex_Array* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glVertex_Array_SZ;
        if (!(tmp = (glVertex_Array*)CVEC_REALLOC(vec->a, sizeof(glVertex_Array)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    CVEC_MEMMOVE(&vec->a[i+num], &vec->a[i], (vec->size-i)*sizeof(glVertex_Array));
    CVEC_MEMMOVE(&vec->a[i], a, num*sizeof(glVertex_Array));
    vec->size += num;
    return 1;
}

glVertex_Array cvec_replace_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz i, glVertex_Array a)
{
    glVertex_Array tmp = vec->a[i];
    vec->a[i] = a;
    return tmp;
}

void cvec_erase_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz start, cvec_sz end)
{
    cvec_sz d = end - start + 1;
    CVEC_MEMMOVE(&vec->a[start], &vec->a[end+1], (vec->size-1-end)*sizeof(glVertex_Array));
    vec->size -= d;
}


int cvec_reserve_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz size)
{
    glVertex_Array* tmp;
    if (vec->capacity < size) {
        if (!(tmp = (glVertex_Array*)CVEC_REALLOC(vec->a, sizeof(glVertex_Array)*(size+CVEC_glVertex_Array_SZ)))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = size + CVEC_glVertex_Array_SZ;
    }
    return 1;
}

int cvec_set_cap_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz size)
{
    glVertex_Array* tmp;
    if (size < vec->size) {
        vec->size = size;
    }

    if (!(tmp = (glVertex_Array*)CVEC_REALLOC(vec->a, sizeof(glVertex_Array)*size))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    vec->a = tmp;
    vec->capacity = size;
    return 1;
}

void cvec_set_val_sz_glVertex_Array(cvector_glVertex_Array* vec, glVertex_Array val)
{
    cvec_sz i;
    for (i=0; i<vec->size; i++) {
        vec->a[i] = val;
    }
}

void cvec_set_val_cap_glVertex_Array(cvector_glVertex_Array* vec, glVertex_Array val)
{
    cvec_sz i;
    for (i=0; i<vec->capacity; i++) {
        vec->a[i] = val;
    }
}

void cvec_clear_glVertex_Array(cvector_glVertex_Array* vec) { vec->size = 0; }

void cvec_free_glVertex_Array_heap(void* vec)
{
    cvector_glVertex_Array* tmp = (cvector_glVertex_Array*)vec;
    if (!tmp) return;
    CVEC_FREE(tmp->a);
    CVEC_FREE(tmp);
}

void cvec_free_glVertex_Array(void* vec)
{
    cvector_glVertex_Array* tmp = (cvector_glVertex_Array*)vec;
    CVEC_FREE(tmp->a);
    tmp->size = 0;
    tmp->capacity = 0;
}


cvec_sz CVEC_glBuffer_SZ = 50;

#define CVEC_glBuffer_ALLOCATOR(x) ((x+1) * 2)

cvector_glBuffer* cvec_glBuffer_heap(cvec_sz size, cvec_sz capacity)
{
    cvector_glBuffer* vec;
    if (!(vec = (cvector_glBuffer*)CVEC_MALLOC(sizeof(cvector_glBuffer)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glBuffer_SZ;

    if (!(vec->a = (glBuffer*)CVEC_MALLOC(vec->capacity*sizeof(glBuffer)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    return vec;
}

cvector_glBuffer* cvec_init_glBuffer_heap(glBuffer* vals, cvec_sz num)
{
    cvector_glBuffer* vec;
    
    if (!(vec = (cvector_glBuffer*)CVEC_MALLOC(sizeof(cvector_glBuffer)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->capacity = num + CVEC_glBuffer_SZ;
    vec->size = num;
    if (!(vec->a = (glBuffer*)CVEC_MALLOC(vec->capacity*sizeof(glBuffer)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glBuffer)*num);

    return vec;
}

int cvec_glBuffer(cvector_glBuffer* vec, cvec_sz size, cvec_sz capacity)
{
    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glBuffer_SZ;

    if (!(vec->a = (glBuffer*)CVEC_MALLOC(vec->capacity*sizeof(glBuffer)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    return 1;
}

int cvec_init_glBuffer(cvector_glBuffer* vec, glBuffer* vals, cvec_sz num)
{
    vec->capacity = num + CVEC_glBuffer_SZ;
    vec->size = num;
    if (!(vec->a = (glBuffer*)CVEC_MALLOC(vec->capacity*sizeof(glBuffer)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glBuffer)*num);

    return 1;
}

int cvec_copyc_glBuffer(void* dest, void* src)
{
    cvector_glBuffer* vec1 = (cvector_glBuffer*)dest;
    cvector_glBuffer* vec2 = (cvector_glBuffer*)src;

    vec1->a = NULL;
    vec1->size = 0;
    vec1->capacity = 0;

    return cvec_copy_glBuffer(vec1, vec2);
}

int cvec_copy_glBuffer(cvector_glBuffer* dest, cvector_glBuffer* src)
{
    glBuffer* tmp = NULL;
    if (!(tmp = (glBuffer*)CVEC_REALLOC(dest->a, src->capacity*sizeof(glBuffer)))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    dest->a = tmp;

    CVEC_MEMMOVE(dest->a, src->a, src->size*sizeof(glBuffer));
    dest->size = src->size;
    dest->capacity = src->capacity;
    return 1;
}


int cvec_push_glBuffer(cvector_glBuffer* vec, glBuffer a)
{
    glBuffer* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        vec->a[vec->size++] = a;
    } else {
        tmp_sz = CVEC_glBuffer_ALLOCATOR(vec->capacity);
        if (!(tmp = (glBuffer*)CVEC_REALLOC(vec->a, sizeof(glBuffer)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->a[vec->size++] = a;
        vec->capacity = tmp_sz;
    }
    return 1;
}

glBuffer cvec_pop_glBuffer(cvector_glBuffer* vec)
{
    return vec->a[--vec->size];
}

glBuffer* cvec_back_glBuffer(cvector_glBuffer* vec)
{
    return &vec->a[vec->size-1];
}

int cvec_extend_glBuffer(cvector_glBuffer* vec, cvec_sz num)
{
    glBuffer* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glBuffer_SZ;
        if (!(tmp = (glBuffer*)CVEC_REALLOC(vec->a, sizeof(glBuffer)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    vec->size += num;
    return 1;
}

int cvec_insert_glBuffer(cvector_glBuffer* vec, cvec_sz i, glBuffer a)
{
    glBuffer* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glBuffer));
        vec->a[i] = a;
    } else {
        tmp_sz = CVEC_glBuffer_ALLOCATOR(vec->capacity);
        if (!(tmp = (glBuffer*)CVEC_REALLOC(vec->a, sizeof(glBuffer)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glBuffer));
        vec->a[i] = a;
        vec->capacity = tmp_sz;
    }

    vec->size++;
    return 1;
}

int cvec_insert_array_glBuffer(cvector_glBuffer* vec, cvec_sz i, glBuffer* a, cvec_sz num)
{
    glBuffer* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glBuffer_SZ;
        if (!(tmp = (glBuffer*)CVEC_REALLOC(vec->a, sizeof(glBuffer)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    CVEC_MEMMOVE(&vec->a[i+num], &vec->a[i], (vec->size-i)*sizeof(glBuffer));
    CVEC_MEMMOVE(&vec->a[i], a, num*sizeof(glBuffer));
    vec->size += num;
    return 1;
}

glBuffer cvec_replace_glBuffer(cvector_glBuffer* vec, cvec_sz i, glBuffer a)
{
    glBuffer tmp = vec->a[i];
    vec->a[i] = a;
    return tmp;
}

void cvec_erase_glBuffer(cvector_glBuffer* vec, cvec_sz start, cvec_sz end)
{
    cvec_sz d = end - start + 1;
    CVEC_MEMMOVE(&vec->a[start], &vec->a[end+1], (vec->size-1-end)*sizeof(glBuffer));
    vec->size -= d;
}


int cvec_reserve_glBuffer(cvector_glBuffer* vec, cvec_sz size)
{
    glBuffer* tmp;
    if (vec->capacity < size) {
        if (!(tmp = (glBuffer*)CVEC_REALLOC(vec->a, sizeof(glBuffer)*(size+CVEC_glBuffer_SZ)))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = size + CVEC_glBuffer_SZ;
    }
    return 1;
}

int cvec_set_cap_glBuffer(cvector_glBuffer* vec, cvec_sz size)
{
    glBuffer* tmp;
    if (size < vec->size) {
        vec->size = size;
    }

    if (!(tmp = (glBuffer*)CVEC_REALLOC(vec->a, sizeof(glBuffer)*size))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    vec->a = tmp;
    vec->capacity = size;
    return 1;
}

void cvec_set_val_sz_glBuffer(cvector_glBuffer* vec, glBuffer val)
{
    cvec_sz i;
    for (i=0; i<vec->size; i++) {
        vec->a[i] = val;
    }
}

void cvec_set_val_cap_glBuffer(cvector_glBuffer* vec, glBuffer val)
{
    cvec_sz i;
    for (i=0; i<vec->capacity; i++) {
        vec->a[i] = val;
    }
}

void cvec_clear_glBuffer(cvector_glBuffer* vec) { vec->size = 0; }

void cvec_free_glBuffer_heap(void* vec)
{
    cvector_glBuffer* tmp = (cvector_glBuffer*)vec;
    if (!tmp) return;
    CVEC_FREE(tmp->a);
    CVEC_FREE(tmp);
}

void cvec_free_glBuffer(void* vec)
{
    cvector_glBuffer* tmp = (cvector_glBuffer*)vec;
    CVEC_FREE(tmp->a);
    tmp->size = 0;
    tmp->capacity = 0;
}


cvec_sz CVEC_glTexture_SZ = 50;

#define CVEC_glTexture_ALLOCATOR(x) ((x+1) * 2)

cvector_glTexture* cvec_glTexture_heap(cvec_sz size, cvec_sz capacity)
{
    cvector_glTexture* vec;
    if (!(vec = (cvector_glTexture*)CVEC_MALLOC(sizeof(cvector_glTexture)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glTexture_SZ;

    if (!(vec->a = (glTexture*)CVEC_MALLOC(vec->capacity*sizeof(glTexture)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    return vec;
}

cvector_glTexture* cvec_init_glTexture_heap(glTexture* vals, cvec_sz num)
{
    cvector_glTexture* vec;
    
    if (!(vec = (cvector_glTexture*)CVEC_MALLOC(sizeof(cvector_glTexture)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->capacity = num + CVEC_glTexture_SZ;
    vec->size = num;
    if (!(vec->a = (glTexture*)CVEC_MALLOC(vec->capacity*sizeof(glTexture)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glTexture)*num);

    return vec;
}

int cvec_glTexture(cvector_glTexture* vec, cvec_sz size, cvec_sz capacity)
{
    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glTexture_SZ;

    if (!(vec->a = (glTexture*)CVEC_MALLOC(vec->capacity*sizeof(glTexture)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    return 1;
}

int cvec_init_glTexture(cvector_glTexture* vec, glTexture* vals, cvec_sz num)
{
    vec->capacity = num + CVEC_glTexture_SZ;
    vec->size = num;
    if (!(vec->a = (glTexture*)CVEC_MALLOC(vec->capacity*sizeof(glTexture)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glTexture)*num);

    return 1;
}

int cvec_copyc_glTexture(void* dest, void* src)
{
    cvector_glTexture* vec1 = (cvector_glTexture*)dest;
    cvector_glTexture* vec2 = (cvector_glTexture*)src;

    vec1->a = NULL;
    vec1->size = 0;
    vec1->capacity = 0;

    return cvec_copy_glTexture(vec1, vec2);
}

int cvec_copy_glTexture(cvector_glTexture* dest, cvector_glTexture* src)
{
    glTexture* tmp = NULL;
    if (!(tmp = (glTexture*)CVEC_REALLOC(dest->a, src->capacity*sizeof(glTexture)))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    dest->a = tmp;

    CVEC_MEMMOVE(dest->a, src->a, src->size*sizeof(glTexture));
    dest->size = src->size;
    dest->capacity = src->capacity;
    return 1;
}


int cvec_push_glTexture(cvector_glTexture* vec, glTexture a)
{
    glTexture* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        vec->a[vec->size++] = a;
    } else {
        tmp_sz = CVEC_glTexture_ALLOCATOR(vec->capacity);
        if (!(tmp = (glTexture*)CVEC_REALLOC(vec->a, sizeof(glTexture)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->a[vec->size++] = a;
        vec->capacity = tmp_sz;
    }
    return 1;
}

glTexture cvec_pop_glTexture(cvector_glTexture* vec)
{
    return vec->a[--vec->size];
}

glTexture* cvec_back_glTexture(cvector_glTexture* vec)
{
    return &vec->a[vec->size-1];
}

int cvec_extend_glTexture(cvector_glTexture* vec, cvec_sz num)
{
    glTexture* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glTexture_SZ;
        if (!(tmp = (glTexture*)CVEC_REALLOC(vec->a, sizeof(glTexture)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    vec->size += num;
    return 1;
}

int cvec_insert_glTexture(cvector_glTexture* vec, cvec_sz i, glTexture a)
{
    glTexture* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glTexture));
        vec->a[i] = a;
    } else {
        tmp_sz = CVEC_glTexture_ALLOCATOR(vec->capacity);
        if (!(tmp = (glTexture*)CVEC_REALLOC(vec->a, sizeof(glTexture)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glTexture));
        vec->a[i] = a;
        vec->capacity = tmp_sz;
    }

    vec->size++;
    return 1;
}

int cvec_insert_array_glTexture(cvector_glTexture* vec, cvec_sz i, glTexture* a, cvec_sz num)
{
    glTexture* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glTexture_SZ;
        if (!(tmp = (glTexture*)CVEC_REALLOC(vec->a, sizeof(glTexture)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    CVEC_MEMMOVE(&vec->a[i+num], &vec->a[i], (vec->size-i)*sizeof(glTexture));
    CVEC_MEMMOVE(&vec->a[i], a, num*sizeof(glTexture));
    vec->size += num;
    return 1;
}

glTexture cvec_replace_glTexture(cvector_glTexture* vec, cvec_sz i, glTexture a)
{
    glTexture tmp = vec->a[i];
    vec->a[i] = a;
    return tmp;
}

void cvec_erase_glTexture(cvector_glTexture* vec, cvec_sz start, cvec_sz end)
{
    cvec_sz d = end - start + 1;
    CVEC_MEMMOVE(&vec->a[start], &vec->a[end+1], (vec->size-1-end)*sizeof(glTexture));
    vec->size -= d;
}


int cvec_reserve_glTexture(cvector_glTexture* vec, cvec_sz size)
{
    glTexture* tmp;
    if (vec->capacity < size) {
        if (!(tmp = (glTexture*)CVEC_REALLOC(vec->a, sizeof(glTexture)*(size+CVEC_glTexture_SZ)))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = size + CVEC_glTexture_SZ;
    }
    return 1;
}

int cvec_set_cap_glTexture(cvector_glTexture* vec, cvec_sz size)
{
    glTexture* tmp;
    if (size < vec->size) {
        vec->size = size;
    }

    if (!(tmp = (glTexture*)CVEC_REALLOC(vec->a, sizeof(glTexture)*size))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    vec->a = tmp;
    vec->capacity = size;
    return 1;
}

void cvec_set_val_sz_glTexture(cvector_glTexture* vec, glTexture val)
{
    cvec_sz i;
    for (i=0; i<vec->size; i++) {
        vec->a[i] = val;
    }
}

void cvec_set_val_cap_glTexture(cvector_glTexture* vec, glTexture val)
{
    cvec_sz i;
    for (i=0; i<vec->capacity; i++) {
        vec->a[i] = val;
    }
}

void cvec_clear_glTexture(cvector_glTexture* vec) { vec->size = 0; }

void cvec_free_glTexture_heap(void* vec)
{
    cvector_glTexture* tmp = (cvector_glTexture*)vec;
    if (!tmp) return;
    CVEC_FREE(tmp->a);
    CVEC_FREE(tmp);
}

void cvec_free_glTexture(void* vec)
{
    cvector_glTexture* tmp = (cvector_glTexture*)vec;
    CVEC_FREE(tmp->a);
    tmp->size = 0;
    tmp->capacity = 0;
}


cvec_sz CVEC_glProgram_SZ = 50;

#define CVEC_glProgram_ALLOCATOR(x) ((x+1) * 2)

cvector_glProgram* cvec_glProgram_heap(cvec_sz size, cvec_sz capacity)
{
    cvector_glProgram* vec;
    if (!(vec = (cvector_glProgram*)CVEC_MALLOC(sizeof(cvector_glProgram)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glProgram_SZ;

    if (!(vec->a = (glProgram*)CVEC_MALLOC(vec->capacity*sizeof(glProgram)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    return vec;
}

cvector_glProgram* cvec_init_glProgram_heap(glProgram* vals, cvec_sz num)
{
    cvector_glProgram* vec;
    
    if (!(vec = (cvector_glProgram*)CVEC_MALLOC(sizeof(cvector_glProgram)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->capacity = num + CVEC_glProgram_SZ;
    vec->size = num;
    if (!(vec->a = (glProgram*)CVEC_MALLOC(vec->capacity*sizeof(glProgram)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glProgram)*num);

    return vec;
}

int cvec_glProgram(cvector_glProgram* vec, cvec_sz size, cvec_sz capacity)
{
    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glProgram_SZ;

    if (!(vec->a = (glProgram*)CVEC_MALLOC(vec->capacity*sizeof(glProgram)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    return 1;
}

int cvec_init_glProgram(cvector_glProgram* vec, glProgram* vals, cvec_sz num)
{
    vec->capacity = num + CVEC_glProgram_SZ;
    vec->size = num;
    if (!(vec->a = (glProgram*)CVEC_MALLOC(vec->capacity*sizeof(glProgram)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glProgram)*num);

    return 1;
}

int cvec_copyc_glProgram(void* dest, void* src)
{
    cvector_glProgram* vec1 = (cvector_glProgram*)dest;
    cvector_glProgram* vec2 = (cvector_glProgram*)src;

    vec1->a = NULL;
    vec1->size = 0;
    vec1->capacity = 0;

    return cvec_copy_glProgram(vec1, vec2);
}

int cvec_copy_glProgram(cvector_glProgram* dest, cvector_glProgram* src)
{
    glProgram* tmp = NULL;
    if (!(tmp = (glProgram*)CVEC_REALLOC(dest->a, src->capacity*sizeof(glProgram)))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    dest->a = tmp;

    CVEC_MEMMOVE(dest->a, src->a, src->size*sizeof(glProgram));
    dest->size = src->size;
    dest->capacity = src->capacity;
    return 1;
}


int cvec_push_glProgram(cvector_glProgram* vec, glProgram a)
{
    glProgram* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        vec->a[vec->size++] = a;
    } else {
        tmp_sz = CVEC_glProgram_ALLOCATOR(vec->capacity);
        if (!(tmp = (glProgram*)CVEC_REALLOC(vec->a, sizeof(glProgram)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->a[vec->size++] = a;
        vec->capacity = tmp_sz;
    }
    return 1;
}

glProgram cvec_pop_glProgram(cvector_glProgram* vec)
{
    return vec->a[--vec->size];
}

glProgram* cvec_back_glProgram(cvector_glProgram* vec)
{
    return &vec->a[vec->size-1];
}

int cvec_extend_glProgram(cvector_glProgram* vec, cvec_sz num)
{
    glProgram* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glProgram_SZ;
        if (!(tmp = (glProgram*)CVEC_REALLOC(vec->a, sizeof(glProgram)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    vec->size += num;
    return 1;
}

int cvec_insert_glProgram(cvector_glProgram* vec, cvec_sz i, glProgram a)
{
    glProgram* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glProgram));
        vec->a[i] = a;
    } else {
        tmp_sz = CVEC_glProgram_ALLOCATOR(vec->capacity);
        if (!(tmp = (glProgram*)CVEC_REALLOC(vec->a, sizeof(glProgram)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glProgram));
        vec->a[i] = a;
        vec->capacity = tmp_sz;
    }

    vec->size++;
    return 1;
}

int cvec_insert_array_glProgram(cvector_glProgram* vec, cvec_sz i, glProgram* a, cvec_sz num)
{
    glProgram* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glProgram_SZ;
        if (!(tmp = (glProgram*)CVEC_REALLOC(vec->a, sizeof(glProgram)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    CVEC_MEMMOVE(&vec->a[i+num], &vec->a[i], (vec->size-i)*sizeof(glProgram));
    CVEC_MEMMOVE(&vec->a[i], a, num*sizeof(glProgram));
    vec->size += num;
    return 1;
}

glProgram cvec_replace_glProgram(cvector_glProgram* vec, cvec_sz i, glProgram a)
{
    glProgram tmp = vec->a[i];
    vec->a[i] = a;
    return tmp;
}

void cvec_erase_glProgram(cvector_glProgram* vec, cvec_sz start, cvec_sz end)
{
    cvec_sz d = end - start + 1;
    CVEC_MEMMOVE(&vec->a[start], &vec->a[end+1], (vec->size-1-end)*sizeof(glProgram));
    vec->size -= d;
}


int cvec_reserve_glProgram(cvector_glProgram* vec, cvec_sz size)
{
    glProgram* tmp;
    if (vec->capacity < size) {
        if (!(tmp = (glProgram*)CVEC_REALLOC(vec->a, sizeof(glProgram)*(size+CVEC_glProgram_SZ)))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = size + CVEC_glProgram_SZ;
    }
    return 1;
}

int cvec_set_cap_glProgram(cvector_glProgram* vec, cvec_sz size)
{
    glProgram* tmp;
    if (size < vec->size) {
        vec->size = size;
    }

    if (!(tmp = (glProgram*)CVEC_REALLOC(vec->a, sizeof(glProgram)*size))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    vec->a = tmp;
    vec->capacity = size;
    return 1;
}

void cvec_set_val_sz_glProgram(cvector_glProgram* vec, glProgram val)
{
    cvec_sz i;
    for (i=0; i<vec->size; i++) {
        vec->a[i] = val;
    }
}

void cvec_set_val_cap_glProgram(cvector_glProgram* vec, glProgram val)
{
    cvec_sz i;
    for (i=0; i<vec->capacity; i++) {
        vec->a[i] = val;
    }
}

void cvec_clear_glProgram(cvector_glProgram* vec) { vec->size = 0; }

void cvec_free_glProgram_heap(void* vec)
{
    cvector_glProgram* tmp = (cvector_glProgram*)vec;
    if (!tmp) return;
    CVEC_FREE(tmp->a);
    CVEC_FREE(tmp);
}

void cvec_free_glProgram(void* vec)
{
    cvector_glProgram* tmp = (cvector_glProgram*)vec;
    CVEC_FREE(tmp->a);
    tmp->size = 0;
    tmp->capacity = 0;
}


cvec_sz CVEC_glVertex_SZ = 50;

#define CVEC_glVertex_ALLOCATOR(x) ((x+1) * 2)

cvector_glVertex* cvec_glVertex_heap(cvec_sz size, cvec_sz capacity)
{
    cvector_glVertex* vec;
    if (!(vec = (cvector_glVertex*)CVEC_MALLOC(sizeof(cvector_glVertex)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glVertex_SZ;

    if (!(vec->a = (glVertex*)CVEC_MALLOC(vec->capacity*sizeof(glVertex)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    return vec;
}

cvector_glVertex* cvec_init_glVertex_heap(glVertex* vals, cvec_sz num)
{
    cvector_glVertex* vec;
    
    if (!(vec = (cvector_glVertex*)CVEC_MALLOC(sizeof(cvector_glVertex)))) {
        CVEC_ASSERT(vec != NULL);
        return NULL;
    }

    vec->capacity = num + CVEC_glVertex_SZ;
    vec->size = num;
    if (!(vec->a = (glVertex*)CVEC_MALLOC(vec->capacity*sizeof(glVertex)))) {
        CVEC_ASSERT(vec->a != NULL);
        CVEC_FREE(vec);
        return NULL;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glVertex)*num);

    return vec;
}

int cvec_glVertex(cvector_glVertex* vec, cvec_sz size, cvec_sz capacity)
{
    vec->size = size;
    vec->capacity = (capacity > vec->size || (vec->size && capacity == vec->size)) ? capacity : vec->size + CVEC_glVertex_SZ;

    if (!(vec->a = (glVertex*)CVEC_MALLOC(vec->capacity*sizeof(glVertex)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    return 1;
}

int cvec_init_glVertex(cvector_glVertex* vec, glVertex* vals, cvec_sz num)
{
    vec->capacity = num + CVEC_glVertex_SZ;
    vec->size = num;
    if (!(vec->a = (glVertex*)CVEC_MALLOC(vec->capacity*sizeof(glVertex)))) {
        CVEC_ASSERT(vec->a != NULL);
        vec->size = vec->capacity = 0;
        return 0;
    }

    CVEC_MEMMOVE(vec->a, vals, sizeof(glVertex)*num);

    return 1;
}

int cvec_copyc_glVertex(void* dest, void* src)
{
    cvector_glVertex* vec1 = (cvector_glVertex*)dest;
    cvector_glVertex* vec2 = (cvector_glVertex*)src;

    vec1->a = NULL;
    vec1->size = 0;
    vec1->capacity = 0;

    return cvec_copy_glVertex(vec1, vec2);
}

int cvec_copy_glVertex(cvector_glVertex* dest, cvector_glVertex* src)
{
    glVertex* tmp = NULL;
    if (!(tmp = (glVertex*)CVEC_REALLOC(dest->a, src->capacity*sizeof(glVertex)))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    dest->a = tmp;

    CVEC_MEMMOVE(dest->a, src->a, src->size*sizeof(glVertex));
    dest->size = src->size;
    dest->capacity = src->capacity;
    return 1;
}


int cvec_push_glVertex(cvector_glVertex* vec, glVertex a)
{
    glVertex* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        vec->a[vec->size++] = a;
    } else {
        tmp_sz = CVEC_glVertex_ALLOCATOR(vec->capacity);
        if (!(tmp = (glVertex*)CVEC_REALLOC(vec->a, sizeof(glVertex)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->a[vec->size++] = a;
        vec->capacity = tmp_sz;
    }
    return 1;
}

glVertex cvec_pop_glVertex(cvector_glVertex* vec)
{
    return vec->a[--vec->size];
}

glVertex* cvec_back_glVertex(cvector_glVertex* vec)
{
    return &vec->a[vec->size-1];
}

int cvec_extend_glVertex(cvector_glVertex* vec, cvec_sz num)
{
    glVertex* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glVertex_SZ;
        if (!(tmp = (glVertex*)CVEC_REALLOC(vec->a, sizeof(glVertex)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    vec->size += num;
    return 1;
}

int cvec_insert_glVertex(cvector_glVertex* vec, cvec_sz i, glVertex a)
{
    glVertex* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity > vec->size) {
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glVertex));
        vec->a[i] = a;
    } else {
        tmp_sz = CVEC_glVertex_ALLOCATOR(vec->capacity);
        if (!(tmp = (glVertex*)CVEC_REALLOC(vec->a, sizeof(glVertex)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        CVEC_MEMMOVE(&vec->a[i+1], &vec->a[i], (vec->size-i)*sizeof(glVertex));
        vec->a[i] = a;
        vec->capacity = tmp_sz;
    }

    vec->size++;
    return 1;
}

int cvec_insert_array_glVertex(cvector_glVertex* vec, cvec_sz i, glVertex* a, cvec_sz num)
{
    glVertex* tmp;
    cvec_sz tmp_sz;
    if (vec->capacity < vec->size + num) {
        tmp_sz = vec->capacity + num + CVEC_glVertex_SZ;
        if (!(tmp = (glVertex*)CVEC_REALLOC(vec->a, sizeof(glVertex)*tmp_sz))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = tmp_sz;
    }

    CVEC_MEMMOVE(&vec->a[i+num], &vec->a[i], (vec->size-i)*sizeof(glVertex));
    CVEC_MEMMOVE(&vec->a[i], a, num*sizeof(glVertex));
    vec->size += num;
    return 1;
}

glVertex cvec_replace_glVertex(cvector_glVertex* vec, cvec_sz i, glVertex a)
{
    glVertex tmp = vec->a[i];
    vec->a[i] = a;
    return tmp;
}

void cvec_erase_glVertex(cvector_glVertex* vec, cvec_sz start, cvec_sz end)
{
    cvec_sz d = end - start + 1;
    CVEC_MEMMOVE(&vec->a[start], &vec->a[end+1], (vec->size-1-end)*sizeof(glVertex));
    vec->size -= d;
}


int cvec_reserve_glVertex(cvector_glVertex* vec, cvec_sz size)
{
    glVertex* tmp;
    if (vec->capacity < size) {
        if (!(tmp = (glVertex*)CVEC_REALLOC(vec->a, sizeof(glVertex)*(size+CVEC_glVertex_SZ)))) {
            CVEC_ASSERT(tmp != NULL);
            return 0;
        }
        vec->a = tmp;
        vec->capacity = size + CVEC_glVertex_SZ;
    }
    return 1;
}

int cvec_set_cap_glVertex(cvector_glVertex* vec, cvec_sz size)
{
    glVertex* tmp;
    if (size < vec->size) {
        vec->size = size;
    }

    if (!(tmp = (glVertex*)CVEC_REALLOC(vec->a, sizeof(glVertex)*size))) {
        CVEC_ASSERT(tmp != NULL);
        return 0;
    }
    vec->a = tmp;
    vec->capacity = size;
    return 1;
}

void cvec_set_val_sz_glVertex(cvector_glVertex* vec, glVertex val)
{
    cvec_sz i;
    for (i=0; i<vec->size; i++) {
        vec->a[i] = val;
    }
}

void cvec_set_val_cap_glVertex(cvector_glVertex* vec, glVertex val)
{
    cvec_sz i;
    for (i=0; i<vec->capacity; i++) {
        vec->a[i] = val;
    }
}

void cvec_clear_glVertex(cvector_glVertex* vec) { vec->size = 0; }

void cvec_free_glVertex_heap(void* vec)
{
    cvector_glVertex* tmp = (cvector_glVertex*)vec;
    if (!tmp) return;
    CVEC_FREE(tmp->a);
    CVEC_FREE(tmp);
}

void cvec_free_glVertex(void* vec)
{
    cvector_glVertex* tmp = (cvector_glVertex*)vec;
    CVEC_FREE(tmp->a);
    tmp->size = 0;
    tmp->capacity = 0;
}


static glContext* c;

static Color blend_pixel(vec4 src, vec4 dst);
static int fragment_processing(int x, int y, float z);
static void draw_pixel(vec4 cf, int x, int y, float z, int do_frag_processing);
static void run_pipeline(GLenum mode, const GLvoid* indices, GLsizei count, GLsizei instance, GLuint base_instance, GLboolean use_elements);

static float calc_poly_offset(vec3 hp0, vec3 hp1, vec3 hp2);

static void draw_triangle_clip(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke, int clip_bit);
static void draw_triangle_point(glVertex* v0, glVertex* v1,  glVertex* v2, unsigned int provoke);
static void draw_triangle_line(glVertex* v0, glVertex* v1,  glVertex* v2, unsigned int provoke);
static void draw_triangle_fill(glVertex* v0, glVertex* v1,  glVertex* v2, unsigned int provoke);
static void draw_triangle_final(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke);
static void draw_triangle(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke);

static void draw_line_clip(glVertex* v1, glVertex* v2);

// This is the prototype for either implementation; only one is defined based on
// whether PGL_BETTER_THICK_LINES is defined
static void draw_thick_line(vec3 hp1, vec3 hp2, float w1, float w2, float* v1_out, float* v2_out, unsigned int provoke, float poly_offset);

// Only width 1 supported for now
static void draw_aa_line(vec3 hp1, vec3 hp2, float w1, float w2, float* v1_out, float* v2_out, unsigned int provoke, float poly_offset);

/* this clip epsilon is needed to avoid some rounding errors after
   several clipping stages */

#define CLIP_EPSILON (1E-5)
#define CLIPZ_MASK 0x3
#define CLIPX_TEST(x) (x >= c->lx && x < c->ux)
#define CLIPY_TEST(y) (y >= c->ly && y < c->uy)
#define CLIPXY_TEST(x, y) (x >= c->lx && x < c->ux && y >= c->ly && y < c->uy)


static inline int gl_clipcode(vec4 pt)
{
    float w;

    w = pt.w * (1.0 + CLIP_EPSILON);
    return
        (((pt.z < -w) |
         ((pt.z >  w) << 1)) &
         ((!c->depth_clamp) |
          (!c->depth_clamp) << 1)) |

        ((pt.x < -w) << 2) |
        ((pt.x >  w) << 3) |
        ((pt.y < -w) << 4) |
        ((pt.y >  w) << 5);

}




static int is_front_facing(glVertex* v0, glVertex* v1, glVertex* v2)
{
    //according to docs culling is done based on window coordinates
    //See page 3.6.1 page 116 of glspec33.core for more on rasterization, culling etc.
    //
    //TODO See if there's a way to determine front facing before
    // clipping the near plane (vertex behind the eye seems to mess
    // up winding).  If yes, can refactor to cull early and handle
    // line and point modes separately
    vec3 p0 = v4_to_v3h(v0->screen_space);
    vec3 p1 = v4_to_v3h(v1->screen_space);
    vec3 p2 = v4_to_v3h(v2->screen_space);

    float a;

    //method from spec
    a = p0.x*p1.y - p1.x*p0.y + p1.x*p2.y - p2.x*p1.y + p2.x*p0.y - p0.x*p2.y;
    //a /= 2;

    if (c->front_face == GL_CW) {
        a = -a;
    }

    if (a <= 0) {
        return 0;
    }

    return 1;
}

// TODO make a config macro that turns this into an inline function/macro that
// only supports float for a small perf boost
static vec4 get_v_attrib(glVertex_Attrib* v, GLsizei i)
{
    // v->buf will be 0 for a client array and buf[0].data
    // is always NULL so this works for both but we have to cast
    // the pointer to GLsizeiptr because adding an offset to a NULL pointer
    // is undefined.  So, do the math as numbers and convert back to a pointer
    GLsizeiptr buf_data = (GLsizeiptr)c->buffers.a[v->buf].data;
    u8* u8p = (u8*)(buf_data + v->offset + v->stride*i);

    i8* i8p = (i8*)u8p;
    u16* u16p = (u16*)u8p;
    i16* i16p = (i16*)u8p;
    u32* u32p = (u32*)u8p;
    i32* i32p = (i32*)u8p;

    vec4 tmpvec4 = { 0.0f, 0.0f, 0.0f, 1.0f };
    float* tv = (float*)&tmpvec4;
    GLenum type = v->type;

    if (type < GL_FLOAT) {
        for (int i=0; i<v->size; i++) {
            if (v->normalized) {
                switch (type) {
                case GL_BYTE:           tv[i] = rsw_mapf(i8p[i], INT8_MIN, INT8_MAX, -1.0f, 1.0f); break;
                case GL_UNSIGNED_BYTE:  tv[i] = rsw_mapf(u8p[i], 0, UINT8_MAX, 0.0f, 1.0f); break;
                case GL_SHORT:          tv[i] = rsw_mapf(i16p[i], INT16_MIN,INT16_MAX, 0.0f, 1.0f); break;
                case GL_UNSIGNED_SHORT: tv[i] = rsw_mapf(u16p[i], 0, UINT16_MAX, 0.0f, 1.0f); break;
                case GL_INT:            tv[i] = rsw_mapf(i32p[i], INT32_MIN, INT32_MAX, 0.0f, 1.0f); break;
                case GL_UNSIGNED_INT:   tv[i] = rsw_mapf(u32p[i], 0, UINT32_MAX, 0.0f, 1.0f); break;
                }
            } else {
                switch (type) {
                case GL_BYTE:           tv[i] = i8p[i]; break;
                case GL_UNSIGNED_BYTE:  tv[i] = u8p[i]; break;
                case GL_SHORT:          tv[i] = i16p[i]; break;
                case GL_UNSIGNED_SHORT: tv[i] = u16p[i]; break;
                case GL_INT:            tv[i] = i32p[i]; break;
                case GL_UNSIGNED_INT:   tv[i] = u32p[i]; break;
                }
            }
        }
    } else {
        // TODO support GL_DOUBLE

        memcpy(tv, u8p, sizeof(float)*v->size);
    }

    //c->cur_vertex_array->vertex_attribs[enabled[j]].buf->data;
    return tmpvec4;
}

// TODO Possibly split for optimization and future parallelization, prep all verts first then do all shader calls at once
// Will need num_verts * vertex_attribs_vs[] space rather than a single attribute staging area...
static void do_vertex(glVertex_Attrib* v, int* enabled, int num_enabled, int i, int vert)
{
    // copy/prep vertex attributes from buffers into appropriate positions for vertex shader to access
    for (int j=0; j<num_enabled; ++j) {
        c->vertex_attribs_vs[enabled[j]] = get_v_attrib(&v[enabled[j]], i);
    }

    float* vs_out = &c->vs_output.output_buf[vert*c->vs_output.size];
    c->programs.a[c->cur_program].vertex_shader(vs_out, c->vertex_attribs_vs, &c->builtins, c->programs.a[c->cur_program].uniform);

    c->glverts.a[vert].vs_out = vs_out;
    c->glverts.a[vert].clip_space = c->builtins.gl_Position;

    // no use setting here because of TRIANGLE_STRIP
    // and TRIANGLE_FAN. While I don't properly
    // generate "primitives", I do expand create unique vertices
    // to process when the user uses an element (index) buffer.
    //
    // so it's done in draw_triangle()
    //c->glverts.a[vert].edge_flag = 1;

    c->glverts.a[vert].clip_code = gl_clipcode(c->builtins.gl_Position);
}

// TODO naming issue/refactor?
// When used with Draw*Arrays* indices is really the index of the first vertex to be used
// When used for Draw*Elements* indices is either a byte offset of the first index or
// an actual pointer to the array of indices depending on whether an ELEMENT_ARRAY_BUFFER is bound
//
// use_elems_type is either 0/false or one of GL_UNSIGNED_BYTE/SHORT/INT
// so used as a boolean and an enum
static void vertex_stage(const GLvoid* indices, GLsizei count, GLsizei instance_id, GLuint base_instance, GLenum use_elems_type)
{
    int i, j, vert, num_enabled;

    glVertex_Attrib* v = c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs;
    GLuint elem_buffer = c->vertex_arrays.a[c->cur_vertex_array].element_buffer;

    //save checking if enabled on every loop if we build this first
    //also initialize the vertex_attrib space
    // TODO does creating enabled array actually help perf?  At what number
    // of GL_MAX_VERTEX_ATTRIBS and vertices does it become a benefit?
    int enabled[GL_MAX_VERTEX_ATTRIBS] = { 0 };
    for (i=0, j=0; i<GL_MAX_VERTEX_ATTRIBS; ++i) {
        if (v[i].enabled) {
            if (v[i].divisor == 0) {
                enabled[j++] = i;
            } else if (!(instance_id % v[i].divisor)) {
                //set instanced attributes if necessary
                int n = instance_id/v[i].divisor + base_instance;
                c->vertex_attribs_vs[i] = get_v_attrib(&v[i], n);
            }
        }
    }
    num_enabled = j;

    cvec_reserve_glVertex(&c->glverts, count);

    // gl_InstanceID always starts at 0, base_instance is only added when grabbing attributes
    // https://www.khronos.org/opengl/wiki/Built-in_Variable_(GLSL)#Vertex_shader_inputs
    c->builtins.gl_InstanceID = instance_id;
    c->builtins.gl_BaseInstance = base_instance;
    GLsizeiptr first = (GLsizeiptr)indices;

    if (!use_elems_type) {
        for (vert=0, i=first; i<first+count; ++i, ++vert) {
            do_vertex(v, enabled, num_enabled, i, vert);
        }
    } else {
        GLuint* uint_array = (GLuint*)indices;
        GLushort* ushort_array = (GLushort*)indices;
        GLubyte* ubyte_array = (GLubyte*)indices;
        if (c->bound_buffers[GL_ELEMENT_ARRAY_BUFFER-GL_ARRAY_BUFFER]) {
            uint_array = (GLuint*)(c->buffers.a[elem_buffer].data + first);
            ushort_array = (GLushort*)(c->buffers.a[elem_buffer].data + first);
            ubyte_array = (GLubyte*)(c->buffers.a[elem_buffer].data + first);
        }
        if (use_elems_type == GL_UNSIGNED_BYTE) {
            for (i=0; i<count; ++i) {
                do_vertex(v, enabled, num_enabled, ubyte_array[i], i);
            }
        } else if (use_elems_type == GL_UNSIGNED_SHORT) {
            for (i=0; i<count; ++i) {
                do_vertex(v, enabled, num_enabled, ushort_array[i], i);
            }
        } else {
            for (i=0; i<count; ++i) {
                do_vertex(v, enabled, num_enabled, uint_array[i], i);
            }
        }
    }
}


//TODO make fs_input static?  or a member of glContext?
static void draw_point(glVertex* vert, float poly_offset)
{
    float fs_input[GL_MAX_VERTEX_OUTPUT_COMPONENTS];

    vec3 point = v4_to_v3h(vert->screen_space);
    point.z += poly_offset; // couldn't this put it outside of [-1,1]?
    point.z = rsw_mapf(point.z, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);

    // TODO necessary for non-perspective?
    //if (c->depth_clamp)
    //	clamp(point.z, c->depth_range_near, c->depth_range_far);

    Shader_Builtins builtins;
    // 3.3 spec pg 110 says r,q are supposed to be replaced with 0 and 1...
    // but PointCoord is a vec2 and that is not in the 4.6 spec so it must be a typo

    int fragdepth_or_discard = c->programs.a[c->cur_program].fragdepth_or_discard;

    //TODO why not just pass vs_output directly?  hmmm...
    memcpy(fs_input, vert->vs_out, c->vs_output.size*sizeof(float));

    //accounting for pixel centers at 0.5, using truncation
    float x = point.x + 0.5f;
    float y = point.y + 0.5f;
    float p_size = c->point_size;
    float origin = (c->point_spr_origin == GL_UPPER_LEFT) ? -1.0f : 1.0f;
    // NOTE/TODO, According to the spec if the clip coordinate, ie the
    // center of the point is outside the clip volume, you're supposed to
    // clip the whole thing, but some vendors don't do that because it's
    // not what most people want.

    // Can easily clip whole point when point size <= 1
    if (p_size <= 1.0f) {
        if (x < c->lx || y < c->ly || x >= c->ux || y >= c->uy)
            return;
    }

    for (float i = y-p_size/2; i<y+p_size/2; ++i) {
        if (i < c->ly || i >= c->uy)
            continue;

        for (float j = x-p_size/2; j<x+p_size/2; ++j) {

            if (j < c->lx || j >= c->ux)
                continue;

            if (!fragdepth_or_discard && !fragment_processing(j, i, point.z)) {
                continue;
            }

            // per page 110 of 3.3 spec (x,y are s,t)
            builtins.gl_PointCoord.x = 0.5f + ((int)j + 0.5f - point.x)/p_size;
            builtins.gl_PointCoord.y = 0.5f + origin * ((int)i + 0.5f - point.y)/p_size;

            SET_V4(builtins.gl_FragCoord, j, i, point.z, 1/vert->screen_space.w);
            builtins.discard = GL_FALSE;
            builtins.gl_FragDepth = point.z;
            c->programs.a[c->cur_program].fragment_shader(fs_input, &builtins, c->programs.a[c->cur_program].uniform);
            if (!builtins.discard)
                draw_pixel(builtins.gl_FragColor, j, i, builtins.gl_FragDepth, fragdepth_or_discard);
        }
    }
}

static void run_pipeline(GLenum mode, const GLvoid* indices, GLsizei count, GLsizei instance, GLuint base_instance, GLboolean use_elements)
{
    GLsizei i;
    int provoke;

    PGL_ASSERT(count <= get_max_vertices());

    vertex_stage(indices, count, instance, base_instance, use_elements);

    //fragment portion
    if (mode == GL_POINTS) {
        for (i=0; i<count; ++i) {
            // clip only z and let partial points (size > 1)
            // show even if the center would have been clipped
            if (c->glverts.a[i].clip_code & CLIPZ_MASK)
                continue;

            c->glverts.a[i].screen_space = mult_m4_v4(c->vp_mat, c->glverts.a[i].clip_space);

            draw_point(&c->glverts.a[i], 0.0f);
        }
    } else if (mode == GL_LINES) {
        for (i=0; i<count-1; i+=2) {
            draw_line_clip(&c->glverts.a[i], &c->glverts.a[i+1]);
        }
    } else if (mode == GL_LINE_STRIP) {
        for (i=0; i<count-1; i++) {
            draw_line_clip(&c->glverts.a[i], &c->glverts.a[i+1]);
        }
    } else if (mode == GL_LINE_LOOP) {
        for (i=0; i<count-1; i++) {
            draw_line_clip(&c->glverts.a[i], &c->glverts.a[i+1]);
        }
        //draw ending line from last to first point
        draw_line_clip(&c->glverts.a[count-1], &c->glverts.a[0]);

    } else if (mode == GL_TRIANGLES) {
        provoke = (c->provoking_vert == GL_LAST_VERTEX_CONVENTION) ? 2 : 0;

        for (i=0; i<count-2; i+=3) {
            draw_triangle(&c->glverts.a[i], &c->glverts.a[i+1], &c->glverts.a[i+2], i+provoke);
        }

    } else if (mode == GL_TRIANGLE_STRIP) {
        unsigned int a=0, b=1, toggle = 0;
        provoke = (c->provoking_vert == GL_LAST_VERTEX_CONVENTION) ? 0 : -2;

        for (i=2; i<count; ++i) {
            draw_triangle(&c->glverts.a[a], &c->glverts.a[b], &c->glverts.a[i], i+provoke);

            if (!toggle)
                a = i;
            else
                b = i;

            toggle = !toggle;
        }
    } else if (mode == GL_TRIANGLE_FAN) {
        provoke = (c->provoking_vert == GL_LAST_VERTEX_CONVENTION) ? 0 : -1;

        for (i=2; i<count; ++i) {
            draw_triangle(&c->glverts.a[0], &c->glverts.a[i-1], &c->glverts.a[i], i+provoke);
        }
    }
}


static int depthtest(u32 zval, u32 zbufval)
{
    switch (c->depth_func) {
    case GL_LESS:
        return zval < zbufval;
    case GL_LEQUAL:
        return zval <= zbufval;
    case GL_GREATER:
        return zval > zbufval;
    case GL_GEQUAL:
        return zval >= zbufval;
    case GL_EQUAL:
        return zval == zbufval;
    case GL_NOTEQUAL:
        return zval != zbufval;
    case GL_ALWAYS:
        return 1;
    case GL_NEVER:
        return 0;
    }
    return 0; //get rid of compile warning
}


static void setup_fs_input(float t, float* v1_out, float* v2_out, float wa, float wb, unsigned int provoke)
{
    float* vs_output = &c->vs_output.output_buf[0];

    float inv_wa = 1.0/wa;
    float inv_wb = 1.0/wb;

    for (int i=0; i<c->vs_output.size; ++i) {
        if (c->vs_output.interpolation[i] == PGL_SMOOTH) {
            c->fs_input[i] = (v1_out[i]*inv_wa + t*(v2_out[i]*inv_wb - v1_out[i]*inv_wa)) / (inv_wa + t*(inv_wb - inv_wa));

        } else if (c->vs_output.interpolation[i] == PGL_NOPERSPECTIVE) {
            c->fs_input[i] = v1_out[i] + t*(v2_out[i] - v1_out[i]);
        } else {
            c->fs_input[i] = vs_output[provoke*c->vs_output.size + i];
        }
    }

    c->builtins.discard = GL_FALSE;
}

/* Line Clipping algorithm from 'Computer Graphics', Principles and
   Practice */
static inline int clip_line(float denom, float num, float* tmin, float* tmax)
{
    float t;

    if (denom > 0) {
        t = num / denom;
        if (t > *tmax) return 0;
        if (t > *tmin) {
            *tmin = t;
            //printf("t > *tmin %f\n", t);
        }
    } else if (denom < 0) {
        t = num / denom;
        if (t < *tmin) return 0;
        if (t < *tmax) {
            *tmax = t;
            //printf("t < *tmax %f\n", t);
        }
    } else if (num > 0) return 0;
    return 1;
}


static void interpolate_clipped_line(glVertex* v1, glVertex* v2, float* v1_out, float* v2_out, float tmin, float tmax)
{
    for (int i=0; i<c->vs_output.size; ++i) {
        v1_out[i] = v1->vs_out[i] + (v2->vs_out[i] - v1->vs_out[i])*tmin;
        v2_out[i] = v1->vs_out[i] + (v2->vs_out[i] - v1->vs_out[i])*tmax;

        //v2_out[i] = (1 - tmax)*v1->vs_out[i] + tmax*v2->vs_out[i];
    }
}



static void draw_line_clip(glVertex* v1, glVertex* v2)
{
    int cc1, cc2;
    vec4 d, p1, p2, t1, t2;
    float tmin, tmax;

    cc1 = v1->clip_code;
    cc2 = v2->clip_code;

    p1 = v1->clip_space;
    p2 = v2->clip_space;
    
    float v1_out[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    float v2_out[GL_MAX_VERTEX_OUTPUT_COMPONENTS];

    vec3 hp1, hp2;

    //TODO ponder this
    unsigned int provoke;
    if (c->provoking_vert == GL_LAST_VERTEX_CONVENTION)
        provoke = (v2 - c->glverts.a)/sizeof(glVertex);
    else
        provoke = (v1 - c->glverts.a)/sizeof(glVertex);

    if (cc1 & cc2) {
        return;
    } else if ((cc1 | cc2) == 0) {
        t1 = mult_m4_v4(c->vp_mat, p1);
        t2 = mult_m4_v4(c->vp_mat, p2);

        hp1 = v4_to_v3h(t1);
        hp2 = v4_to_v3h(t2);

        if (c->line_smooth) {
            draw_aa_line(hp1, hp2, t1.w, t2.w, v1->vs_out, v2->vs_out, provoke, 0.0f);
        } else {
            draw_thick_line(hp1, hp2, t1.w, t2.w, v1->vs_out, v2->vs_out, provoke, 0.0f);
        }
    } else {

        d = sub_v4s(p2, p1);

        tmin = 0;
        tmax = 1;
        if (clip_line( d.x+d.w, -p1.x-p1.w, &tmin, &tmax) &&
            clip_line(-d.x+d.w,  p1.x-p1.w, &tmin, &tmax) &&
            clip_line( d.y+d.w, -p1.y-p1.w, &tmin, &tmax) &&
            clip_line(-d.y+d.w,  p1.y-p1.w, &tmin, &tmax) &&
            clip_line( d.z+d.w, -p1.z-p1.w, &tmin, &tmax) &&
            clip_line(-d.z+d.w,  p1.z-p1.w, &tmin, &tmax)) {

            //printf("%f %f\n", tmin, tmax);

            t1 = add_v4s(p1, scale_v4(d, tmin));
            t2 = add_v4s(p1, scale_v4(d, tmax));

            t1 = mult_m4_v4(c->vp_mat, t1);
            t2 = mult_m4_v4(c->vp_mat, t2);
            //print_v4(t1, "\n");
            //print_v4(t2, "\n");

            interpolate_clipped_line(v1, v2, v1_out, v2_out, tmin, tmax);

            hp1 = v4_to_v3h(t1);
            hp2 = v4_to_v3h(t2);

            if (c->line_smooth) {
                draw_aa_line(hp1, hp2, t1.w, t2.w, v1_out, v2_out, provoke, 0.0f);
            } else {
                draw_thick_line(hp1, hp2, t1.w, t2.w, v1_out, v2_out, provoke, 0.0f);
            }
        }
    }
}

#ifndef PGL_BETTER_THICK_LINES
static void draw_thick_line(vec3 hp1, vec3 hp2, float w1, float w2, float* v1_out, float* v2_out, unsigned int provoke, float poly_offset)
{
    float tmp;
    float* tmp_ptr;

    float x1 = hp1.x, x2 = hp2.x, y1 = hp1.y, y2 = hp2.y;
    float z1 = hp1.z, z2 = hp2.z;

    //always draw from left to right
    if (x2 < x1) {
        tmp = x1;
        x1 = x2;
        x2 = tmp;
        tmp = y1;
        y1 = y2;
        y2 = tmp;

        tmp = z1;
        z1 = z2;
        z2 = tmp;

        tmp = w1;
        w1 = w2;
        w2 = tmp;

        tmp_ptr = v1_out;
        v1_out = v2_out;
        v2_out = tmp_ptr;
    }

    //calculate slope and implicit line parameters once
    //could just use my Line type/constructor as in draw_triangle
    float m = (y2-y1)/(x2-x1);
    Line line = make_Line(x1, y1, x2, y2);

    float t, x, y, z, w;

    vec2 p1 = { x1, y1 }, p2 = { x2, y2 };
    vec2 pr, sub_p2p1 = sub_v2s(p2, p1);
    float line_length_squared = len_v2(sub_p2p1);
    line_length_squared *= line_length_squared;

    frag_func fragment_shader = c->programs.a[c->cur_program].fragment_shader;
    void* uniform = c->programs.a[c->cur_program].uniform;
    int fragdepth_or_discard = c->programs.a[c->cur_program].fragdepth_or_discard;

    float i_x1, i_y1, i_x2, i_y2;
    i_x1 = floor(p1.x) + 0.5;
    i_y1 = floor(p1.y) + 0.5;
    i_x2 = floor(p2.x) + 0.5;
    i_y2 = floor(p2.y) + 0.5;

    float x_min, x_max, y_min, y_max;
    x_min = i_x1;
    x_max = i_x2; //always left to right;
    if (m <= 0) {
        y_min = i_y2;
        y_max = i_y1;
    } else {
        y_min = i_y1;
        y_max = i_y2;
    }

    // TODO should be done for each fragment, after poly_offset is added?
    z1 = rsw_mapf(z1, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);
    z2 = rsw_mapf(z2, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);

    float width = roundf(c->line_width);
    if (!width) {
        width = 1.0f;
    }
    //int wi = width;
    float half_w = width * 0.5f;

    // TODO solve off by one issues:
    //   See test outputs where there seems to occasionally be an extra pixel
    //   Also might be drawing lines one pixel lower on the minor axis
    //
    //   Also, I shouldn't have to clamp t, technically if it's outside [0,1]
    //   it's not part of the line so it should be skipped or blended if the
    //   pixel is partially covered and you're doing AA. Or mabye I do have to
    //   clamp but be more particular about starting and ending pixel which..
    //
    // TODO I need to do anyway, since GL specifically says two lines which
    // share an endpoint should *not* evaluate that pixel twice and which
    // gets it should be deterministic
    //
    // TODO maybe try simplifying into only 2 cases steep or not steep like
    // AA algorithm

    //4 cases based on slope
    if (m <= -1) {     //(-infinite, -1]
        //printf("slope <= -1\n");
        for (x = x_min, y = y_max; y>=y_min && x<=x_max; --y) {
            pr.x = x;
            pr.y = y;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            t = clamp_01(t);

            z = (1 - t) * z1 + t * z2;
            z += poly_offset;
            w = (1 - t) * w1 + t * w2;

            for (float j=x-half_w; j<x+half_w; ++j) {
                if (CLIPXY_TEST(j, y)) {
                    if (fragdepth_or_discard || fragment_processing(j, y, z)) {
                        SET_V4(c->builtins.gl_FragCoord, j, y, z, 1/w);
                        c->builtins.discard = GL_FALSE;
                        c->builtins.gl_FragDepth = z;
                        setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                        fragment_shader(c->fs_input, &c->builtins, uniform);
                        if (!c->builtins.discard)
                            draw_pixel(c->builtins.gl_FragColor, j, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }

            if (line_func(&line, x+0.5f, y-1) < 0) //A*(x+0.5f) + B*(y-1) + C < 0)
                ++x;
        }
    } else if (m <= 0) {     //(-1, 0]
        //printf("slope = (-1, 0]\n");
        for (x = x_min, y = y_max; x<=x_max && y>=y_min; ++x) {
            pr.x = x;
            pr.y = y;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            t = clamp_01(t);

            z = (1 - t) * z1 + t * z2;
            z += poly_offset;
            w = (1 - t) * w1 + t * w2;

            for (float j=y-half_w; j<y+half_w; ++j) {
                if (CLIPXY_TEST(x, j)) {
                    if (fragdepth_or_discard || fragment_processing(x, j, z)) {

                        SET_V4(c->builtins.gl_FragCoord, x, j, z, 1/w);
                        c->builtins.discard = GL_FALSE;
                        c->builtins.gl_FragDepth = z;
                        setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                        fragment_shader(c->fs_input, &c->builtins, uniform);
                        if (!c->builtins.discard)
                            draw_pixel(c->builtins.gl_FragColor, x, j, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }
            if (line_func(&line, x+1, y-0.5f) > 0) //A*(x+1) + B*(y-0.5f) + C > 0)
                --y;
        }
    } else if (m <= 1) {     //(0, 1]
        //printf("slope = (0, 1]\n");
        for (x = x_min, y = y_min; x <= x_max && y <= y_max; ++x) {
            pr.x = x;
            pr.y = y;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            t = clamp_01(t);

            z = (1 - t) * z1 + t * z2;
            z += poly_offset;
            w = (1 - t) * w1 + t * w2;

            for (float j=y-half_w; j<y+half_w; ++j) {
                if (CLIPXY_TEST(x, j)) {
                    if (fragdepth_or_discard || fragment_processing(x, j, z)) {

                        SET_V4(c->builtins.gl_FragCoord, x, j, z, 1/w);
                        c->builtins.discard = GL_FALSE;
                        c->builtins.gl_FragDepth = z;
                        setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                        fragment_shader(c->fs_input, &c->builtins, uniform);
                        if (!c->builtins.discard)
                            draw_pixel(c->builtins.gl_FragColor, x, j, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }
            if (line_func(&line, x+1, y+0.5f) < 0) //A*(x+1) + B*(y+0.5f) + C < 0)
                ++y;
        }

    } else {    //(1, +infinite)
        //printf("slope > 1\n");
        for (x = x_min, y = y_min; y<=y_max && x <= x_max; ++y) {
            pr.x = x;
            pr.y = y;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            t = clamp_01(t);

            z = (1 - t) * z1 + t * z2;
            z += poly_offset;
            w = (1 - t) * w1 + t * w2;

            for (float j=x-half_w; j<x+half_w; ++j) {
                if (CLIPXY_TEST(j, y)) {
                    if (fragdepth_or_discard || fragment_processing(j, y, z)) {

                        SET_V4(c->builtins.gl_FragCoord, j, y, z, 1/w);
                        c->builtins.discard = GL_FALSE;
                        c->builtins.gl_FragDepth = z;
                        setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                        fragment_shader(c->fs_input, &c->builtins, uniform);
                        if (!c->builtins.discard)
                            draw_pixel(c->builtins.gl_FragColor, j, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }
            if (line_func(&line, x+0.5f, y+1) > 0) //A*(x+0.5f) + B*(y+1) + C > 0)
                ++x;
        }
    }
}
#else
static void draw_thick_line(vec3 hp1, vec3 hp2, float w1, float w2, float* v1_out, float* v2_out, unsigned int provoke, float poly_offset)
{
    float tmp;
    float* tmp_ptr;

    float x1 = hp1.x, x2 = hp2.x, y1 = hp1.y, y2 = hp2.y;
    float z1 = hp1.z, z2 = hp2.z;

    //always draw from left to right
    if (x2 < x1) {
        tmp = x1;
        x1 = x2;
        x2 = tmp;
        tmp = y1;
        y1 = y2;
        y2 = tmp;

        tmp = z1;
        z1 = z2;
        z2 = tmp;

        tmp = w1;
        w1 = w2;
        w2 = tmp;

        tmp_ptr = v1_out;
        v1_out = v2_out;
        v2_out = tmp_ptr;
    }

    // Need half for the rest
    float width = c->line_width * 0.5f;

    //calculate slope and implicit line parameters once
    float m = (y2-y1)/(x2-x1);
    Line line = make_Line(x1, y1, x2, y2);
    normalize_line(&line);

    vec2 p1 = { x1, y1 };
    vec2 p2 = { x2, y2 };
    vec2 v12 = sub_v2s(p2, p1);
    vec2 v1r, pr; // v2r

    float dot_1212 = dot_v2s(v12, v12);

    float x_min, x_max, y_min, y_max;

    x_min = p1.x - width;
    x_max = p2.x + width;
    if (m <= 0) {
        y_min = p2.y - width;
        y_max = p1.y + width;
    } else {
        y_min = p1.y - width;
        y_max = p2.y + width;
    }

    // clipping/scissoring against side planes here
    x_min = MAX(c->lx, x_min);
    x_max = MIN(c->ux, x_max);
    y_min = MAX(c->ly, y_min);
    y_max = MIN(c->uy, y_max);
    // end clipping
    
    y_min = floor(y_min) + 0.5f;
    x_min = floor(x_min) + 0.5f;
    float x_mino = x_min;
    float x_maxo = x_max;


    frag_func fragment_shader = c->programs.a[c->cur_program].fragment_shader;
    void* uniform = c->programs.a[c->cur_program].uniform;
    int fragdepth_or_discard = c->programs.a[c->cur_program].fragdepth_or_discard;

    float t, x, y, z, w, e, dist;
    //float width_squared = width*width;

    // calculate x_max or just use last logic?
    //int last = 0;

    //printf("%f %f %f %f   =\n", i_x1, i_y1, i_x2, i_y2);
    //printf("%f %f %f %f   x_min etc\n", x_min, x_max, y_min, y_max);

    // TODO should be done for each fragment, after poly_offset is added?
    z1 = rsw_mapf(z1, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);
    z2 = rsw_mapf(z2, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);

    for (y = y_min; y < y_max; ++y) {
        pr.y = y;
        //last = GL_FALSE;

        // could also check fabsf(line.A) > epsilon
        if (fabsf(m) > 0.0001f) {
            x_min = (-width - line.C - line.B*y)/line.A;
            x_max = (width - line.C - line.B*y)/line.A;
            if (x_min > x_max) {
                tmp = x_min;
                x_min = x_max;
                x_max = tmp;
            }
            x_min = MAX(c->lx, x_min);
            x_min = floorf(x_min) + 0.5f;
            x_max = MIN(c->ux, x_max);
            //printf("%f %f   x_min etc\n", x_min, x_max);
        } else {
            x_min = x_mino;
            x_max = x_maxo;
        }
        for (x = x_min; x < x_max; ++x) {
            pr.x = x;
            v1r = sub_v2s(pr, p1);
            //v2r = sub_v2s(pr, p2);
            e = dot_v2s(v1r, v12);

            // c lies past the ends of the segment v12
            if (e <= 0.0f || e >= dot_1212) {
                continue;
            }

            // can do this because we normalized the line equation
            // TODO square or fabsf?
            dist = line_func(&line, pr.x, pr.y);
            //if (dist*dist < width_squared) {
            if (fabsf(dist) < width) {
                t = e / dot_1212;

                z = (1 - t) * z1 + t * z2;
                z += poly_offset;
                if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                    w = (1 - t) * w1 + t * w2;

                    SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                    c->builtins.discard = GL_FALSE;
                    c->builtins.gl_FragDepth = z;
                    setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);

                    fragment_shader(c->fs_input, &c->builtins, uniform);
                    if (!c->builtins.discard)
                        draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            //	last = GL_TRUE;
            //} else if (last) {
            //	break; // we have passed the right edge of the line on this row
            }
        }
    }
}
#endif



// As an adaptation of Xialin Wu's AA line algorithm, unlike all other GL
// rasterization functions, this uses integer pixel centers and passes
// those in glFragCoord.

#define ipart_(X) ((int)(X))
#define round_(X) ((int)(((float)(X))+0.5f))
#define fpart_(X) (((float)(X))-(float)ipart_(X))
#define rfpart_(X) (1.0f-fpart_(X))

#define swap_(a, b) do{ __typeof__(a) tmp;  tmp = a; a = b; b = tmp; } while(0)
static void draw_aa_line(vec3 hp1, vec3 hp2, float w1, float w2, float* v1_out, float* v2_out, unsigned int provoke, float poly_offset)
{
    float t, z, w;
    int x, y;

    frag_func fragment_shader = c->programs.a[c->cur_program].fragment_shader;
    void* uniform = c->programs.a[c->cur_program].uniform;
    int fragdepth_or_discard = c->programs.a[c->cur_program].fragdepth_or_discard;

    float x1 = hp1.x, x2 = hp2.x, y1 = hp1.y, y2 = hp2.y;
    float z1 = hp1.z, z2 = hp2.z;

    float dx = x2 - x1;
    float dy = y2 - y1;

    if (fabsf(dx) > fabsf(dy)) {
        if (x2 < x1) {
            swap_(x1, x2);
            swap_(y1, y2);
            swap_(z1, z2);
            swap_(w1, w2);
            swap_(v1_out, v2_out);
        }

        vec2 p1 = { x1, y1 }, p2 = { x2, y2 };
        vec2 pr, sub_p2p1 = sub_v2s(p2, p1);
        float line_length_squared = len_v2(sub_p2p1);
        line_length_squared *= line_length_squared;

        // TODO should be done for each fragment, after poly_offset is added?
        z1 = rsw_mapf(z1, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);
        z2 = rsw_mapf(z2, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);

        float gradient = dy / dx;
        float xend = round_(x1);
        float yend = y1 + gradient*(xend - x1);
        float xgap = rfpart_(x1 + 0.5);
        int xpxl1 = xend;
        int ypxl1 = ipart_(yend);

        t = 0.0f;
        z = z1 + poly_offset;
        w = w1;

        // TODO This is so ugly and repetitive...Should I bother with end points?
        // Or run the shader only once for each pair?
        x = xpxl1;
        y = ypxl1;
        if (CLIPXY_TEST(x, y)) {
            if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= rfpart_(yend)*xgap;
                    draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }
        if (CLIPXY_TEST(x, y+1)) {
            if (fragdepth_or_discard || fragment_processing(x, y+1, z)) {
                SET_V4(c->builtins.gl_FragCoord, x, y+1, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= fpart_(yend)*xgap;
                    draw_pixel(c->builtins.gl_FragColor, x, y+1, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }
        //printf("xgap = %f\n", xgap);
        //printf("%f %f\n", rfpart_(yend), fpart_(yend));
        //printf("%f %f\n", rfpart_(yend)*xgap, fpart_(yend)*xgap);
        float intery = yend + gradient;

        xend = round_(x2);
        yend = y2 + gradient*(xend - x2);
        xgap = fpart_(x2+0.5);
        int xpxl2 = xend;
        int ypxl2 = ipart_(yend);

        t = 1.0f;
        z = z2 + poly_offset;
        w = w2;

        x = xpxl2;
        y = ypxl2;
        if (CLIPXY_TEST(x, y)) {
            if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= rfpart_(yend)*xgap;
                    draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }
        if (CLIPXY_TEST(x, y+1)) {
            if (fragdepth_or_discard || fragment_processing(x, y+1, z)) {
                SET_V4(c->builtins.gl_FragCoord, x, y+1, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= fpart_(yend)*xgap;
                    draw_pixel(c->builtins.gl_FragColor, x, y+1, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }

        for(x=xpxl1+1; x < xpxl2; x++) {
            pr.x = x;
            pr.y = intery;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            z = (1 - t) * z1 + t * z2;
            z += poly_offset;
            w = (1 - t) * w1 + t * w2;

            y = ipart_(intery);
            if (CLIPXY_TEST(x, y)) {
                if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                    SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                    c->builtins.discard = GL_FALSE;
                    c->builtins.gl_FragDepth = z;
                    setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                    fragment_shader(c->fs_input, &c->builtins, uniform);
                    if (!c->builtins.discard) {
                        c->builtins.gl_FragColor.w *= rfpart_(intery);
                        draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }
            if (CLIPXY_TEST(x, y+1)) {
                if (fragdepth_or_discard || fragment_processing(x, y+1, z)) {
                    SET_V4(c->builtins.gl_FragCoord, x, y+1, z, 1/w);
                    c->builtins.discard = GL_FALSE;
                    c->builtins.gl_FragDepth = z;
                    setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                    fragment_shader(c->fs_input, &c->builtins, uniform);
                    if (!c->builtins.discard) {
                        c->builtins.gl_FragColor.w *= fpart_(intery);
                        draw_pixel(c->builtins.gl_FragColor, x, y+1, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }

            intery += gradient;
        }
    } else {
        if (y2 < y1) {
            swap_(x1, x2);
            swap_(y1, y2);
            swap_(z1, z2);
            swap_(w1, w2);
            swap_(v1_out, v2_out);
        }

        vec2 p1 = { x1, y1 }, p2 = { x2, y2 };
        vec2 pr, sub_p2p1 = sub_v2s(p2, p1);
        float line_length_squared = len_v2(sub_p2p1);
        line_length_squared *= line_length_squared;

        // TODO should be done for each fragment, after poly_offset is added?
        z1 = rsw_mapf(z1, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);
        z2 = rsw_mapf(z2, -1.0f, 1.0f, c->depth_range_near, c->depth_range_far);

        float gradient = dx / dy;
        float yend = round_(y1);
        float xend = x1 + gradient*(yend - y1);
        float ygap = rfpart_(y1 + 0.5);
        int ypxl1 = yend;
        int xpxl1 = ipart_(xend);

        t = 0.0f;
        z = z1 + poly_offset;
        w = w1;

        x = xpxl1;
        y = ypxl1;
        if (CLIPXY_TEST(x, y)) {
            if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= rfpart_(xend)*ygap;
                    draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }
        if (CLIPXY_TEST(x+1, y)) {
            if (fragdepth_or_discard || fragment_processing(x+1, y, z)) {
                SET_V4(c->builtins.gl_FragCoord, x+1, y, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= fpart_(xend)*ygap;
                    draw_pixel(c->builtins.gl_FragColor, x+1, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }

        float interx = xend + gradient;

        yend = round_(y2);
        xend = x2 + gradient*(yend - y2);
        ygap = fpart_(y2+0.5);
        int ypxl2 = yend;
        int xpxl2 = ipart_(xend);

        t = 1.0f;
        z = z2 + poly_offset;
        w = w2;

        x = xpxl2;
        y = ypxl2;
        if (CLIPXY_TEST(x, y)) {
            if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= rfpart_(xend)*ygap;
                    draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }
        if (CLIPXY_TEST(x+1, y)) {
            if (fragdepth_or_discard || fragment_processing(x+1, y, z)) {
                SET_V4(c->builtins.gl_FragCoord, x+1, y, z, 1/w);
                c->builtins.discard = GL_FALSE;
                c->builtins.gl_FragDepth = z;
                setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                fragment_shader(c->fs_input, &c->builtins, uniform);
                if (!c->builtins.discard) {
                    c->builtins.gl_FragColor.w *= fpart_(xend)*ygap;
                    draw_pixel(c->builtins.gl_FragColor, x+1, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                }
            }
        }

        for(y=ypxl1+1; y < ypxl2; y++) {
            pr.x = interx;
            pr.y = y;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            z = (1 - t) * z1 + t * z2;
            z += poly_offset;
            w = (1 - t) * w1 + t * w2;

            x = ipart_(interx);
            if (CLIPXY_TEST(x, y)) {
                if (fragdepth_or_discard || fragment_processing(x, y, z)) {
                    SET_V4(c->builtins.gl_FragCoord, x, y, z, 1/w);
                    c->builtins.discard = GL_FALSE;
                    c->builtins.gl_FragDepth = z;
                    setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                    fragment_shader(c->fs_input, &c->builtins, uniform);
                    if (!c->builtins.discard) {
                        c->builtins.gl_FragColor.w *= rfpart_(interx);
                        draw_pixel(c->builtins.gl_FragColor, x, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }
            if (CLIPXY_TEST(x+1, y)) {
                if (fragdepth_or_discard || fragment_processing(x+1, y, z)) {
                    SET_V4(c->builtins.gl_FragCoord, x+1, y, z, 1/w);
                    c->builtins.discard = GL_FALSE;
                    c->builtins.gl_FragDepth = z;
                    setup_fs_input(t, v1_out, v2_out, w1, w2, provoke);
                    fragment_shader(c->fs_input, &c->builtins, uniform);
                    if (!c->builtins.discard) {
                        c->builtins.gl_FragColor.w *= fpart_(interx);
                        draw_pixel(c->builtins.gl_FragColor, x+1, y, c->builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }

            interx += gradient;
        }
    }
}

#undef swap_
#undef plot
#undef ipart_
#undef fpart_
#undef round_
#undef rfpart_

static void draw_triangle(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke)
{
    int c_or, c_and;
    c_and = v0->clip_code & v1->clip_code & v2->clip_code;
    if (c_and != 0) {
        //printf("triangle outside\n");
        return;
    }

    // have to set here because we can re use vertices
    // for multiple triangles in STRIP and FAN
    v0->edge_flag = v1->edge_flag = v2->edge_flag = 1;

    // TODO figure out how to remove XY clipping while still
    // handling weird edge cases like LearnPortableGL's skybox
    // case
    //v0->clip_code &= CLIPZ_MASK;
    //v1->clip_code &= CLIPZ_MASK;
    //v2->clip_code &= CLIPZ_MASK;
    c_or = v0->clip_code | v1->clip_code | v2->clip_code;
    if (c_or == 0) {
        draw_triangle_final(v0, v1, v2, provoke);
    } else {
        draw_triangle_clip(v0, v1, v2, provoke, 0);
    }
}

static void draw_triangle_final(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke)
{
    int front_facing;
    // Use batch vertex transformation to reduce function call overhead and improve cache locality
#if PGL_NEON_ENABLED
    pgl_neon_transform_3vertices(&v0->screen_space, &v1->screen_space, &v2->screen_space,
                                  c->vp_mat, v0->clip_space, v1->clip_space, v2->clip_space);
#else
    v0->screen_space = mult_m4_v4(c->vp_mat, v0->clip_space);
    v1->screen_space = mult_m4_v4(c->vp_mat, v1->clip_space);
    v2->screen_space = mult_m4_v4(c->vp_mat, v2->clip_space);
#endif

    front_facing = is_front_facing(v0, v1, v2);
    if (c->cull_face) {
        if (c->cull_mode == GL_FRONT_AND_BACK)
            return;
        if (c->cull_mode == GL_BACK && !front_facing) {
            //puts("culling back face");
            return;
        }
        if (c->cull_mode == GL_FRONT && front_facing)
            return;
    }

    c->builtins.gl_FrontFacing = front_facing;

    // TODO when/if I get rid of glPolygonMode support for FRONT
    // and BACK, this becomes a single function pointer, no branch
    if (front_facing) {
        c->draw_triangle_front(v0, v1, v2, provoke);
    } else {
        c->draw_triangle_back(v0, v1, v2, provoke);
    }
}


/* We clip the segment [a,b] against the 6 planes of the normal volume.
 * We compute the point 'c' of intersection and the value of the parameter 't'
 * of the intersection if x=a+t(b-a).
 */

#define clip_func(name, sign, dir, dir1, dir2) \
static float name(vec4 *c, vec4 *a, vec4 *b) \
{\
    float t, dx, dy, dz, dw, den;\
    dx = (b->x - a->x);\
    dy = (b->y - a->y);\
    dz = (b->z - a->z);\
    dw = (b->w - a->w);\
    den = -(sign d ## dir) + dw;\
    if (den == 0) t=0;\
    else t = ( sign a->dir - a->w) / den;\
    c->dir1 = a->dir1 + t * d ## dir1;\
    c->dir2 = a->dir2 + t * d ## dir2;\
    c->w = a->w + t * dw;\
    c->dir = sign c->w;\
    return t;\
}


clip_func(clip_xmin, -, x, y, z)

clip_func(clip_xmax, +, x, y, z)

clip_func(clip_ymin, -, y, x, z)

clip_func(clip_ymax, +, y, x, z)

clip_func(clip_zmin, -, z, x, y)

clip_func(clip_zmax, +, z, x, y)


static float (*clip_proc[6])(vec4 *, vec4 *, vec4 *) = {
    clip_zmin, clip_zmax,
    clip_xmin, clip_xmax,
    clip_ymin, clip_ymax
};

static inline void update_clip_pt(glVertex *q, glVertex *v0, glVertex *v1, float t)
{
    for (int i=0; i<c->vs_output.size; ++i) {
        // this is correct for both smooth and noperspective because
        // it's in clip space, pre-perspective divide
        //
        // https://www.khronos.org/opengl/wiki/Vertex_Post-Processing#Clipping
        q->vs_out[i] = v0->vs_out[i] + (v1->vs_out[i] - v0->vs_out[i]) * t;

        //PGL_FLAT should be handled indirectly by the provoke index
        //nothing to do here unless I change that
    }
    
    q->clip_code = gl_clipcode(q->clip_space);
    //q->clip_code = gl_clipcode(q->clip_space) & CLIPZ_MASK;
}




static void draw_triangle_clip(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke, int clip_bit)
{
    int c_or, c_and, c_ex_or, cc[3], edge_flag_tmp, clip_mask;
    glVertex tmp1, tmp2, *q[3];
    float tt;

    //quite a bit of stack if there's a lot of clipping ...
    float tmp1_out[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    float tmp2_out[GL_MAX_VERTEX_OUTPUT_COMPONENTS];

    tmp1.vs_out = tmp1_out;
    tmp2.vs_out = tmp2_out;

    cc[0] = v0->clip_code;
    cc[1] = v1->clip_code;
    cc[2] = v2->clip_code;
    /*
    printf("in draw_triangle_clip\n");
    print_v4(v0->clip_space, "\n");
    print_v4(v1->clip_space, "\n");
    print_v4(v2->clip_space, "\n");
    printf("tmp_out tmp2_out = %p %p\n\n", tmp1_out, tmp2_out);
    */


    c_or = cc[0] | cc[1] | cc[2];
    if (c_or == 0) {
        draw_triangle_final(v0, v1, v2, provoke);
    } else {
        c_and = cc[0] & cc[1] & cc[2];
        /* the triangle is completely outside */
        if (c_and != 0) {
            //printf("triangle outside\n");
            return;
        }

        /* find the next direction to clip */
        // TODO only clip z planes or only near
        while (clip_bit < 6 && (c_or & (1 << clip_bit)) == 0)  {
            ++clip_bit;
        }

        /* this test can be true only in case of rounding errors */
        if (clip_bit == 6) {
#if 1
            printf("Clipping error:\n");
            print_v4(v0->clip_space, "\n");
            print_v4(v1->clip_space, "\n");
            print_v4(v2->clip_space, "\n");
#endif
            return;
        }

        clip_mask = 1 << clip_bit;
        c_ex_or = (cc[0] ^ cc[1] ^ cc[2]) & clip_mask;

        if (c_ex_or)  {
            /* one point outside */

            if (cc[0] & clip_mask) { q[0]=v0; q[1]=v1; q[2]=v2; }
            else if (cc[1] & clip_mask) { q[0]=v1; q[1]=v2; q[2]=v0; }
            else { q[0]=v2; q[1]=v0; q[2]=v1; }

            tt = clip_proc[clip_bit](&tmp1.clip_space, &q[0]->clip_space, &q[1]->clip_space);
            update_clip_pt(&tmp1, q[0], q[1], tt);

            tt = clip_proc[clip_bit](&tmp2.clip_space, &q[0]->clip_space, &q[2]->clip_space);
            update_clip_pt(&tmp2, q[0], q[2], tt);

            tmp1.edge_flag = q[0]->edge_flag;
            edge_flag_tmp = q[2]->edge_flag;
            q[2]->edge_flag = 0;
            draw_triangle_clip(&tmp1, q[1], q[2], provoke, clip_bit+1);

            tmp2.edge_flag = 0;
            tmp1.edge_flag = 0; // fixed from TinyGL, was 1
            q[2]->edge_flag = edge_flag_tmp;
            draw_triangle_clip(&tmp2, &tmp1, q[2], provoke, clip_bit+1);
        } else {
            /* two points outside */

            if ((cc[0] & clip_mask) == 0) { q[0]=v0; q[1]=v1; q[2]=v2; }
            else if ((cc[1] & clip_mask) == 0) { q[0]=v1; q[1]=v2; q[2]=v0; }
            else { q[0]=v2; q[1]=v0; q[2]=v1; }

            tt = clip_proc[clip_bit](&tmp1.clip_space, &q[0]->clip_space, &q[1]->clip_space);
            update_clip_pt(&tmp1, q[0], q[1], tt);

            tt = clip_proc[clip_bit](&tmp2.clip_space, &q[0]->clip_space, &q[2]->clip_space);
            update_clip_pt(&tmp2, q[0], q[2], tt);

            tmp1.edge_flag = 0; // fixed from TinyGL, was 1
            tmp2.edge_flag = q[2]->edge_flag;
            draw_triangle_clip(q[0], &tmp1, &tmp2, provoke, clip_bit+1);
        }
    }
}

static void draw_triangle_point(glVertex* v0, glVertex* v1,  glVertex* v2, unsigned int provoke)
{
    //TODO use provoke?
    PGL_UNUSED(provoke);

    glVertex* vert[3] = { v0, v1, v2 };
    vec3 hp[3];
    hp[0] = v4_to_v3h(v0->screen_space);
    hp[1] = v4_to_v3h(v1->screen_space);
    hp[2] = v4_to_v3h(v2->screen_space);

    float poly_offset = 0;
    if (c->poly_offset_pt) {
        poly_offset = calc_poly_offset(hp[0], hp[1], hp[2]);
    }

    // TODO TinyGL uses edge_flags to determine whether to draw
    // a point here...but it doesn't work and there's no way
    // to make it work as far as I can tell.  There are hacks
    // I can do to get proper behavior but for now...meh
    for (int i=0; i<3; ++i) {
        draw_point(vert[i], poly_offset);
    }
}

static void draw_triangle_line(glVertex* v0, glVertex* v1,  glVertex* v2, unsigned int provoke)
{
    // TODO early return if no edge_flags
    vec4 s0 = v0->screen_space;
    vec4 s1 = v1->screen_space;
    vec4 s2 = v2->screen_space;

    // TODO remove redundant calc in thick_line_shader
    vec3 hp0 = v4_to_v3h(s0);
    vec3 hp1 = v4_to_v3h(s1);
    vec3 hp2 = v4_to_v3h(s2);
    float w0 = v0->screen_space.w;
    float w1 = v1->screen_space.w;
    float w2 = v2->screen_space.w;

    float poly_offset = 0;
    if (c->poly_offset_line) {
        poly_offset = calc_poly_offset(hp0, hp1, hp2);
    }

    if (c->line_smooth) {
        if (v0->edge_flag) {
            draw_aa_line(hp0, hp1, w0, w1, v0->vs_out, v1->vs_out, provoke, poly_offset);
        }
        if (v1->edge_flag) {
            draw_aa_line(hp1, hp2, w1, w2, v1->vs_out, v2->vs_out, provoke, poly_offset);
        }
        if (v2->edge_flag) {
            draw_aa_line(hp2, hp0, w2, w0, v2->vs_out, v0->vs_out, provoke, poly_offset);
        }
    } else {
        if (v0->edge_flag) {
            draw_thick_line(hp0, hp1, w0, w1, v0->vs_out, v1->vs_out, provoke, poly_offset);
        }
        if (v1->edge_flag) {
            draw_thick_line(hp1, hp2, w1, w2, v1->vs_out, v2->vs_out, provoke, poly_offset);
        }
        if (v2->edge_flag) {
            draw_thick_line(hp2, hp0, w2, w0, v2->vs_out, v0->vs_out, provoke, poly_offset);
        }
    }
}

// TODO make macro or inline?
static float calc_poly_offset(vec3 hp0, vec3 hp1, vec3 hp2)
{
    float max_depth_slope = 0;
    float dzxy[6];
    dzxy[0] = fabsf((hp1.z - hp0.z)/(hp1.x - hp0.x));
    dzxy[1] = fabsf((hp1.z - hp0.z)/(hp1.y - hp0.y));
    dzxy[2] = fabsf((hp2.z - hp1.z)/(hp2.x - hp1.x));
    dzxy[3] = fabsf((hp2.z - hp1.z)/(hp2.y - hp1.y));
    dzxy[4] = fabsf((hp0.z - hp2.z)/(hp0.x - hp2.x));
    dzxy[5] = fabsf((hp0.z - hp2.z)/(hp0.y - hp2.y));

    max_depth_slope = dzxy[0];
    for (int i=1; i<6; ++i) {
        if (dzxy[i] > max_depth_slope)
            max_depth_slope = dzxy[i];
    }

#define SMALLEST_INCR 0.000001;
    return max_depth_slope * c->poly_factor + c->poly_units * SMALLEST_INCR;
#undef SMALLEST_INCR
}

static void draw_triangle_fill(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke)
{
    vec4 p0 = v0->screen_space;
    vec4 p1 = v1->screen_space;
    vec4 p2 = v2->screen_space;

    vec3 hp0 = v4_to_v3h(p0);
    vec3 hp1 = v4_to_v3h(p1);
    vec3 hp2 = v4_to_v3h(p2);

    // TODO even worth calculating or just some constant?
    float poly_offset = 0;

    if (c->poly_offset_fill) {
        poly_offset = calc_poly_offset(hp0, hp1, hp2);
    }

    /*
    print_v4(hp0, "\n");
    print_v4(hp1, "\n");
    print_v4(hp2, "\n");

    printf("%f %f %f\n", p0.w, p1.w, p2.w);
    print_v3(hp0, "\n");
    print_v3(hp1, "\n");
    print_v3(hp2, "\n\n");
    */

    //can't think of a better/cleaner way to do this than these 8 lines
    float x_min = MIN(hp0.x, hp1.x);
    float x_max = MAX(hp0.x, hp1.x);
    float y_min = MIN(hp0.y, hp1.y);
    float y_max = MAX(hp0.y, hp1.y);

    x_min = MIN(hp2.x, x_min);
    x_max = MAX(hp2.x, x_max);
    y_min = MIN(hp2.y, y_min);
    y_max = MAX(hp2.y, y_max);

    // clipping/scissoring against side planes here
    x_min = MAX(c->lx, x_min);
    x_max = MIN(c->ux, x_max);
    y_min = MAX(c->ly, y_min);
    y_max = MIN(c->uy, y_max);
    // end clipping

    // TODO is there any point to having an int index?
    // I think I did it for OpenMP
    int ix_max = roundf(x_max);
    int iy_max = roundf(y_max);

    //form implicit lines
    Line l01 = make_Line(hp0.x, hp0.y, hp1.x, hp1.y);
    Line l12 = make_Line(hp1.x, hp1.y, hp2.x, hp2.y);
    Line l20 = make_Line(hp2.x, hp2.y, hp0.x, hp0.y);

    // Precompute line function denominators and edge tests
    float denom_l01_hp2 = line_func(&l01, hp2.x, hp2.y);
    float denom_l20_hp1 = line_func(&l20, hp1.x, hp1.y);
    float edge_test_l12 = line_func(&l12, hp0.x, hp0.y) * line_func(&l12, -1, -2.5f);
    float edge_test_l20 = line_func(&l20, hp1.x, hp1.y) * line_func(&l20, -1, -2.5f);
    float edge_test_l01 = line_func(&l01, hp2.x, hp2.y) * line_func(&l01, -1, -2.5f);

    float alpha, beta, gamma, tmp, tmp2, z;
    float fs_input[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
    float perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS*3];
    float* vs_output = &c->vs_output.output_buf[0];

    int vs_output_size = c->vs_output.size;
    for (int i=0; i<vs_output_size; ++i) {
        perspective[i] = v0->vs_out[i]/p0.w;
        perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + i] = v1->vs_out[i]/p1.w;
        perspective[2*GL_MAX_VERTEX_OUTPUT_COMPONENTS + i] = v2->vs_out[i]/p2.w;
    }
    float inv_w0 = 1.0f/p0.w;
    float inv_w1 = 1.0f/p1.w;
    float inv_w2 = 1.0f/p2.w;

    // Precompute depth mapping
    float depth_near = c->depth_range_near;
    float depth_scale = c->depth_range_far - depth_near;
    float depth_scale_half = depth_scale * 0.5f;

    int fragdepth_or_discard = c->programs.a[c->cur_program].fragdepth_or_discard;
    Shader_Builtins builtins;
    builtins.gl_InstanceID = c->builtins.gl_InstanceID;

    for (int iy = y_min; iy<iy_max; ++iy) {
        float y = iy + 0.5f;

        for (int ix = x_min; ix<ix_max; ++ix) {
            float x = ix + 0.5f;

            gamma = line_func(&l01, x, y) / denom_l01_hp2;
            beta = line_func(&l20, x, y) / denom_l20_hp1;
            alpha = 1.0f - beta - gamma;

            if (alpha >= 0.0f && beta >= 0.0f && gamma >= 0.0f) {
                if ((alpha > 0.0f || edge_test_l12 > 0.0f) &&
                    (beta  > 0.0f || edge_test_l20 > 0.0f) &&
                    (gamma > 0.0f || edge_test_l01 > 0.0f)) {
                    tmp2 = alpha*inv_w0 + beta*inv_w1 + gamma*inv_w2;

                    z = alpha * hp0.z + beta * hp1.z + gamma * hp2.z + poly_offset;
                    z = (z + 1.0f) * depth_scale_half + depth_near;

                    if (!fragdepth_or_discard && !fragment_processing(x, y, z)) {
                        continue;
                    }

                    for (int i=0; i<vs_output_size; ++i) {
                        int interp = c->vs_output.interpolation[i];
                        if (interp == PGL_SMOOTH) {
                            tmp = alpha*perspective[i] + beta*perspective[GL_MAX_VERTEX_OUTPUT_COMPONENTS + i] + gamma*perspective[2*GL_MAX_VERTEX_OUTPUT_COMPONENTS + i];
                            fs_input[i] = tmp/tmp2;
                        } else if (interp == PGL_NOPERSPECTIVE) {
                            fs_input[i] = alpha * v0->vs_out[i] + beta * v1->vs_out[i] + gamma * v2->vs_out[i];
                        } else {
                            fs_input[i] = vs_output[provoke*vs_output_size + i];
                        }
                    }

                    SET_V4(builtins.gl_FragCoord, x, y, z, tmp2);
                    builtins.discard = GL_FALSE;
                    builtins.gl_FragDepth = z;

                    c->programs.a[c->cur_program].fragment_shader(fs_input, &builtins, c->programs.a[c->cur_program].uniform);
                    if (!builtins.discard) {
                        draw_pixel(builtins.gl_FragColor, x, y, builtins.gl_FragDepth, fragdepth_or_discard);
                    }
                }
            }
        }
    }
}


// TODO should this be done in colors/integers not vec4/floats?
// and if it's done in Colors/integers what's the performance difference?
static Color blend_pixel(vec4 src, vec4 dst)
{
    vec4 bc = c->blend_color;
    float i = MIN(src.w, 1-dst.w); // in colors this would be min(src.a, 255-dst.a)/255

    // only initializing to get rid of "possibly uninitialized warning"
    vec4 Cs = {0}, Cd = {0};

    switch (c->blend_sRGB) {
    case GL_ZERO:                     SET_V4(Cs, 0,0,0,0);                                 break;
    case GL_ONE:                      SET_V4(Cs, 1,1,1,1);                                 break;
    case GL_SRC_COLOR:                Cs = src;                                              break;
    case GL_ONE_MINUS_SRC_COLOR:      SET_V4(Cs, 1-src.x,1-src.y,1-src.z,1-src.w);         break;
    case GL_DST_COLOR:                Cs = dst;                                              break;
    case GL_ONE_MINUS_DST_COLOR:      SET_V4(Cs, 1-dst.x,1-dst.y,1-dst.z,1-dst.w);         break;
    case GL_SRC_ALPHA:                SET_V4(Cs, src.w, src.w, src.w, src.w);              break;
    case GL_ONE_MINUS_SRC_ALPHA:      SET_V4(Cs, 1-src.w,1-src.w,1-src.w,1-src.w);         break;
    case GL_DST_ALPHA:                SET_V4(Cs, dst.w, dst.w, dst.w, dst.w);              break;
    case GL_ONE_MINUS_DST_ALPHA:      SET_V4(Cs, 1-dst.w,1-dst.w,1-dst.w,1-dst.w);         break;
    case GL_CONSTANT_COLOR:           Cs = bc;                                               break;
    case GL_ONE_MINUS_CONSTANT_COLOR: SET_V4(Cs, 1-bc.x,1-bc.y,1-bc.z,1-bc.w);             break;
    case GL_CONSTANT_ALPHA:           SET_V4(Cs, bc.w, bc.w, bc.w, bc.w);                  break;
    case GL_ONE_MINUS_CONSTANT_ALPHA: SET_V4(Cs, 1-bc.w,1-bc.w,1-bc.w,1-bc.w);             break;

    case GL_SRC_ALPHA_SATURATE:       SET_V4(Cs, i, i, i, 1);                              break;
    /*not implemented yet
     * won't be until I implement dual source blending/dual output from frag shader
     *https://www.opengl.org/wiki/Blending#Dual_Source_Blending
    case GL_SRC1_COLOR:               Cs =  break;
    case GL_ONE_MINUS_SRC1_COLOR:     Cs =  break;
    case GL_SRC1_ALPHA:               Cs =  break;
    case GL_ONE_MINUS_SRC1_ALPHA:     Cs =  break;
    */
    default:
        //should never get here
        puts("error unrecognized blend_sRGB!");
        break;
    }

    switch (c->blend_dRGB) {
    case GL_ZERO:                     SET_V4(Cd, 0,0,0,0);                                 break;
    case GL_ONE:                      SET_V4(Cd, 1,1,1,1);                                 break;
    case GL_SRC_COLOR:                Cd = src;                                              break;
    case GL_ONE_MINUS_SRC_COLOR:      SET_V4(Cd, 1-src.x,1-src.y,1-src.z,1-src.w);         break;
    case GL_DST_COLOR:                Cd = dst;                                              break;
    case GL_ONE_MINUS_DST_COLOR:      SET_V4(Cd, 1-dst.x,1-dst.y,1-dst.z,1-dst.w);         break;
    case GL_SRC_ALPHA:                SET_V4(Cd, src.w, src.w, src.w, src.w);              break;
    case GL_ONE_MINUS_SRC_ALPHA:      SET_V4(Cd, 1-src.w,1-src.w,1-src.w,1-src.w);         break;
    case GL_DST_ALPHA:                SET_V4(Cd, dst.w, dst.w, dst.w, dst.w);              break;
    case GL_ONE_MINUS_DST_ALPHA:      SET_V4(Cd, 1-dst.w,1-dst.w,1-dst.w,1-dst.w);         break;
    case GL_CONSTANT_COLOR:           Cd = bc;                                               break;
    case GL_ONE_MINUS_CONSTANT_COLOR: SET_V4(Cd, 1-bc.x,1-bc.y,1-bc.z,1-bc.w);             break;
    case GL_CONSTANT_ALPHA:           SET_V4(Cd, bc.w, bc.w, bc.w, bc.w);                  break;
    case GL_ONE_MINUS_CONSTANT_ALPHA: SET_V4(Cd, 1-bc.w,1-bc.w,1-bc.w,1-bc.w);             break;

    case GL_SRC_ALPHA_SATURATE:       SET_V4(Cd, i, i, i, 1);                              break;
    /*not implemented yet
    case GL_SRC_ALPHA_SATURATE:       Cd =  break;
    case GL_SRC1_COLOR:               Cd =  break;
    case GL_ONE_MINUS_SRC1_COLOR:     Cd =  break;
    case GL_SRC1_ALPHA:               Cd =  break;
    case GL_ONE_MINUS_SRC1_ALPHA:     Cd =  break;
    */
    default:
        //should never get here
        puts("error unrecognized blend_dRGB!");
        break;
    }

    // TODO simplify combine redundancies
    switch (c->blend_sA) {
    case GL_ZERO:                     Cs.w = 0;              break;
    case GL_ONE:                      Cs.w = 1;              break;
    case GL_SRC_COLOR:                Cs.w = src.w;          break;
    case GL_ONE_MINUS_SRC_COLOR:      Cs.w = 1-src.w;        break;
    case GL_DST_COLOR:                Cs.w = dst.w;          break;
    case GL_ONE_MINUS_DST_COLOR:      Cs.w = 1-dst.w;        break;
    case GL_SRC_ALPHA:                Cs.w = src.w;          break;
    case GL_ONE_MINUS_SRC_ALPHA:      Cs.w = 1-src.w;        break;
    case GL_DST_ALPHA:                Cs.w = dst.w;          break;
    case GL_ONE_MINUS_DST_ALPHA:      Cs.w = 1-dst.w;        break;
    case GL_CONSTANT_COLOR:           Cs.w = bc.w;           break;
    case GL_ONE_MINUS_CONSTANT_COLOR: Cs.w = 1-bc.w;         break;
    case GL_CONSTANT_ALPHA:           Cs.w = bc.w;           break;
    case GL_ONE_MINUS_CONSTANT_ALPHA: Cs.w = 1-bc.w;         break;

    case GL_SRC_ALPHA_SATURATE:       Cs.w = 1;              break;
    /*not implemented yet
     * won't be until I implement dual source blending/dual output from frag shader
     *https://www.opengl.org/wiki/Blending#Dual_Source_Blending
    case GL_SRC1_COLOR:               Cs =  break;
    case GL_ONE_MINUS_SRC1_COLOR:     Cs =  break;
    case GL_SRC1_ALPHA:               Cs =  break;
    case GL_ONE_MINUS_SRC1_ALPHA:     Cs =  break;
    */
    default:
        //should never get here
        puts("error unrecognized blend_sA!");
        break;
    }

    switch (c->blend_dA) {
    case GL_ZERO:                     Cd.w = 0;              break;
    case GL_ONE:                      Cd.w = 1;              break;
    case GL_SRC_COLOR:                Cd.w = src.w;          break;
    case GL_ONE_MINUS_SRC_COLOR:      Cd.w = 1-src.w;        break;
    case GL_DST_COLOR:                Cd.w = dst.w;          break;
    case GL_ONE_MINUS_DST_COLOR:      Cd.w = 1-dst.w;        break;
    case GL_SRC_ALPHA:                Cd.w = src.w;          break;
    case GL_ONE_MINUS_SRC_ALPHA:      Cd.w = 1-src.w;        break;
    case GL_DST_ALPHA:                Cd.w = dst.w;          break;
    case GL_ONE_MINUS_DST_ALPHA:      Cd.w = 1-dst.w;        break;
    case GL_CONSTANT_COLOR:           Cd.w = bc.w;           break;
    case GL_ONE_MINUS_CONSTANT_COLOR: Cd.w = 1-bc.w;         break;
    case GL_CONSTANT_ALPHA:           Cd.w = bc.w;           break;
    case GL_ONE_MINUS_CONSTANT_ALPHA: Cd.w = 1-bc.w;         break;

    case GL_SRC_ALPHA_SATURATE:       Cd.w = 1;              break;
    /*not implemented yet
    case GL_SRC_ALPHA_SATURATE:       Cd =  break;
    case GL_SRC1_COLOR:               Cd =  break;
    case GL_ONE_MINUS_SRC1_COLOR:     Cd =  break;
    case GL_SRC1_ALPHA:               Cd =  break;
    case GL_ONE_MINUS_SRC1_ALPHA:     Cd =  break;
    */
    default:
        //should never get here
        puts("error unrecognized blend_dA!");
        break;
    }

    vec4 result;

    // TODO eliminate function calls to avoid alpha component calculations?
    switch (c->blend_eqRGB) {
    case GL_FUNC_ADD:
        result = add_v4s(mult_v4s(Cs, src), mult_v4s(Cd, dst));
        break;
    case GL_FUNC_SUBTRACT:
        result = sub_v4s(mult_v4s(Cs, src), mult_v4s(Cd, dst));
        break;
    case GL_FUNC_REVERSE_SUBTRACT:
        result = sub_v4s(mult_v4s(Cd, dst), mult_v4s(Cs, src));
        break;
    case GL_MIN:
        SET_V4(result, MIN(src.x, dst.x), MIN(src.y, dst.y), MIN(src.z, dst.z), MIN(src.w, dst.w));
        break;
    case GL_MAX:
        SET_V4(result, MAX(src.x, dst.x), MAX(src.y, dst.y), MAX(src.z, dst.z), MAX(src.w, dst.w));
        break;
    default:
        //should never get here
        puts("error unrecognized blend_eqRGB!");
        break;
    }

    switch (c->blend_eqA) {
    case GL_FUNC_ADD:
        result.w = Cs.w*src.w + Cd.w*dst.w;
        break;
    case GL_FUNC_SUBTRACT:
        result.w = Cs.w*src.w - Cd.w*dst.w;
        break;
    case GL_FUNC_REVERSE_SUBTRACT:
        result.w = Cd.w*dst.w - Cs.w*src.w;
        break;
    case GL_MIN:
        result.w = MIN(src.w, dst.w);
        break;
    case GL_MAX:
        result.w = MAX(src.w, dst.w);
        break;
    default:
        //should never get here
        puts("error unrecognized blend_eqRGB!");
        break;
    }

    // TODO should I clamp in v4_to_Color() instead
    result = clamp_01_v4(result);
    return v4_to_Color(result);
}

// source and destination colors
static pix_t logic_ops_pixel(pix_t s, pix_t d)
{
    switch (c->logic_func) {
    case GL_CLEAR:
        return 0;
    case GL_SET:
        return ~0;
    case GL_COPY:
        return s;
    case GL_COPY_INVERTED:
        return ~s;
    case GL_NOOP:
        return d;
    case GL_INVERT:
        return ~d;
    case GL_AND:
        return s & d;
    case GL_NAND:
        return ~(s & d);
    case GL_OR:
        return s | d;
    case GL_NOR:
        return ~(s | d);
    case GL_XOR:
        return s ^ d;
    case GL_EQUIV:
        return ~(s ^ d);
    case GL_AND_REVERSE:
        return s & ~d;
    case GL_AND_INVERTED:
        return ~s & d;
    case GL_OR_REVERSE:
        return s | ~d;
    case GL_OR_INVERTED:
        return ~s | d;
    default:
        puts("Unrecognized logic op!, defaulting to GL_COPY");
        return s;
    }

}

#ifndef PGL_NO_STENCIL
static int stencil_test(u8 stencil)
{
    int func, ref, mask;
    // TODO what about non-triangles, should use front values, so need to make sure that's set?
    if (c->builtins.gl_FrontFacing) {
        func = c->stencil_func;
        ref = c->stencil_ref;
        mask = c->stencil_valuemask;
    } else {
        func = c->stencil_func_back;
        ref = c->stencil_ref_back;
        mask = c->stencil_valuemask_back;
    }
    switch (func) {
    case GL_NEVER:    return 0;
    case GL_LESS:     return (ref & mask) < (stencil & mask);
    case GL_LEQUAL:   return (ref & mask) <= (stencil & mask);
    case GL_GREATER:  return (ref & mask) > (stencil & mask);
    case GL_GEQUAL:   return (ref & mask) >= (stencil & mask);
    case GL_EQUAL:    return (ref & mask) == (stencil & mask);
    case GL_NOTEQUAL: return (ref & mask) != (stencil & mask);
    case GL_ALWAYS:   return 1;
    default:
        puts("Error: unrecognized stencil function!");
        return 0;
    }

}

// TODO change ints to GLboolean? or just rename to indicate they're booleans?
// stencil_dest is pointer to stencil pixel which may be u32 for PGL_D24S8
static void stencil_op(int stencil, int depth, void* stencil_dest)
{
    GLuint op, ref, mask;
    // TODO make them proper arrays in gl_context?
    // ops is effectively set to an array of sfail, dpfail, dppass for either front
    // or back facing
    GLenum* ops;
    // TODO what about non-triangles, should use front values, so need to make sure that's set?
    if (c->builtins.gl_FrontFacing) {
        ops = &c->stencil_sfail;
        ref = c->stencil_ref;
        mask = c->stencil_writemask;
    } else {
        ops = &c->stencil_sfail_back;
        ref = c->stencil_ref_back;
        mask = c->stencil_writemask_back;
    }
    op = (!stencil) ? ops[0] : ((!depth) ? ops[1] : ops[2]);

    stencil_pix_t orig = *(stencil_pix_t*)stencil_dest;

    // TODO check C conversion guide...is there a point to masking the low byte?
#ifdef PGL_D16
    u8 val = orig;
#else
    u8 val = orig & PGL_STENCIL_MASK;
#endif

    switch (op) {
    case GL_KEEP: return;
    case GL_ZERO: val = 0; break;
    case GL_REPLACE: val = ref; break;
    case GL_INCR: if (val < 255) val++; break;
    case GL_INCR_WRAP: val++; break;
    case GL_DECR: if (val > 0) val--; break;
    case GL_DECR_WRAP: val--; break;
    case GL_INVERT: val = ~val;
    }

    // TODO is this sufficient? It doesn't really write protect
    // the bits not covered by the mask, it will just write 0 there
    //u8 result = val & mask;
    // TODO create a stencil test to verify correct behavior
    u8 result = (orig & ~mask) | (val & mask);

#ifdef PGL_D16
    *(u8*)stencil_dest = result;
#else
    *(u32*)stencil_dest = (orig & ~PGL_STENCIL_MASK) | result;
#endif
}

// end PGL_NO_STENCIL
#endif

/*
 * spec pg 110:
Point rasterization produces a fragment for each framebuffer pixel whose center
lies inside a square centered at the point’s (x w , y w ), with side length equal to the
current point size.

for a 1 pixel size point there are only 3 edge cases where more than 1 pixel center (0.5, 0.5)
would fall on the very edge of a 1 pixel square.  I think just drawing the upper or upper
corner pixel in these cases is fine and makes sense since width and height are actually 0.01 less
than full, see make_viewport_matrix
*/

static int fragment_processing(int x, int y, float z)
{
#ifndef PGL_NO_DEPTH_NO_STENCIL
    // NOTE/TODO assumes all 3 buffers have the same dimensions
    int i = -y*c->zbuf.w + x;

#ifndef PGL_NO_STENCIL
    // Stencil Test
    if (c->stencil_test) {
        stencil_pix_t* stencil_dest = &GET_STENCIL_PIX(i);
        if (!stencil_test(EXTRACT_STENCIL(*stencil_dest))) {
            stencil_op(GL_FALSE, GL_TRUE, stencil_dest);
            return 0;
        }
        // Depth test
        if (c->depth_test) {
            u32 dest_depth = GET_Z(i);
            u32 src_depth = z * PGL_MAX_Z;
            int depth_result = depthtest(src_depth, dest_depth);
            stencil_op(GL_TRUE, depth_result, stencil_dest);
            if (!depth_result) return GL_FALSE;
            SET_Z(i, src_depth & -(c->depth_mask != 0));
        } else {
            stencil_op(GL_TRUE, GL_TRUE, stencil_dest);
        }
    } else
#endif
    {
        // Depth test only (no stencil)
        if (c->depth_test) {
            u32 dest_depth = GET_Z(i);
            u32 src_depth = z * PGL_MAX_Z;
            if (!depthtest(src_depth, dest_depth)) return GL_FALSE;
            if (c->depth_mask) SET_Z(i, src_depth);
        }
    }
    return GL_TRUE;
#else
    return GL_TRUE;
#endif
}


static void draw_pixel(vec4 cf, int x, int y, float z, int do_frag_processing)
{
    if (do_frag_processing && !fragment_processing(x, y, z)) {
        return;
    }

    pix_t* dest_loc = &((pix_t*)c->back_buffer.lastrow)[-y*c->back_buffer.w + x];
    pix_t dst = *dest_loc;
    pix_t src;

    if (c->blend) {
        Color dest_color = PIXEL_TO_COLOR(dst);
        Color src_color = blend_pixel(cf, COLOR_TO_VEC4(dest_color));
        src = RGBA_TO_PIXEL(src_color.r, src_color.g, src_color.b, src_color.a);
    } else {
        cf = clamp_01_v4(cf);
        Color src_color = VEC4_TO_COLOR(cf);
        src = RGBA_TO_PIXEL(src_color.r, src_color.g, src_color.b, src_color.a);
    }

    if (c->logic_ops) {
        src = logic_ops_pixel(src, dst);
    }

#ifndef PGL_DISABLE_COLOR_MASK
    src = (src & c->color_mask) | (dst & ~c->color_mask);
#endif

    *dest_loc = src;
}



#include <stdarg.h>


/******************************************
 * PORTABLEGL_IMPLEMENTATION
 ******************************************/

#include <stdio.h>
#include <float.h>

// for CHAR_BIT
#include <limits.h>

// TODO different name? NO_ERROR_CHECKING? LOOK_MA_NO_HANDS?
#ifdef PGL_UNSAFE
#define PGL_SET_ERR(err)
#define PGL_ERR(check, err)
#define PGL_ERR_RET_VAL(check, err, ret)
#define PGL_LOG(err)
#else
#define PGL_LOG(err) \
    do { \
        if (c->dbg_output && c->dbg_callback) { \
            int len = snprintf(c->dbg_msg_buf, PGL_MAX_DEBUG_MESSAGE_LENGTH, "%s in %s() at %s:%d", pgl_err_strs[err-GL_NO_ERROR], __func__, __FILE__, __LINE__); \
            c->dbg_callback(GL_DEBUG_SOURCE_API, GL_DEBUG_TYPE_ERROR, 0, GL_DEBUG_SEVERITY_HIGH, len, c->dbg_msg_buf, c->dbg_userparam); \
        } \
    } while (0)

#define PGL_SET_ERR(err) \
    do { \
        if (!c->error) c->error = err; \
        PGL_LOG(err); \
    } while (0)

#define PGL_ERR(check, err) \
    do { \
        if (check) {  \
            if (!c->error) c->error = err; \
            PGL_LOG(err); \
            return; \
        } \
    } while (0)

#define PGL_ERR_RET_VAL(check, err, ret) \
    do { \
        if (check) {  \
            if (!c->error) c->error = err; \
            PGL_LOG(err); \
            return ret; \
        } \
    } while (0)
#endif

// I just set everything even if not everything applies to the type
// see section 3.8.15 pg 181 of spec for what it's supposed to be
// TODO better name and inline?
static void INIT_TEX(glTexture* tex, GLenum target)
{
    tex->type = target;
    tex->mag_filter = GL_LINEAR;
    if (target != GL_TEXTURE_RECTANGLE) {
        //tex->min_filter = GL_NEAREST_MIPMAP_LINEAR;
        tex->min_filter = GL_NEAREST;
        tex->wrap_s = GL_REPEAT;
        tex->wrap_t = GL_REPEAT;
        tex->wrap_r = GL_REPEAT;
    } else {
        tex->min_filter = GL_LINEAR;
        tex->wrap_s = GL_CLAMP_TO_EDGE;
        tex->wrap_t = GL_CLAMP_TO_EDGE;
        tex->wrap_r = GL_CLAMP_TO_EDGE;
    }
    tex->data = NULL;
    tex->deleted = GL_FALSE;
    tex->user_owned = GL_TRUE;
    tex->format = GL_RGBA;
    tex->w = 0;
    tex->h = 0;
    tex->d = 0;

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    tex->border_color = make_v4(0,0,0,0);
#endif
}

// default pass through shaders for index 0
PGLDEF void default_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(vs_output);
    PGL_UNUSED(uniforms);

    builtins->gl_Position = vertex_attribs[PGL_ATTR_VERT];
}

PGLDEF void default_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(fs_input);
    PGL_UNUSED(uniforms);

    vec4* fragcolor = &builtins->gl_FragColor;
    //wish I could use a compound literal, stupid C++ compatibility
    fragcolor->x = 1.0f;
    fragcolor->y = 0.0f;
    fragcolor->z = 0.0f;
    fragcolor->w = 1.0f;
}

// TODO Where to put this and what to name it? move this and all static functions to gl_internal.c?
#ifndef PGL_UNSAFE
static const char* pgl_err_strs[] =
{
    "GL_NO_ERROR",
    "GL_INVALID_ENUM",
    "GL_INVALID_VALUE",
    "GL_INVALID_OPERATION",
    "GL_INVALID_FRAMEBUFFER_OPERATION",
    "GL_OUT_OF_MEMORY"
};

PGLDEF void dflt_dbg_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
    PGL_UNUSED(source);
    PGL_UNUSED(type);
    PGL_UNUSED(id);
    PGL_UNUSED(severity);
    PGL_UNUSED(length);
    PGL_UNUSED(userParam);

    fprintf(stderr, "%s\n", message);
}
#endif

static void init_glVertex_Attrib(glVertex_Attrib* v)
{
    /*
    GLint size;      // number of components 1-4
    GLenum type;     // GL_FLOAT, default
    GLsizei stride;  //
    GLsizei offset;  //
    GLboolean normalized;
    unsigned int buf;
    GLboolean enabled;
    GLuint divisor;
*/
    v->buf = 0;
    v->enabled = 0;
    v->divisor = 0;
}

// TODO these are currently equivalent to memset(0) or = {0}...
static void init_glVertex_Array(glVertex_Array* v)
{
    v->deleted = GL_FALSE;
    for (int i=0; i<GL_MAX_VERTEX_ATTRIBS; ++i)
        init_glVertex_Attrib(&v->vertex_attribs[i]);
}

#define GET_SHIFT(mask, shift) \
    do {\
    shift = 0;\
    while ((mask & 1) == 0) {\
        mask >>= 1;\
        ++shift;\
    }\
    } while (0)


PGLDEF GLboolean init_glContext(glContext* context, pix_t** back, GLsizei w, GLsizei h)
{
    PGL_ERR_RET_VAL(!back, GL_INVALID_VALUE, GL_FALSE);
    PGL_ERR_RET_VAL((w < 0 || h < 0), GL_INVALID_VALUE, GL_FALSE);

    c = context;
    memset(c, 0, sizeof(glContext));

    if (w && h && *back != NULL) {
        c->user_alloced_backbuf = GL_TRUE;
        c->back_buffer.buf = (u8*)*back;
        c->back_buffer.w = w;
        c->back_buffer.h = h;
        c->back_buffer.lastrow = c->back_buffer.buf + (h-1)*w*sizeof(pix_t);
    }

    c->xmin = 0;
    c->ymin = 0;
    c->width = w;
    c->height = h;

    c->color_mask = ~0;

    //initialize all vectors
    cvec_glVertex_Array(&c->vertex_arrays, 0, 3);
    cvec_glBuffer(&c->buffers, 0, 3);
    cvec_glProgram(&c->programs, 0, 3);
    cvec_glTexture(&c->textures, 0, 1);
    cvec_glVertex(&c->glverts, 0, 10);

    // If not pre-allocating max, need to track size and edit glUseProgram and pglSetInterp
    int max_vertices = get_max_vertices();
    c->vs_output.output_buf = (float*)PGL_MALLOC(max_vertices * GL_MAX_VERTEX_OUTPUT_COMPONENTS * sizeof(float));
    PGL_ERR_RET_VAL(!c->vs_output.output_buf, GL_OUT_OF_MEMORY, GL_FALSE);

    c->clear_color = 0;
    SET_V4(c->blend_color, 0, 0, 0, 0);
    c->point_size = 1.0f;
    c->line_width = 1.0f;
    c->clear_depth = 1.0f;
    c->depth_range_near = 0.0f;
    c->depth_range_far = 1.0f;
    make_viewport_m4(c->vp_mat, 0, 0, w, h, 1);

    //set flags
    //TODO match order in structure definition
    c->provoking_vert = GL_LAST_VERTEX_CONVENTION;
    c->cull_mode = GL_BACK;
    c->cull_face = GL_FALSE;
    c->front_face = GL_CCW;
    c->depth_test = GL_FALSE;
    c->fragdepth_or_discard = GL_FALSE;
    c->depth_clamp = GL_FALSE;
    c->depth_mask = GL_TRUE;
    c->blend = GL_FALSE;
    c->logic_ops = GL_FALSE;
    c->poly_offset_pt = GL_FALSE;
    c->poly_offset_line = GL_FALSE;
    c->poly_offset_fill = GL_FALSE;
    c->scissor_test = GL_FALSE;

#ifndef PGL_NO_STENCIL
    c->clear_stencil = 0;

    c->stencil_test = GL_FALSE;
    c->stencil_writemask = -1; // all 1s for the masks
    c->stencil_writemask_back = -1;
    c->stencil_ref = 0;
    c->stencil_ref_back = 0;
    c->stencil_valuemask = -1;
    c->stencil_valuemask_back = -1;
    c->stencil_func = GL_ALWAYS;
    c->stencil_func_back = GL_ALWAYS;
    c->stencil_sfail = GL_KEEP;
    c->stencil_dpfail = GL_KEEP;
    c->stencil_dppass = GL_KEEP;
    c->stencil_sfail_back = GL_KEEP;
    c->stencil_dpfail_back = GL_KEEP;
    c->stencil_dppass_back = GL_KEEP;
#endif

    c->logic_func = GL_COPY;
    c->blend_sRGB = GL_ONE;
    c->blend_sA = GL_ONE;
    c->blend_dRGB = GL_ZERO;
    c->blend_dA = GL_ZERO;
    c->blend_eqRGB = GL_FUNC_ADD;
    c->blend_eqA = GL_FUNC_ADD;
    c->depth_func = GL_LESS;
    c->line_smooth = GL_FALSE;
    c->poly_mode_front = GL_FILL;
    c->poly_mode_back = GL_FILL;
    c->point_spr_origin = GL_UPPER_LEFT;

    c->poly_factor = 0.0f;
    c->poly_units = 0.0f;

    c->scissor_lx = 0;
    c->scissor_ly = 0;
    c->scissor_w = w;
    c->scissor_h = h;

    // According to refpages https://www.khronos.org/registry/OpenGL-Refpages/gl4/html/glPixelStore.xhtml
    c->unpack_alignment = 4;
    c->pack_alignment = 4;

    c->draw_triangle_front = draw_triangle_fill;
    c->draw_triangle_back = draw_triangle_fill;

    c->error = GL_NO_ERROR;
#ifndef PGL_UNSAFE
    c->dbg_callback = dflt_dbg_callback;
    c->dbg_output = GL_TRUE;
#else
    c->dbg_callback = NULL;
    c->dbg_output = GL_FALSE;
#endif

    // program 0 is supposed to be undefined but not invalid so I'll
    // just make it default, no transform, just draws things red
    glProgram tmp_prog = { default_vs, default_fs, NULL, 0, {0}, GL_FALSE, GL_FALSE };
    cvec_push_glProgram(&c->programs, tmp_prog);
    glUseProgram(0);

    // setup default vertex_array (vao) at position 0
    // we're like a compatibility profile for this but come on
    // no reason not to have this imo
    // https://www.opengl.org/wiki/Vertex_Specification#Vertex_Array_Object
    glVertex_Array tmp_va;
    init_glVertex_Array(&tmp_va);
    cvec_push_glVertex_Array(&c->vertex_arrays, tmp_va);
    c->cur_vertex_array = 0;

    // buffer 0 is invalid
    glBuffer tmp_buf = {0};
    tmp_buf.user_owned = GL_TRUE;
    tmp_buf.deleted = GL_FALSE;
    cvec_push_glBuffer(&c->buffers, tmp_buf);

    // From glBindTexture():
    // "The value zero is reserved to represent the default texture for each texture target."
    // "In effect, the texture targets become aliases for the textures currently bound to them, and the texture name zero refers to the default textures that were bound to them at initialization."
    //
    // ... which means we can't use the 0 index at all as it can obviously only
    // be one type/target at a time and it would be a pain regardless
    // Still we might as well initialize it since something has to be there
    glTexture tmp_tex;
    INIT_TEX(&tmp_tex, GL_TEXTURE_UNBOUND);
    cvec_push_glTexture(&c->textures, tmp_tex);

    // Initialize the actual default textures..
    // TODO Should I initialize them as their actual types? no
    // Should I do the non-spec white pixel thing?
    for (int i=0; i<GL_NUM_TEXTURE_TYPES-GL_TEXTURE_UNBOUND-1; i++) {
        INIT_TEX(&c->default_textures[i], GL_TEXTURE_UNBOUND);
    }

    // default texture (0) is bound to all targets initially
    memset(c->bound_textures, 0, sizeof(c->bound_textures));

    // invalid buffer (0) bound initially
    memset(c->bound_buffers, 0, sizeof(c->bound_buffers));

    // DRY, do all buffer allocs/init in here
    if (w && h && !pglResizeFramebuffer(w, h)) {
#ifndef PGL_NO_DEPTH_NO_STENCIL
        PGL_FREE(c->zbuf.buf);
#if defined(PGL_D16) && !defined(PGL_NO_STENCIL)
        PGL_FREE(c->stencil_buf.buf);
#endif
#endif
        if (!c->user_alloced_backbuf) {
            PGL_FREE(c->back_buffer.buf);
        }
        return GL_FALSE;
    }

    *back = (pix_t*)c->back_buffer.buf;

    return GL_TRUE;
}

PGLDEF void free_glContext(glContext* ctx)
{
    int i;
#ifndef PGL_NO_DEPTH_NO_STENCIL
    PGL_FREE(ctx->zbuf.buf);
#  if defined(PGL_D16) && !defined(PGL_NO_STENCIL)
    PGL_FREE(ctx->stencil_buf.buf);
#  endif
#endif
    if (!ctx->user_alloced_backbuf) {
        PGL_FREE(ctx->back_buffer.buf);
    }

    for (i=0; i<ctx->buffers.size; ++i) {
        if (!ctx->buffers.a[i].user_owned) {
            PGL_FREE(ctx->buffers.a[i].data);
        }
    }

    for (i=0; i<ctx->textures.size; ++i) {
        if (!ctx->textures.a[i].user_owned) {
            PGL_FREE(ctx->textures.a[i].data);
        }
    }

    //free vectors
    cvec_free_glVertex_Array(&ctx->vertex_arrays);
    cvec_free_glBuffer(&ctx->buffers);
    cvec_free_glProgram(&ctx->programs);
    cvec_free_glTexture(&ctx->textures);
    cvec_free_glVertex(&ctx->glverts);

    PGL_FREE(ctx->vs_output.output_buf);

    if (c == ctx) {
        c = NULL;
    }
}

PGLDEF void set_glContext(glContext* context)
{
    c = context;
}

PGLDEF GLboolean pglResizeFramebuffer(GLsizei w, GLsizei h)
{
    PGL_ERR_RET_VAL((w < 0 || h < 0), GL_INVALID_VALUE, GL_FALSE);

    // TODO C standard doesn't guarantee that passing the same size to
    // realloc is a no-op and will return the same pointer
    // NOTE checking zbuf because of the separation between pglSetBackBuffer()
    // and pglResizeFramebuffer(). If the former is called before the latter
    // backbuf dimensions would compare the same to the new size even when
    // we still need to update stencil and zbuf
    if (w == c->zbuf.w && h == c->zbuf.h) {
        return GL_TRUE; // no resize necessary = success to me
    }

    u8* tmp;

    if (!c->user_alloced_backbuf) {
        tmp = (u8*)PGL_REALLOC(c->back_buffer.buf, w*h * sizeof(pix_t));
        PGL_ERR_RET_VAL(!tmp, GL_OUT_OF_MEMORY, GL_FALSE);
        c->back_buffer.buf = tmp;
        c->back_buffer.w = w;
        c->back_buffer.h = h;
        c->back_buffer.lastrow = c->back_buffer.buf + (h-1)*w*sizeof(pix_t);
    }

#ifdef PGL_D24S8
    tmp = (u8*)PGL_REALLOC(c->zbuf.buf, w*h * sizeof(u32));
    PGL_ERR_RET_VAL(!tmp, GL_OUT_OF_MEMORY, GL_FALSE);

    c->zbuf.buf = tmp;
    c->zbuf.w = w;
    c->zbuf.h = h;
    c->zbuf.lastrow = c->zbuf.buf + (h-1)*w*sizeof(u32);

    // not checking for NO_STENCIL here because it makes no sense not to
    // have it if you're already using the space
    c->stencil_buf.buf = tmp;
    c->stencil_buf.w = w;
    c->stencil_buf.h = h;
    c->stencil_buf.lastrow = c->stencil_buf.buf + (h-1)*w*sizeof(u32);
#elif defined(PGL_D16)
    tmp = (u8*)PGL_REALLOC(c->zbuf.buf, w*h * sizeof(u16));
    PGL_ERR_RET_VAL(!tmp, GL_OUT_OF_MEMORY, GL_FALSE);

    c->zbuf.buf = tmp;
    c->zbuf.w = w;
    c->zbuf.h = h;
    c->zbuf.lastrow = c->zbuf.buf + (h-1)*w*sizeof(u16);

#ifndef PGL_NO_STENCIL
    tmp = (u8*)PGL_REALLOC(c->stencil_buf.buf, w*h);
    PGL_ERR_RET_VAL(!tmp, GL_OUT_OF_MEMORY, GL_FALSE);
    c->stencil_buf.buf = tmp;
    c->stencil_buf.w = w;
    c->stencil_buf.h = h;
    c->stencil_buf.lastrow = c->stencil_buf.buf + (h-1)*w;
#endif
#endif

    if (c->scissor_test) {
        int ux = c->scissor_lx+c->scissor_w;
        int uy = c->scissor_ly+c->scissor_h;

        c->lx = MAX(c->scissor_lx, 0);
        c->ly = MAX(c->scissor_ly, 0);
        c->ux = MIN(ux, w);
        c->uy = MIN(uy, h);
    } else {
        c->lx = 0;
        c->ly = 0;
        c->ux = w;
        c->uy = h;
    }

    return GL_TRUE;
}



PGLDEF GLubyte* glGetString(GLenum name)
{
    static GLubyte vendor[] = "Robert Winkler (robertwinkler.com)";
    static GLubyte renderer[] = "PortableGL 0.101.0";
    static GLubyte version[] = "0.101.0";
    static GLubyte shading_language[] = "C/C++";

    switch (name) {
    case GL_VENDOR:                   return vendor;
    case GL_RENDERER:                 return renderer;
    case GL_VERSION:                  return version;
    case GL_SHADING_LANGUAGE_VERSION: return shading_language;
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
        return NULL;
    }
}

PGLDEF GLenum glGetError(void)
{
    GLenum err = c->error;
    c->error = GL_NO_ERROR;
    return err;
}

PGLDEF void glGenVertexArrays(GLsizei n, GLuint* arrays)
{
    PGL_ERR(n < 0, GL_INVALID_VALUE);

    glVertex_Array tmp = {0};
    //init_glVertex_Array(&tmp);
    tmp.deleted = GL_FALSE;

    //fill up empty slots first
    --n;
    for (int i=1; i<c->vertex_arrays.size && n>=0; ++i) {
        if (c->vertex_arrays.a[i].deleted) {
            c->vertex_arrays.a[i] = tmp;
            arrays[n--] = i;
        }
    }

    for (; n>=0; --n) {
        cvec_push_glVertex_Array(&c->vertex_arrays, tmp);
        arrays[n] = c->vertex_arrays.size-1;
    }
}

PGLDEF void glDeleteVertexArrays(GLsizei n, const GLuint* arrays)
{
    PGL_ERR(n < 0, GL_INVALID_VALUE);
    for (int i=0; i<n; ++i) {
        if (!arrays[i] || arrays[i] >= c->vertex_arrays.size)
            continue;

        // NOTE/TODO: This is non-standard behavior even in a compatibility profile but it
        // is similar to (from the user's perspective) how GL handles DeleteProgram called on
        // the active program.  So instead of getting a blank screen immediately, you just
        // free up the name moving the current vao to the default 0. Of course if you're switching
        // between VAOs and bind to the old name, you will get a GL error even if it still works
        // (because VAOS are POD and I don't overwrite it)... so maybe I should just have an
        // error here
        if (arrays[i] == c->cur_vertex_array) {
            memcpy(&c->vertex_arrays.a[0], &c->vertex_arrays.a[arrays[i]], sizeof(glVertex_Array));
            c->cur_vertex_array = 0;
        }

        c->vertex_arrays.a[arrays[i]].deleted = GL_TRUE;
    }
}

PGLDEF void glGenBuffers(GLsizei n, GLuint* buffers)
{
    PGL_ERR(n < 0, GL_INVALID_VALUE);
    //fill up empty slots first
    int j = 0;
    for (int i=1; i<c->buffers.size && j<n; ++i) {
        if (c->buffers.a[i].deleted) {
            c->buffers.a[i].deleted = GL_FALSE;
            buffers[j++] = i;
        }
    }

    if (j != n) {
        int s = c->buffers.size;
        cvec_extend_glBuffer(&c->buffers, n-j);
        for (int i=s; j<n; i++) {
            c->buffers.a[i].data = NULL;
            c->buffers.a[i].deleted = GL_FALSE;
            c->buffers.a[i].user_owned = GL_FALSE;
            buffers[j++] = i;
        }
    }
}

PGLDEF void glDeleteBuffers(GLsizei n, const GLuint* buffers)
{
    PGL_ERR(n < 0, GL_INVALID_VALUE);
    GLenum type;
    for (int i=0; i<n; ++i) {
        if (!buffers[i] || buffers[i] >= c->buffers.size)
            continue;

        // NOTE(rswinkle): type is stored as correct index not the raw enum value so no need to
        // subtract here see glBindBuffer
        type = c->buffers.a[buffers[i]].type;
        if (buffers[i] == c->bound_buffers[type])
            c->bound_buffers[type] = 0;

        if (!c->buffers.a[buffers[i]].user_owned) {
            PGL_FREE(c->buffers.a[buffers[i]].data);
        }
        c->buffers.a[buffers[i]].data = NULL;
        c->buffers.a[buffers[i]].deleted = GL_TRUE;
        c->buffers.a[buffers[i]].user_owned = GL_FALSE;
    }
}

PGLDEF void glGenTextures(GLsizei n, GLuint* textures)
{
    PGL_ERR(n < 0, GL_INVALID_VALUE);
    int j = 0;
    for (int i=1; i<c->textures.size && j<n; ++i) {
        if (c->textures.a[i].deleted) {
            c->textures.a[i].deleted = GL_FALSE;
            c->textures.a[i].type = GL_TEXTURE_UNBOUND;
            textures[j++] = i;
        }
    }
    if (j != n) {
        int s = c->textures.size;
        cvec_extend_glTexture(&c->textures, n-j);
        for (int i=s; j<n; i++) {
            c->textures.a[i].deleted = GL_FALSE;
            c->textures.a[i].type = GL_TEXTURE_UNBOUND;
            c->textures.a[i].user_owned = GL_FALSE;
            c->textures.a[i].data = NULL;
            textures[j++] = i;
        }
    }
}

PGLDEF void glCreateTextures(GLenum target, GLsizei n, GLuint* textures)
{
    PGL_ERR((target < GL_TEXTURE_1D || target >= GL_NUM_TEXTURE_TYPES), GL_INVALID_ENUM);
    PGL_ERR(n < 0, GL_INVALID_VALUE);

    target -= GL_TEXTURE_UNBOUND + 1;
    int j = 0;
    for (int i=1; i<c->textures.size && j<n; ++i) {
        if (c->textures.a[i].deleted) {
            INIT_TEX(&c->textures.a[i], target);
            textures[j++] = i;
        }
    }
    if (j != n) {
        int s = c->textures.size;
        cvec_extend_glTexture(&c->textures, n-j);
        for (int i=s; j<n; i++) {
            INIT_TEX(&c->textures.a[i], target);
            textures[j++] = i;
        }
    }
}

PGLDEF void glDeleteTextures(GLsizei n, const GLuint* textures)
{
    PGL_ERR(n < 0, GL_INVALID_VALUE);
    GLenum type;
    for (int i=0; i<n; ++i) {
        if (!textures[i] || textures[i] >= c->textures.size)
            continue;

        // NOTE(rswinkle): type is stored as correct index not the raw enum value
        // so no need to subtract here see glBindTexture
        type = c->textures.a[textures[i]].type;
        if (textures[i] == c->bound_textures[type])
            c->bound_textures[type] = 0;

        if (!c->textures.a[textures[i]].user_owned) {
            PGL_FREE(c->textures.a[textures[i]].data);
        }

        c->textures.a[textures[i]].type = GL_TEXTURE_UNBOUND;
        c->textures.a[textures[i]].data = NULL;
        c->textures.a[textures[i]].deleted = GL_TRUE;
        c->textures.a[textures[i]].user_owned = GL_FALSE;
    }
}

PGLDEF void glBindVertexArray(GLuint array)
{
    PGL_ERR((array >= c->vertex_arrays.size || c->vertex_arrays.a[array].deleted), GL_INVALID_OPERATION);

    c->cur_vertex_array = array;
    c->bound_buffers[GL_ELEMENT_ARRAY_BUFFER-GL_ARRAY_BUFFER] = c->vertex_arrays.a[array].element_buffer;
}

PGLDEF void glBindBuffer(GLenum target, GLuint buffer)
{
    PGL_ERR(target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER, GL_INVALID_ENUM);

    PGL_ERR((buffer >= c->buffers.size || c->buffers.a[buffer].deleted), GL_INVALID_OPERATION);

    target -= GL_ARRAY_BUFFER;
    c->bound_buffers[target] = buffer;

    // Note type isn't set till binding and we're not storing the raw
    // enum but the enum - GL_ARRAY_BUFFER so it's an index into c->bound_buffers
    // TODO need to see what's supposed to happen if you try to bind
    // a buffer to multiple targets
    c->buffers.a[buffer].type = target;

    if (target == GL_ELEMENT_ARRAY_BUFFER - GL_ARRAY_BUFFER) {
        c->vertex_arrays.a[c->cur_vertex_array].element_buffer = buffer;
    }
}

// TODO reuse code, call glNamedBufferData() internally, remove duplicated error checks?
PGLDEF void glBufferData(GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage)
{
    //TODO check for usage later
    PGL_UNUSED(usage);

    PGL_ERR((target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER), GL_INVALID_ENUM);
    PGL_ERR(size < 0, GL_INVALID_VALUE);

    target -= GL_ARRAY_BUFFER;
    PGL_ERR(!c->bound_buffers[target], GL_INVALID_OPERATION);

    // the spec says any pre-existing data store is deleted but there's no reason to
    // c->buffers.a[c->bound_buffers[target]].data is always NULL or valid
    u8* tmp = (u8*)PGL_REALLOC(c->buffers.a[c->bound_buffers[target]].data, size);
    PGL_ERR(!tmp, GL_OUT_OF_MEMORY);

    c->buffers.a[c->bound_buffers[target]].data = tmp;

    if (data) {
        memcpy(c->buffers.a[c->bound_buffers[target]].data, data, size);
    }

    c->buffers.a[c->bound_buffers[target]].user_owned = GL_FALSE;
    c->buffers.a[c->bound_buffers[target]].size = size;
}

PGLDEF void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid* data)
{
    PGL_ERR(target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER, GL_INVALID_ENUM);
    PGL_ERR((offset < 0 || size < 0), GL_INVALID_VALUE);

    target -= GL_ARRAY_BUFFER;

    PGL_ERR(!c->bound_buffers[target], GL_INVALID_OPERATION);
    PGL_ERR((offset + size > c->buffers.a[c->bound_buffers[target]].size), GL_INVALID_VALUE);

    memcpy(&c->buffers.a[c->bound_buffers[target]].data[offset], data, size);
}

PGLDEF void glNamedBufferData(GLuint buffer, GLsizeiptr size, const GLvoid* data, GLenum usage)
{
    //check for usage later
    PGL_UNUSED(usage);

    PGL_ERR((!buffer || buffer >= c->buffers.size || c->buffers.a[buffer].deleted), GL_INVALID_OPERATION);
    PGL_ERR(size < 0, GL_INVALID_VALUE);

    //always NULL or valid
    PGL_FREE(c->buffers.a[buffer].data);

    c->buffers.a[buffer].data = (u8*)PGL_MALLOC(size);
    PGL_ERR(!c->buffers.a[buffer].data, GL_OUT_OF_MEMORY);

    if (data) {
        memcpy(c->buffers.a[buffer].data, data, size);
    }

    c->buffers.a[buffer].user_owned = GL_FALSE;
    c->buffers.a[buffer].size = size;
}

PGLDEF void glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const GLvoid* data)
{
    PGL_ERR((!buffer || buffer >= c->buffers.size || c->buffers.a[buffer].deleted), GL_INVALID_OPERATION);
    PGL_ERR((offset < 0 || size < 0), GL_INVALID_VALUE);
    PGL_ERR((offset + size > c->buffers.a[buffer].size), GL_INVALID_VALUE);

    memcpy(&c->buffers.a[buffer].data[offset], data, size);
}

// TODO see page 136-7 of spec
PGLDEF void glBindTexture(GLenum target, GLuint texture)
{
    PGL_ERR((target < GL_TEXTURE_1D || target >= GL_NUM_TEXTURE_TYPES), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;

    PGL_ERR((texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_VALUE);

    if (texture) {
        GLenum type = c->textures.a[texture].type;
        PGL_ERR((type != GL_TEXTURE_UNBOUND && type != target), GL_INVALID_OPERATION);

        if (type == GL_TEXTURE_UNBOUND) {
            INIT_TEX(&c->textures.a[texture], target);
        }
    }
    c->bound_textures[target] = texture;
}

static void set_texparami(glTexture* tex, GLenum pname, GLint param)
{
    /*
    PGL_ERR((pname != GL_TEXTURE_MIN_FILTER && pname != GL_TEXTURE_MAG_FILTER &&
             pname != GL_TEXTURE_WRAP_S && pname != GL_TEXTURE_WRAP_T &&
             pname != GL_TEXTURE_WRAP_R), GL_INVALID_ENUM);
             */

    // NOTE, currently in the texture access functions
    // if it's not NEAREST, it assumes LINEAR so I could
    // just say that's good rather than these switch statements
    //
    // TODO compress this code
    if (pname == GL_TEXTURE_MIN_FILTER) {
        // TODO technically GL_TEXTURE_RECTANGLE can only have NEAREST OR LINEAR, no mipmapping
        // but since we don't actually do mipmaping or use min filter at all...
        switch (param) {
        case GL_NEAREST:
        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_NEAREST_MIPMAP_LINEAR:
            param = GL_NEAREST;
            break;
        case GL_LINEAR:
        case GL_LINEAR_MIPMAP_NEAREST:
        case GL_LINEAR_MIPMAP_LINEAR:
            param = GL_LINEAR;
            break;
        default:
            PGL_SET_ERR(GL_INVALID_ENUM);
            return;
        }
        tex->min_filter = param;
    } else if (pname == GL_TEXTURE_MAG_FILTER) {
        switch (param) {
        case GL_NEAREST:
        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_NEAREST_MIPMAP_LINEAR:
            param = GL_NEAREST;
            break;
        case GL_LINEAR:
        case GL_LINEAR_MIPMAP_NEAREST:
        case GL_LINEAR_MIPMAP_LINEAR:
            param = GL_LINEAR;
            break;
        default:
            PGL_SET_ERR(GL_INVALID_ENUM);
            return;
        }
        tex->min_filter = param;
        tex->mag_filter = param;
    } else if (pname == GL_TEXTURE_WRAP_S) {
        PGL_ERR((param != GL_REPEAT && param != GL_CLAMP_TO_EDGE && param != GL_CLAMP_TO_BORDER && param != GL_MIRRORED_REPEAT), GL_INVALID_ENUM);

        // TODO This is in the standard but I don't really see the point, it costs nothing to support it,
        // maybe I'll make a PGL_WARN() macro or something
        //PGL_ERR((tex->type == GL_TEXTURE_RECTANGLE && param != GL_CLAMP_TO_EDGE && param != GL_CLAMP_TO_BORDER), GL_INVALID_ENUM);
        tex->wrap_s = param;
    } else if (pname == GL_TEXTURE_WRAP_T) {
        PGL_ERR((param != GL_REPEAT && param != GL_CLAMP_TO_EDGE && param != GL_CLAMP_TO_BORDER && param != GL_MIRRORED_REPEAT), GL_INVALID_ENUM);

        //PGL_ERR((tex->type == GL_TEXTURE_RECTANGLE && param != GL_CLAMP_TO_EDGE && param != GL_CLAMP_TO_BORDER), GL_INVALID_ENUM);

        tex->wrap_t = param;
    } else if (pname == GL_TEXTURE_WRAP_R) {
        PGL_ERR((param != GL_REPEAT && param != GL_CLAMP_TO_EDGE && param != GL_CLAMP_TO_BORDER && param != GL_MIRRORED_REPEAT), GL_INVALID_ENUM);
        tex->wrap_r = param;
    } else {
        PGL_SET_ERR(GL_INVALID_ENUM);
    }
}

// TODO handle ParameterI*() functions correctly
static void get_texparami(glTexture* tex, GLenum pname, GLenum type, GLvoid* params)
{
    GLenum val;
    switch (pname) {
    case GL_TEXTURE_MIN_FILTER: val = tex->min_filter; break;
    case GL_TEXTURE_MAG_FILTER: val = tex->mag_filter; break;
    case GL_TEXTURE_WRAP_S:
        PGL_ERR((pname != GL_REPEAT && pname != GL_CLAMP_TO_EDGE && pname != GL_CLAMP_TO_BORDER && pname != GL_MIRRORED_REPEAT), GL_INVALID_ENUM);
        val = tex->wrap_s;
        break;
    case GL_TEXTURE_WRAP_T:
        PGL_ERR((pname != GL_REPEAT && pname != GL_CLAMP_TO_EDGE && pname != GL_CLAMP_TO_BORDER && pname != GL_MIRRORED_REPEAT), GL_INVALID_ENUM);
        val = tex->wrap_t;
        break;
    case GL_TEXTURE_WRAP_R:
        PGL_ERR((pname != GL_REPEAT && pname != GL_CLAMP_TO_EDGE && pname != GL_CLAMP_TO_BORDER && pname != GL_MIRRORED_REPEAT), GL_INVALID_ENUM);
        val = tex->wrap_r;
        break;
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
        return;
    }

    if (type == GL_INT) {
        *(GLint*)params = val;
    } else {
        *(GLuint*)params = val;
    }
}

PGLDEF void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    //shift to range 0 - NUM_TEXTURES-1 to access bound_textures array
    target -= GL_TEXTURE_UNBOUND + 1;

    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }
    set_texparami(tex, pname, param);
}

PGLDEF void glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params)
{
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    PGL_ERR((pname != GL_TEXTURE_BORDER_COLOR), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;
    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }
    memcpy(&tex->border_color, params, sizeof(GLfloat)*4);
#endif
}
PGLDEF void glTexParameteriv(GLenum target, GLenum pname, const GLint* params)
{
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    PGL_ERR((pname != GL_TEXTURE_BORDER_COLOR), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;
    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }

    tex->border_color.x = (2*params[0] + 1)/(UINT32_MAX - 1.0f);
    tex->border_color.y = (2*params[1] + 1)/(UINT32_MAX - 1.0f);
    tex->border_color.z = (2*params[2] + 1)/(UINT32_MAX - 1.0f);
    tex->border_color.w = (2*params[3] + 1)/(UINT32_MAX - 1.0f);
#endif
}

// NOTE: I added the !texture checks to the glTextureParameter*() functions
// even though it's not in the spec because there's no way to know which
// default texture (0) target you're referring to
PGLDEF void glTextureParameteri(GLuint texture, GLenum pname, GLint param)
{
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);
    set_texparami(&c->textures.a[texture], pname, param);
}

PGLDEF void glTextureParameterfv(GLuint texture, GLenum pname, const GLfloat* params)
{
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);
    memcpy(&c->textures.a[texture].border_color, params, sizeof(GLfloat)*4);
#endif
}

PGLDEF void glTextureParameteriv(GLuint texture, GLenum pname, const GLint* params)
{
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);

    glTexture* tex = &c->textures.a[texture];
    tex->border_color.x = (2*params[0] + 1)/(UINT32_MAX - 1.0f);
    tex->border_color.y = (2*params[1] + 1)/(UINT32_MAX - 1.0f);
    tex->border_color.z = (2*params[2] + 1)/(UINT32_MAX - 1.0f);
    tex->border_color.w = (2*params[3] + 1)/(UINT32_MAX - 1.0f);
#endif
}

PGLDEF void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params)
{
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    PGL_ERR((pname != GL_TEXTURE_BORDER_COLOR), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;
    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }
    memcpy(params, &tex->border_color, sizeof(GLfloat)*4);
#endif
}

PGLDEF void glGetTexParameteriv(GLenum target, GLenum pname, GLint* params)
{
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;

    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }
    get_texparami(tex, pname, GL_INT, (GLvoid*)params);
}

PGLDEF void glGetTexParameterIiv(GLenum target, GLenum pname, GLint* params)
{
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;

    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }
    get_texparami(tex, pname, GL_INT, (GLvoid*)params);
}

PGLDEF void glGetTexParameterIuiv(GLenum target, GLenum pname, GLuint* params)
{
    PGL_ERR((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D && target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY && target != GL_TEXTURE_RECTANGLE && target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    target -= GL_TEXTURE_UNBOUND + 1;

    glTexture* tex = NULL;
    if (c->bound_textures[target]) {
        tex = &c->textures.a[c->bound_textures[target]];
    } else {
        tex = &c->default_textures[target];
    }
    get_texparami(tex, pname, GL_UNSIGNED_INT, (GLvoid*)params);
}

PGLDEF void glGetTextureParameterfv(GLuint texture, GLenum pname, GLfloat* params)
{
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);
    memcpy(params, &c->textures.a[texture].border_color, sizeof(GLfloat)*4);
#endif
}

PGLDEF void glGetTextureParameteriv(GLuint texture, GLenum pname, GLint* params)
{
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);
    get_texparami(&c->textures.a[texture], pname, GL_UNSIGNED_INT, (GLvoid*)params);
}

PGLDEF void glGetTextureParameterIiv(GLuint texture, GLenum pname, GLint* params)
{
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);
    get_texparami(&c->textures.a[texture], pname, GL_UNSIGNED_INT, (GLvoid*)params);
}

PGLDEF void glGetTextureParameterIuiv(GLuint texture, GLenum pname, GLuint* params)
{
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);
    get_texparami(&c->textures.a[texture], pname, GL_UNSIGNED_INT, (GLvoid*)params);
}


PGLDEF void glPixelStorei(GLenum pname, GLint param)
{
    PGL_ERR((pname != GL_UNPACK_ALIGNMENT && pname != GL_PACK_ALIGNMENT), GL_INVALID_ENUM);

    PGL_ERR((param != 1 && param != 2 && param != 4 && param != 8), GL_INVALID_VALUE);

    // TODO eliminate branch? or use PGL_SET_ERR in else
    if (pname == GL_UNPACK_ALIGNMENT) {
        c->unpack_alignment = param;
    } else if (pname == GL_PACK_ALIGNMENT) {
        c->pack_alignment = param;
    }

}

// TODO check preprocessor output
#define CHECK_FORMAT_GET_COMP(format, components) \
    do { \
    switch (format) { \
    case GL_RED: \
    case GL_ALPHA: \
    case GL_LUMINANCE: \
    case PGL_ONE_ALPHA: \
        components = 1; \
        break; \
    case GL_RG: \
    case GL_LUMINANCE_ALPHA: \
        components = 2; \
        break; \
    case GL_RGB: \
    case GL_BGR: \
        components = 3; \
        break; \
    case GL_RGBA: \
    case GL_BGRA: \
        components = 4; \
        break; \
    default: \
        PGL_SET_ERR(GL_INVALID_ENUM); \
        return; \
    } \
    } while (0)

PGLDEF void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);
    PGL_UNUSED(internalformat);
    PGL_UNUSED(border);

    PGL_ERR(target != GL_TEXTURE_1D, GL_INVALID_ENUM);
    PGL_ERR((width < 0 || width > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);

    int components;
#ifdef PGL_DONT_CONVERT_TEXTURES
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);
    components = 4;
#else
    CHECK_FORMAT_GET_COMP(format, components);
#endif

    int target_idx = target-GL_TEXTURE_UNBOUND-1;
    int cur_tex_i = c->bound_textures[target_idx];
    glTexture* tex = NULL;
    if (cur_tex_i) {
        tex = &c->textures.a[cur_tex_i];
    } else {
        tex = &c->default_textures[target_idx];
    }

    tex->w = width;
    tex->h = 1;
    tex->d = 1;

    // TODO NULL or valid ... but what if user_owned?
    PGL_FREE(tex->data);

    //TODO hardcoded 4 till I support more than RGBA/UBYTE internally
    tex->data = (u8*)PGL_MALLOC(width * 4);
    PGL_ERR(!tex->data, GL_OUT_OF_MEMORY);

    u8* texdata = tex->data;

    if (data) {
        convert_format_to_packed_rgba(texdata, (u8*)data, width, 1, width*components, format);
    }

    tex->user_owned = GL_FALSE;
}

PGLDEF void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);
    PGL_UNUSED(internalformat);
    PGL_UNUSED(border);

    // TODO GL_TEXTURE_1D_ARRAY
    PGL_ERR((target != GL_TEXTURE_2D &&
             target != GL_TEXTURE_1D_ARRAY &&
             target != GL_TEXTURE_RECTANGLE &&
             target != GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
             target != GL_TEXTURE_CUBE_MAP_NEGATIVE_X &&
             target != GL_TEXTURE_CUBE_MAP_POSITIVE_Y &&
             target != GL_TEXTURE_CUBE_MAP_NEGATIVE_Y &&
             target != GL_TEXTURE_CUBE_MAP_POSITIVE_Z &&
             target != GL_TEXTURE_CUBE_MAP_NEGATIVE_Z), GL_INVALID_ENUM);

    PGL_ERR((width < 0 || width > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR((height < 0 || height > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);

    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);

    int components;
#ifdef PGL_DONT_CONVERT_TEXTURES
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);
    components = 4;
#else
    CHECK_FORMAT_GET_COMP(format, components);
#endif

    // Have to handle cubemaps specially since they have 1 real target
    // and 6 pseudo targets
    int target_idx;
    if (target < GL_TEXTURE_CUBE_MAP_POSITIVE_X) {
        //target is 2D, 1D_ARRAY, or RECTANGLE
        target_idx = target-GL_TEXTURE_UNBOUND-1;
    } else {
        target_idx = GL_TEXTURE_CUBE_MAP-GL_TEXTURE_UNBOUND-1;
    }
    int cur_tex_i = c->bound_textures[target_idx];

    // Have to handle 0 specially as well
    glTexture* tex = NULL;
    if (cur_tex_i) {
        tex = &c->textures.a[cur_tex_i];
    } else {
        tex = &c->default_textures[target_idx];
    }

    // TODO If I ever support type other than GL_UNSIGNED_BYTE (also using for both internalformat and format)
    int byte_width = width * components;
    int padding_needed = byte_width % c->unpack_alignment;
    int padded_row_len = (!padding_needed) ? byte_width : byte_width + c->unpack_alignment - padding_needed;

    if (target < GL_TEXTURE_CUBE_MAP_POSITIVE_X) {
        //target is 2D, 1D_ARRAY, or RECTANGLE
        tex->w = width;
        tex->h = height;
        tex->d = 1;

        // either NULL or valid
        PGL_FREE(tex->data);

        //TODO support other internal formats? components should be of internalformat not format hardcoded 4 until I support more than RGBA
        tex->data = (u8*)PGL_MALLOC(height * width*4);
        PGL_ERR(!tex->data, GL_OUT_OF_MEMORY);

        if (data) {
            convert_format_to_packed_rgba(tex->data, (u8*)data, width, height, padded_row_len, format);
        }

        tex->user_owned = GL_FALSE;

    } else {  //CUBE_MAP
        // If we're reusing a texture, and we haven't already loaded
        // one of the planes of the cubemap, data is either NULL or valid
        if (!tex->w)
            PGL_FREE(tex->data);

        // TODO specs say INVALID_VALUE, man/ref pages say INVALID_ENUM?
        // https://registry.khronos.org/OpenGL-Refpages/gl4/html/glTexImage2D.xhtml
        PGL_ERR(width != height, GL_INVALID_VALUE);

        // TODO hardcoded 4 as long as we only support RGBA/UBYTES
        int mem_size = width*height*6 * 4;
        if (tex->w == 0) {
            tex->w = width;
            tex->h = width; //same cause square
            tex->d = 1;

            tex->data = (u8*)PGL_MALLOC(mem_size);
            PGL_ERR(!tex->data, GL_OUT_OF_MEMORY);
        } else if (tex->w != width) {
            //TODO spec doesn't say all sides must have same dimensions but it makes sense
            //and this site suggests it http://www.opengl.org/wiki/Cubemap_Texture
            PGL_SET_ERR(GL_INVALID_VALUE);
            return;
        }

        //use target as plane index
        target -= GL_TEXTURE_CUBE_MAP_POSITIVE_X;

        // TODO handle different format and internalformat
        int p = height*width*4;
        u8* texdata = tex->data;

        if (data) {
            convert_format_to_packed_rgba(&texdata[target*p], (u8*)data, width, height, padded_row_len, format);
        }

        tex->user_owned = GL_FALSE;
    } //end CUBE_MAP
}

PGLDEF void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);
    PGL_UNUSED(internalformat);
    PGL_UNUSED(border);

    PGL_ERR((target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY), GL_INVALID_ENUM);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);
    PGL_ERR((width < 0 || width > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR((height < 0 || height > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR((depth < 0 || depth > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);

    int components;
#ifdef PGL_DONT_CONVERT_TEXTURES
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);
    components = 4;
#else
    CHECK_FORMAT_GET_COMP(format, components);
#endif

    int target_idx = target-GL_TEXTURE_UNBOUND-1;
    int cur_tex_i = c->bound_textures[target_idx];
    glTexture* tex = NULL;
    if (cur_tex_i) {
        tex = &c->textures.a[cur_tex_i];
    } else {
        tex = &c->default_textures[target_idx];
    }

    tex->w = width;
    tex->h = height;
    tex->d = depth;

    int byte_width = width * components;
    int padding_needed = byte_width % c->unpack_alignment;
    int padded_row_len = (!padding_needed) ? byte_width : byte_width + c->unpack_alignment - padding_needed;

    // NULL or valid
    PGL_FREE(tex->data);

    //TODO hardcoded 4 till I support more than RGBA/UBYTE internally
    tex->data = (u8*)PGL_MALLOC(width*height*depth * 4);
    PGL_ERR(!tex->data, GL_OUT_OF_MEMORY);

    u8* texdata = tex->data;

    if (data) {
        convert_format_to_packed_rgba(texdata, (u8*)data, width, height*depth, padded_row_len, format);
    }

    tex->user_owned = GL_FALSE;
}

PGLDEF void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);

    PGL_ERR(target != GL_TEXTURE_1D, GL_INVALID_ENUM);
    PGL_ERR((width < 0 || width > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);

    int target_idx = target-GL_TEXTURE_UNBOUND-1;
    int cur_tex_i = c->bound_textures[target_idx];
    glTexture* tex = NULL;
    if (cur_tex_i) {
        tex = &c->textures.a[cur_tex_i];
    } else {
        tex = &c->default_textures[target_idx];
    }

    int components;
#ifdef PGL_DONT_CONVERT_TEXTURES
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);
    components = 4;
#else
    CHECK_FORMAT_GET_COMP(format, components);
#endif

    PGL_ERR((xoffset < 0 || xoffset + width > tex->w), GL_INVALID_VALUE);

    u32* texdata = (u32*)tex->data;
    convert_format_to_packed_rgba((u8*)&texdata[xoffset], (u8*)data, width, 1, width*components, format);
}

PGLDEF void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);

    // TODO GL_TEXTURE_1D_ARRAY
    PGL_ERR((target != GL_TEXTURE_2D &&
             target != GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
             target != GL_TEXTURE_CUBE_MAP_NEGATIVE_X &&
             target != GL_TEXTURE_CUBE_MAP_POSITIVE_Y &&
             target != GL_TEXTURE_CUBE_MAP_NEGATIVE_Y &&
             target != GL_TEXTURE_CUBE_MAP_POSITIVE_Z &&
             target != GL_TEXTURE_CUBE_MAP_NEGATIVE_Z), GL_INVALID_ENUM);

    PGL_ERR((width < 0 || width > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR((height < 0 || height > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);

    int components;
#ifdef PGL_DONT_CONVERT_TEXTURES
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);
    components = 4;
#else
    CHECK_FORMAT_GET_COMP(format, components);
#endif

    // Have to handle cubemaps specially since they have 1 real target
    // and 6 pseudo targets
    int target_idx;
    if (target == GL_TEXTURE_2D) {
        target_idx = target-GL_TEXTURE_UNBOUND-1;
    } else {
        target_idx = GL_TEXTURE_CUBE_MAP-GL_TEXTURE_UNBOUND-1;
    }
    int cur_tex_i = c->bound_textures[target_idx];

    // Have to handle 0 specially as well
    glTexture* tex = NULL;
    if (cur_tex_i) {
        tex = &c->textures.a[cur_tex_i];
    } else {
        tex = &c->default_textures[target_idx];
    }

    u8* d = (u8*)data;

    int byte_width = width * components;
    int padding_needed = byte_width % c->unpack_alignment;
    int padded_row_len = (!padding_needed) ? byte_width : byte_width + c->unpack_alignment - padding_needed;

    if (target == GL_TEXTURE_2D) {
        u32* texdata = (u32*)tex->data;

        PGL_ERR((xoffset < 0 || xoffset + width > tex->w || yoffset < 0 || yoffset + height > tex->h), GL_INVALID_VALUE);

        int w = tex->w;

        // TODO maybe better to covert the whole input image if
        // necessary then do the original memcpy's even with
        // the extra alloc and free
        for (int i=0; i<height; ++i) {
            convert_format_to_packed_rgba((u8*)&texdata[(yoffset+i)*w + xoffset], &d[i*padded_row_len], width, 1, padded_row_len, format);
        }

    } else {  //CUBE_MAP
        u32* texdata = (u32*)tex->data;

        int w = tex->w;

        target -= GL_TEXTURE_CUBE_MAP_POSITIVE_X; //use target as plane index

        int p = w*w;

        for (int i=0; i<height; ++i) {
            convert_format_to_packed_rgba((u8*)&texdata[p*target + (yoffset+i)*w + xoffset], &d[i*padded_row_len], width, 1, padded_row_len, format);
        }
    } //end CUBE_MAP
}

PGLDEF void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);

    PGL_ERR((target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY), GL_INVALID_ENUM);
    PGL_ERR((width < 0 || width > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR((height < 0 || height > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR((depth < 0 || depth > PGL_MAX_TEXTURE_SIZE), GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);

    int components;
#ifdef PGL_DONT_CONVERT_TEXTURES
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);
    components = 4;
#else
    CHECK_FORMAT_GET_COMP(format, components);
#endif

    int byte_width = width * components;
    int padding_needed = byte_width % c->unpack_alignment;
    int padded_row_len = (!padding_needed) ? byte_width : byte_width + c->unpack_alignment - padding_needed;

    int target_idx = target-GL_TEXTURE_UNBOUND-1;
    int cur_tex_i = c->bound_textures[target_idx];
    glTexture* tex = NULL;
    if (cur_tex_i) {
        tex = &c->textures.a[cur_tex_i];
    } else {
        tex = &c->default_textures[target_idx];
    }

    PGL_ERR((xoffset < 0 || xoffset + width > tex->w ||
             yoffset < 0 || yoffset + height > tex->h ||
             zoffset < 0 || zoffset + depth > tex->d), GL_INVALID_VALUE);

    int w = tex->w;
    int h = tex->h;
    int p = w*h;
    int pp = h*padded_row_len;
    u8* d = (u8*)data;
    u32* texdata = (u32*)tex->data;
    u8* out;
    u8* in;

    for (int j=0; j<depth; ++j) {
        for (int i=0; i<height; ++i) {
            out = (u8*)&texdata[(zoffset+j)*p + (yoffset+i)*w + xoffset];
            in = &d[j*pp + i*padded_row_len];
            convert_format_to_packed_rgba(out, in, width, 1, padded_row_len, format);
        }
    }
}

PGLDEF void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* pointer)
{
    // See Section 2.8 pages 37-38 of 3.3 compatiblity spec
    //
    // Compare with Section 2.8 page 29 of 3.3 core spec
    // plus section E.2.2, pg 344 (VAOs required for everything, no default/0 VAO)
    //
    // GLES 2 and 3 match 3.3 compatibility profile
    //
    // Basically, core got rid of client arrays entirely, while compatibility
    // allows them for the default/0 VAO.
    //
    // So for now I've decided to match the compatibility profile
    // but you can easily remove c->cur_vertex_array from the check
    // below to enable client arrays for all VAOs; there's not really
    // any downside in PGL, it's all RAM.
    PGL_ERR((c->cur_vertex_array && !c->bound_buffers[GL_ARRAY_BUFFER-GL_ARRAY_BUFFER] && pointer),
            GL_INVALID_OPERATION);

    PGL_ERR(stride < 0, GL_INVALID_VALUE);
    PGL_ERR(index >= GL_MAX_VERTEX_ATTRIBS, GL_INVALID_VALUE);
    PGL_ERR((size < 1 || size > 4), GL_INVALID_VALUE);

    int type_sz = 4;
    switch (type) {
    case GL_BYTE:           type_sz = sizeof(GLbyte); break;
    case GL_UNSIGNED_BYTE:  type_sz = sizeof(GLubyte); break;
    case GL_SHORT:          type_sz = sizeof(GLshort); break;
    case GL_UNSIGNED_SHORT: type_sz = sizeof(GLushort); break;
    case GL_INT:            type_sz = sizeof(GLint); break;
    case GL_UNSIGNED_INT:   type_sz = sizeof(GLuint); break;

    case GL_FLOAT:  type_sz = sizeof(GLfloat); break;
    case GL_DOUBLE: type_sz = sizeof(GLdouble); break;

    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
        return;
    }

    glVertex_Attrib* v = &(c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs[index]);
    v->size = size;
    v->type = type;
    v->normalized = normalized;
    v->stride = (stride) ? stride : size*type_sz;

    // offset can still really be a pointer if using the 0 VAO and no bound ARRAY_BUFFER.
    v->offset = (GLsizeiptr)pointer;
    // I put ARRAY_BUFFER-itself instead of 0 to reinforce that bound_buffers is indexed that way, buffer type - GL_ARRAY_BUFFER
    v->buf = c->bound_buffers[GL_ARRAY_BUFFER-GL_ARRAY_BUFFER];
}

PGLDEF void glEnableVertexAttribArray(GLuint index)
{
    PGL_ERR(index >= GL_MAX_VERTEX_ATTRIBS, GL_INVALID_VALUE);
    c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs[index].enabled = GL_TRUE;
}

PGLDEF void glDisableVertexAttribArray(GLuint index)
{
    PGL_ERR(index >= GL_MAX_VERTEX_ATTRIBS, GL_INVALID_VALUE);
    c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs[index].enabled = GL_FALSE;
}

PGLDEF void glEnableVertexArrayAttrib(GLuint vaobj, GLuint index)
{
    PGL_ERR(index >= GL_MAX_VERTEX_ATTRIBS, GL_INVALID_VALUE);
    PGL_ERR((vaobj >= c->vertex_arrays.size || c->vertex_arrays.a[vaobj].deleted), GL_INVALID_OPERATION);

    c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs[index].enabled = GL_TRUE;
}

PGLDEF void glDisableVertexArrayAttrib(GLuint vaobj, GLuint index)
{
    PGL_ERR(index >= GL_MAX_VERTEX_ATTRIBS, GL_INVALID_VALUE);
    PGL_ERR((vaobj >= c->vertex_arrays.size || c->vertex_arrays.a[vaobj].deleted), GL_INVALID_OPERATION);
    c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs[index].enabled = GL_FALSE;
}

PGLDEF void glVertexAttribDivisor(GLuint index, GLuint divisor)
{
    PGL_ERR(index >= GL_MAX_VERTEX_ATTRIBS, GL_INVALID_VALUE);

    c->vertex_arrays.a[c->cur_vertex_array].vertex_attribs[index].divisor = divisor;
}



//TODO(rswinkle): Why is first, an index, a GLint and not GLuint or GLsizei?
PGLDEF void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR(count < 0, GL_INVALID_VALUE);

    if (!count)
        return;

    run_pipeline(mode, (GLvoid*)(GLintptr)first, count, 0, 0, GL_FALSE);
}

PGLDEF void glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei drawcount)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR(drawcount < 0, GL_INVALID_VALUE);

    for (GLsizei i=0; i<drawcount; i++) {
        if (!count[i]) continue;
        run_pipeline(mode, (GLvoid*)(GLintptr)first[i], count[i], 0, 0, GL_FALSE);
    }
}

PGLDEF void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR(count < 0, GL_INVALID_VALUE);

    // TODO error not in the spec but says type must be one of these ... strange
    PGL_ERR((type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT), GL_INVALID_ENUM);

    if (!count)
        return;

    run_pipeline(mode, indices, count, 0, 0, type);
}

// TODO fix
PGLDEF void glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type, const GLvoid* const* indices, GLsizei drawcount)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR(drawcount < 0, GL_INVALID_VALUE);

    // TODO error not in the spec but says type must be one of these ... strange
    PGL_ERR((type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT), GL_INVALID_ENUM);

    for (GLsizei i=0; i<drawcount; i++) {
        if (!count[i]) continue;
        run_pipeline(mode, indices[i], count[i], 0, 0, type);
    }
}

PGLDEF void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instancecount)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR((count < 0 || instancecount < 0), GL_INVALID_VALUE);

    if (!count || !instancecount)
        return;

    for (GLsizei instance = 0; instance < instancecount; ++instance) {
        run_pipeline(mode, (GLvoid*)(GLintptr)first, count, instance, 0, GL_FALSE);
    }
}

PGLDEF void glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei instancecount, GLuint baseinstance)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR((count < 0 || instancecount < 0), GL_INVALID_VALUE);

    if (!count || !instancecount)
        return;

    for (GLsizei instance = 0; instance < instancecount; ++instance) {
        run_pipeline(mode, (GLvoid*)(GLintptr)first, count, instance, baseinstance, GL_FALSE);
    }
}


PGLDEF void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices, GLsizei instancecount)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR((count < 0 || instancecount < 0), GL_INVALID_VALUE);

    // NOTE: error not in the spec but says type must be one of these ... strange
    PGL_ERR((type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT), GL_INVALID_ENUM);

    if (!count || !instancecount)
        return;

    for (GLsizei instance = 0; instance < instancecount; ++instance) {
        run_pipeline(mode, indices, count, instance, 0, type);
    }
}

PGLDEF void glDrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices, GLsizei instancecount, GLuint baseinstance)
{
    PGL_ERR((mode < GL_POINTS || mode > GL_TRIANGLE_FAN), GL_INVALID_ENUM);
    PGL_ERR((count < 0 || instancecount < 0), GL_INVALID_VALUE);

    //error not in the spec but says type must be one of these ... strange
    PGL_ERR((type != GL_UNSIGNED_BYTE && type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT), GL_INVALID_ENUM);

    if (!count || !instancecount)
        return;

    for (GLsizei instance = 0; instance < instancecount; ++instance) {
        run_pipeline(mode, indices, count, instance, baseinstance, GL_TRUE);
    }
}

PGLDEF void glDebugMessageCallback(GLDEBUGPROC callback, void* userParam)
{
    c->dbg_callback = callback;
    c->dbg_userparam = userParam;
}

PGLDEF void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    PGL_ERR((width < 0 || height < 0), GL_INVALID_VALUE);

    // TODO: Do I need a full matrix? Also I don't actually
    // use these values anywhere else so why save them?  See ref pages or TinyGL for alternative
    make_viewport_m4(c->vp_mat, x, y, width, height, 1);
    c->xmin = x;
    c->ymin = y;
    c->width = width;
    c->height = height;
}

PGLDEF void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    red = clamp_01(red);
    green = clamp_01(green);
    blue = clamp_01(blue);
    alpha = clamp_01(alpha);

    //vec4 tmp = { red, green, blue, alpha };
    //c->clear_color = vec4_to_Color(tmp);
    c->clear_color = RGBA_TO_PIXEL(red*PGL_RMAX, green*PGL_GMAX, blue*PGL_BMAX, alpha*PGL_AMAX);
}

PGLDEF void glClearDepthf(GLfloat depth)
{
    c->clear_depth = clamp_01(depth);
}

PGLDEF void glClearDepth(GLdouble depth)
{
    c->clear_depth = clamp_01(depth);
}

PGLDEF void glDepthFunc(GLenum func)
{
    PGL_ERR((func < GL_LESS || func > GL_NEVER), GL_INVALID_ENUM);

    c->depth_func = func;
}

PGLDEF void glDepthRangef(GLfloat nearVal, GLfloat farVal)
{
    c->depth_range_near = clamp_01(nearVal);
    c->depth_range_far = clamp_01(farVal);
}

PGLDEF void glDepthRange(GLdouble nearVal, GLdouble farVal)
{
    c->depth_range_near = clamp_01(nearVal);
    c->depth_range_far = clamp_01(farVal);
}

PGLDEF void glDepthMask(GLboolean flag)
{
    c->depth_mask = flag;
}

PGLDEF void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
#ifndef PGL_DISABLE_COLOR_MASK
    // !! ensures 1 or 0
    red = !!red;
    green = !!green;
    blue = !!blue;
    alpha = !!alpha;

    // By multiplying by the pixel format masks there's no need to shift them
    pix_t rmask = red*PGL_RMASK;
    pix_t gmask = green*PGL_GMASK;
    pix_t bmask = blue*PGL_BMASK;
    pix_t amask = alpha*PGL_AMASK;
    c->color_mask = rmask | gmask | bmask | amask;
#endif
}

PGLDEF void glClear(GLbitfield mask)
{
    // TODO: If a buffer is not present, then a glClear directed at that buffer has no effect.
    // right now they're all always present

    PGL_ERR((mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT)), GL_INVALID_VALUE);

    // NOTE: All buffers should have the same dimensions/size
    int sz = c->ux * c->uy;
    int w = c->back_buffer.w;

    pix_t color = c->clear_color;

#ifndef PGL_DISABLE_COLOR_MASK
    // clear out channels not enabled for writing
    // TODO are these casts really necessary?
    color &= (pix_t)c->color_mask;
    // used to erase channels to be written
    pix_t clear_mask = ~((pix_t)c->color_mask);
    pix_t tmp;
#endif

#ifndef PGL_NO_DEPTH_NO_STENCIL
    u32 cd = (u32)(c->clear_depth * PGL_MAX_Z) << PGL_ZSHIFT;
#  ifndef PGL_NO_STENCIL
    u8 cs = c->clear_stencil;
#  endif
#endif
    if (!c->scissor_test) {
        if (mask & GL_COLOR_BUFFER_BIT) {
            for (int i=0; i<sz; ++i) {
#ifdef PGL_DISABLE_COLOR_MASK
                ((pix_t*)c->back_buffer.buf)[i] = color;
#else
                tmp = ((pix_t*)c->back_buffer.buf)[i];
                tmp &= clear_mask;
                ((pix_t*)c->back_buffer.buf)[i] = tmp | color;
#endif
            }
        }
#ifndef PGL_NO_DEPTH_NO_STENCIL
        if (mask & GL_DEPTH_BUFFER_BIT && c->depth_mask) {
            for (int i=0; i < sz; ++i) {
                SET_Z_PRESHIFTED_TOP(i, cd);
            }
        }

#ifndef PGL_NO_STENCIL
        if (mask & GL_STENCIL_BUFFER_BIT) {
#  ifdef PGL_D16
            memset(c->stencil_buf.buf, cs, sz);
#  else
            for (int i=0; i < sz; ++i) {
                SET_STENCIL_TOP(i, cs);
            }
#  endif
        }
#  endif
#endif
    } else {
        // TODO this code is correct with or without scissor
        // enabled, test performance difference with above before
        // getting rid of above
        if (mask & GL_COLOR_BUFFER_BIT) {
            for (int y=c->ly; y<c->uy; ++y) {
                for (int x=c->lx; x<c->ux; ++x) {
                    int i = -y*w + x;
#ifdef PGL_DISABLE_COLOR_MASK
                    ((pix_t*)c->back_buffer.lastrow)[i] = color;
#else
                    tmp = ((pix_t*)c->back_buffer.lastrow)[i];
                    tmp &= clear_mask;
                    ((pix_t*)c->back_buffer.lastrow)[i] = tmp | color;
#endif
                }
            }
        }
#ifndef PGL_NO_DEPTH_NO_STENCIL
        if (mask & GL_DEPTH_BUFFER_BIT && c->depth_mask) {
            for (int y=c->ly; y<c->uy; ++y) {
                for (int x=c->lx; x<c->ux; ++x) {
                    int i = -y*w + x;
                    SET_Z_PRESHIFTED(i, cd);
                }
            }
        }
#  ifndef PGL_NO_STENCIL
        if (mask & GL_STENCIL_BUFFER_BIT) {
            for (int y=c->ly; y<c->uy; ++y) {
                for (int x=c->lx; x<c->ux; ++x) {
                    int i = -y*w + x;
                    SET_STENCIL(i, cs);
                }
            }
        }
#  endif
#endif
    }
}

PGLDEF void glEnable(GLenum cap)
{
    switch (cap) {
    case GL_CULL_FACE:
        c->cull_face = GL_TRUE;
        break;
    case GL_DEPTH_TEST:
        c->depth_test = GL_TRUE;
        break;
    case GL_DEPTH_CLAMP:
        c->depth_clamp = GL_TRUE;
        break;
    case GL_LINE_SMOOTH:
        // TODO implementation needs work/upgrade
        c->line_smooth = GL_TRUE;
        break;
    case GL_BLEND:
        c->blend = GL_TRUE;
        break;
    case GL_COLOR_LOGIC_OP:
        c->logic_ops = GL_TRUE;
        break;
    case GL_POLYGON_OFFSET_POINT:
        c->poly_offset_pt = GL_TRUE;
        break;
    case GL_POLYGON_OFFSET_LINE:
        c->poly_offset_line = GL_TRUE;
        break;
    case GL_POLYGON_OFFSET_FILL:
        c->poly_offset_fill = GL_TRUE;
        break;
    case GL_SCISSOR_TEST: {
        c->scissor_test = GL_TRUE;
        int ux = c->scissor_lx+c->scissor_w;
        int uy = c->scissor_ly+c->scissor_h;
        c->lx = MAX(c->scissor_lx, 0);
        c->ly = MAX(c->scissor_ly, 0);
        c->ux = MIN(ux, c->back_buffer.w);
        c->uy = MIN(uy, c->back_buffer.h);
    } break;
    case GL_STENCIL_TEST:
#ifndef PGL_NO_STENCIL
        c->stencil_test = GL_TRUE;
#endif
        break;
    case GL_DEBUG_OUTPUT:
        c->dbg_output = GL_TRUE;
        break;
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
    }
}

PGLDEF void glDisable(GLenum cap)
{
    switch (cap) {
    case GL_CULL_FACE:
        c->cull_face = GL_FALSE;
        break;
    case GL_DEPTH_TEST:
        c->depth_test = GL_FALSE;
        break;
    case GL_DEPTH_CLAMP:
        c->depth_clamp = GL_FALSE;
        break;
    case GL_LINE_SMOOTH:
        c->line_smooth = GL_FALSE;
        break;
    case GL_BLEND:
        c->blend = GL_FALSE;
        break;
    case GL_COLOR_LOGIC_OP:
        c->logic_ops = GL_FALSE;
        break;
    case GL_POLYGON_OFFSET_POINT:
        c->poly_offset_pt = GL_FALSE;
        break;
    case GL_POLYGON_OFFSET_LINE:
        c->poly_offset_line = GL_FALSE;
        break;
    case GL_POLYGON_OFFSET_FILL:
        c->poly_offset_fill = GL_FALSE;
        break;
    case GL_SCISSOR_TEST:
        c->scissor_test = GL_FALSE;
        c->lx = 0;
        c->ly = 0;
        c->ux = c->back_buffer.w;
        c->uy = c->back_buffer.h;
        break;
    case GL_STENCIL_TEST:
#ifndef PGL_NO_STENCIL
        c->stencil_test = GL_FALSE;
#endif
        break;
    case GL_DEBUG_OUTPUT:
        c->dbg_output = GL_FALSE;
        break;
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
    }
}

PGLDEF GLboolean glIsEnabled(GLenum cap)
{
    // make up my own enum for this?  rename member as no_early_z?
    //GLboolean fragdepth_or_discard;
    switch (cap) {
    case GL_DEPTH_TEST: return c->depth_test;
    case GL_LINE_SMOOTH: return c->line_smooth;
    case GL_CULL_FACE: return c->cull_face;
    case GL_DEPTH_CLAMP: return c->depth_clamp;
    case GL_BLEND: return c->blend;
    case GL_COLOR_LOGIC_OP: return c->logic_ops;
    case GL_POLYGON_OFFSET_POINT: return c->poly_offset_pt;
    case GL_POLYGON_OFFSET_LINE: return c->poly_offset_line;
    case GL_POLYGON_OFFSET_FILL: return c->poly_offset_fill;
    case GL_SCISSOR_TEST: return c->scissor_test;
#ifndef PGL_NO_STENCIL
    case GL_STENCIL_TEST: return c->stencil_test;
#endif
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
    }

    return GL_FALSE;
}

PGLDEF GLboolean glIsProgram(GLuint program)
{
    if (!program || program >= c->programs.size || c->programs.a[program].deleted) {
        return GL_FALSE;
    }

    return GL_TRUE;
}

PGLDEF void glGetBooleanv(GLenum pname, GLboolean* data)
{
    // not sure it's worth adding every enum, spec says
    // gelGet* will convert/map types if they don't match the function
    switch (pname) {
    case GL_DEPTH_TEST:           *data = c->depth_test;       break;
    case GL_LINE_SMOOTH:          *data = c->line_smooth;      break;
    case GL_CULL_FACE:            *data = c->cull_face;        break;
    case GL_DEPTH_CLAMP:          *data = c->depth_clamp;      break;
    case GL_BLEND:                *data = c->blend;            break;
    case GL_COLOR_LOGIC_OP:       *data = c->logic_ops;        break;
    case GL_POLYGON_OFFSET_POINT: *data = c->poly_offset_pt;  break;
    case GL_POLYGON_OFFSET_LINE:  *data = c->poly_offset_line; break;
    case GL_POLYGON_OFFSET_FILL:  *data = c->poly_offset_fill; break;
    case GL_SCISSOR_TEST:         *data = c->scissor_test;     break;
#ifndef PGL_NO_STENCIL
    case GL_STENCIL_TEST:         *data = c->stencil_test;     break;
#endif
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
    }
}

PGLDEF void glGetFloatv(GLenum pname, GLfloat* data)
{
    switch (pname) {
    case GL_POLYGON_OFFSET_FACTOR:         *data = c->poly_factor;         break;
    case GL_POLYGON_OFFSET_UNITS:          *data = c->poly_units;          break;
    case GL_POINT_SIZE:                    *data = c->point_size;          break;
    case GL_LINE_WIDTH:                    *data = c->line_width;          break;
    case GL_DEPTH_CLEAR_VALUE:             *data = c->clear_depth;         break;
    case GL_SMOOTH_LINE_WIDTH_GRANULARITY: *data = PGL_SMOOTH_GRANULARITY; break;

    case GL_MAX_TEXTURE_SIZE:         *data = PGL_MAX_TEXTURE_SIZE;         break;
    case GL_MAX_3D_TEXTURE_SIZE:      *data = PGL_MAX_3D_TEXTURE_SIZE;      break;
    case GL_MAX_ARRAY_TEXTURE_LAYERS: *data = PGL_MAX_ARRAY_TEXTURE_LAYERS; break;

    case GL_ALIASED_LINE_WIDTH_RANGE:
        data[0] = 1.0f;
        data[1] = PGL_MAX_ALIASED_WIDTH;
        break;

    case GL_SMOOTH_LINE_WIDTH_RANGE:
        data[0] = 1.0f;
        data[1] = PGL_MAX_SMOOTH_WIDTH;
        break;

    case GL_DEPTH_RANGE:
        data[0] = c->depth_range_near;
        data[1] = c->depth_range_near;
        break;
    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
    }
}

PGLDEF void glGetIntegerv(GLenum pname, GLint* data)
{
    // TODO maybe make all the enum/int member names match the associated ENUM?
    switch (pname) {
#ifndef PGL_NO_STENCIL
    case GL_STENCIL_WRITE_MASK:       data[0] = c->stencil_writemask; break;
    case GL_STENCIL_REF:              data[0] = c->stencil_ref; break;
    case GL_STENCIL_VALUE_MASK:       data[0] = c->stencil_valuemask; break;
    case GL_STENCIL_FUNC:             data[0] = c->stencil_func; break;
    case GL_STENCIL_FAIL:             data[0] = c->stencil_sfail; break;
    case GL_STENCIL_PASS_DEPTH_FAIL:  data[0] = c->stencil_dpfail; break;
    case GL_STENCIL_PASS_DEPTH_PASS:  data[0] = c->stencil_dppass; break;

    case GL_STENCIL_BACK_WRITE_MASK:      data[0] = c->stencil_writemask_back; break;
    case GL_STENCIL_BACK_REF:             data[0] = c->stencil_ref_back; break;
    case GL_STENCIL_BACK_VALUE_MASK:      data[0] = c->stencil_valuemask_back; break;
    case GL_STENCIL_BACK_FUNC:            data[0] = c->stencil_func_back; break;
    case GL_STENCIL_BACK_FAIL:            data[0] = c->stencil_sfail_back; break;
    case GL_STENCIL_BACK_PASS_DEPTH_FAIL: data[0] = c->stencil_dpfail_back; break;
    case GL_STENCIL_BACK_PASS_DEPTH_PASS: data[0] = c->stencil_dppass_back; break;
#endif

    case GL_LOGIC_OP_MODE:             data[0] = c->logic_func; break;

    //TODO implement glBlendFuncSeparate and glBlendEquationSeparate
    case GL_BLEND_SRC_RGB:             data[0] = c->blend_sRGB; break;
    case GL_BLEND_SRC_ALPHA:           data[0] = c->blend_sA; break;
    case GL_BLEND_DST_RGB:             data[0] = c->blend_dRGB; break;
    case GL_BLEND_DST_ALPHA:           data[0] = c->blend_dA; break;

    case GL_BLEND_EQUATION_RGB:        data[0] = c->blend_eqRGB; break;
    case GL_BLEND_EQUATION_ALPHA:      data[0] = c->blend_eqA; break;

    case GL_CULL_FACE_MODE:            data[0] = c->cull_mode; break;
    case GL_FRONT_FACE:                data[0] = c->front_face; break;
    case GL_DEPTH_FUNC:                data[0] = c->depth_func; break;
    case GL_POINT_SPRITE_COORD_ORIGIN: data[0] = c->point_spr_origin; break;
    case GL_PROVOKING_VERTEX:          data[0] = c->provoking_vert; break;

    case GL_MAX_TEXTURE_SIZE:         data[0] = PGL_MAX_TEXTURE_SIZE;         break;
    case GL_MAX_3D_TEXTURE_SIZE:      data[0] = PGL_MAX_3D_TEXTURE_SIZE;      break;
    case GL_MAX_ARRAY_TEXTURE_LAYERS: data[0] = PGL_MAX_ARRAY_TEXTURE_LAYERS; break;

    case GL_MAX_DEBUG_MESSAGE_LENGTH: data[0] = PGL_MAX_DEBUG_MESSAGE_LENGTH; break;

    case GL_POLYGON_MODE:
        data[0] = c->poly_mode_front;
        data[1] = c->poly_mode_back;
        break;

    case GL_VIEWPORT:
        data[0] = c->xmin;
        data[1] = c->ymin;
        data[2] = c->width;
        data[3] = c->height;
        break;

    case GL_SCISSOR_BOX:
        data[0] = c->scissor_lx;
        data[1] = c->scissor_ly;
        data[2] = c->scissor_w;
        data[3] = c->scissor_h;
        break;

    // TODO decide if 3.2 is the best approximation
    case GL_MAJOR_VERSION:             data[0] = 3; break;
    case GL_MINOR_VERSION:             data[0] = 2; break;

    case GL_ARRAY_BUFFER_BINDING:
        data[0] = c->bound_buffers[GL_ARRAY_BUFFER-GL_ARRAY_BUFFER];
        break;

    case GL_ELEMENT_ARRAY_BUFFER_BINDING:
        data[0] = c->bound_buffers[GL_ELEMENT_ARRAY_BUFFER-GL_ARRAY_BUFFER];
        break;

    case GL_VERTEX_ARRAY_BINDING:
        data[0] = c->cur_vertex_array;
        break;

    case GL_CURRENT_PROGRAM:
        data[0] = c->cur_program;
        break;


    case GL_TEXTURE_BINDING_1D:        data[0] = c->bound_textures[GL_TEXTURE_1D-GL_TEXTURE_UNBOUND-1]; break;
    case GL_TEXTURE_BINDING_2D:        data[0] = c->bound_textures[GL_TEXTURE_2D-GL_TEXTURE_UNBOUND-1]; break;
    case GL_TEXTURE_BINDING_3D:        data[0] = c->bound_textures[GL_TEXTURE_3D-GL_TEXTURE_UNBOUND-1]; break;
    case GL_TEXTURE_BINDING_1D_ARRAY:  data[0] = c->bound_textures[GL_TEXTURE_1D_ARRAY-GL_TEXTURE_UNBOUND-1]; break;
    case GL_TEXTURE_BINDING_2D_ARRAY:  data[0] = c->bound_textures[GL_TEXTURE_2D_ARRAY-GL_TEXTURE_UNBOUND-1]; break;
    case GL_TEXTURE_BINDING_RECTANGLE: data[0] = c->bound_textures[GL_TEXTURE_RECTANGLE-GL_TEXTURE_UNBOUND-1]; break;
    case GL_TEXTURE_BINDING_CUBE_MAP:  data[0] = c->bound_textures[GL_TEXTURE_CUBE_MAP-GL_TEXTURE_UNBOUND-1]; break;

    default:
        PGL_SET_ERR(GL_INVALID_ENUM);
    }
}

PGLDEF void glCullFace(GLenum mode)
{
    PGL_ERR((mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK), GL_INVALID_ENUM);
    c->cull_mode = mode;
}

PGLDEF void glFrontFace(GLenum mode)
{
    PGL_ERR((mode != GL_CCW && mode != GL_CW), GL_INVALID_ENUM);
    c->front_face = mode;
}

PGLDEF void glPolygonMode(GLenum face, GLenum mode)
{
    // TODO only support FRONT_AND_BACK like OpenGL 3/4 and OpenGL ES 2/3 ...
    // or keep support for FRONT and BACK like OpenGL 1 and 2?
    // Make final decision before version 1.0.0
    PGL_ERR(((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) ||
             (mode != GL_POINT && mode != GL_LINE && mode != GL_FILL)), GL_INVALID_ENUM);

    if (mode == GL_POINT) {
        if (face == GL_FRONT) {
            c->poly_mode_front = mode;
            c->draw_triangle_front = draw_triangle_point;
        } else if (face == GL_BACK) {
            c->poly_mode_back = mode;
            c->draw_triangle_back = draw_triangle_point;
        } else {
            c->poly_mode_front = mode;
            c->poly_mode_back = mode;
            c->draw_triangle_front = draw_triangle_point;
            c->draw_triangle_back = draw_triangle_point;
        }
    } else if (mode == GL_LINE) {
        if (face == GL_FRONT) {
            c->poly_mode_front = mode;
            c->draw_triangle_front = draw_triangle_line;
        } else if (face == GL_BACK) {
            c->poly_mode_back = mode;
            c->draw_triangle_back = draw_triangle_line;
        } else {
            c->poly_mode_front = mode;
            c->poly_mode_back = mode;
            c->draw_triangle_front = draw_triangle_line;
            c->draw_triangle_back = draw_triangle_line;
        }
    } else  {
        if (face == GL_FRONT) {
            c->poly_mode_front = mode;
            c->draw_triangle_front = draw_triangle_fill;
        } else if (face == GL_BACK) {
            c->poly_mode_back = mode;
            c->draw_triangle_back = draw_triangle_fill;
        } else {
            c->poly_mode_front = mode;
            c->poly_mode_back = mode;
            c->draw_triangle_front = draw_triangle_fill;
            c->draw_triangle_back = draw_triangle_fill;
        }
    }
}

PGLDEF void glLineWidth(GLfloat width)
{
    PGL_ERR(width <= 0.0f, GL_INVALID_VALUE);
    c->line_width = width;
}

PGLDEF void glPointSize(GLfloat size)
{
    PGL_ERR(size <= 0.0f, GL_INVALID_VALUE);
    c->point_size = size;
}

PGLDEF void glPointParameteri(GLenum pname, GLint param)
{
    //also GL_POINT_FADE_THRESHOLD_SIZE
    PGL_ERR((pname != GL_POINT_SPRITE_COORD_ORIGIN ||
            (param != GL_LOWER_LEFT && param != GL_UPPER_LEFT)), GL_INVALID_ENUM);

    c->point_spr_origin = param;
}

PGLDEF void glProvokingVertex(GLenum provokeMode)
{
    PGL_ERR((provokeMode != GL_FIRST_VERTEX_CONVENTION && provokeMode != GL_LAST_VERTEX_CONVENTION), GL_INVALID_ENUM);

    c->provoking_vert = provokeMode;
}


// Shader functions
PGLDEF GLuint pglCreateProgram(vert_func vertex_shader, frag_func fragment_shader, GLsizei n, GLenum* interpolation, GLboolean fragdepth_or_discard)
{
    // Using glAttachShader error if shader is not a shader object which
    // is the closest analog
    PGL_ERR_RET_VAL((!vertex_shader || !fragment_shader), GL_INVALID_OPERATION, 0);

    PGL_ERR_RET_VAL((n < 0 || n > GL_MAX_VERTEX_OUTPUT_COMPONENTS), GL_INVALID_VALUE, 0);

    glProgram tmp = {vertex_shader, fragment_shader, NULL, n, {0}, fragdepth_or_discard, GL_FALSE };
    for (int i=0; i<n; ++i) {
        tmp.interpolation[i] = interpolation[i];
    }

    for (int i=1; i<c->programs.size; ++i) {
        if (c->programs.a[i].deleted && (GLuint)i != c->cur_program) {
            c->programs.a[i] = tmp;
            return i;
        }
    }

    cvec_push_glProgram(&c->programs, tmp);
    return c->programs.size-1;
}

// Doesn't really do anything except mark for re-use, you
// could still use it even if it wasn't current as long as
// no new program get's assigned to the same spot
PGLDEF void glDeleteProgram(GLuint program)
{
    // This check isn't really necessary since "deleting" only marks it
    // and CreateProgram will never overwrite the 0/default shader
    if (!program)
        return;

    PGL_ERR(program >= c->programs.size, GL_INVALID_VALUE);

    c->programs.a[program].deleted = GL_TRUE;
}

PGLDEF void glUseProgram(GLuint program)
{
    // Not a problem if program is marked "deleted" already
    PGL_ERR(program >= c->programs.size, GL_INVALID_VALUE);

    c->vs_output.size = c->programs.a[program].vs_output_size;
    // c->vs_output.output_buf was pre-allocated to max size needed in init_glContext
    // otherwise would need to assure it's at least
    // c->vs_output_size * PGL_MAX_VERTS * sizeof(float) right here
    c->vs_output.interpolation = c->programs.a[program].interpolation;
    c->fragdepth_or_discard = c->programs.a[program].fragdepth_or_discard;

    c->cur_program = program;
}

PGLDEF void pglSetUniform(void* uniform)
{
    //TODO check for NULL? definitely if I ever switch to storing a local
    //copy in glProgram
    c->programs.a[c->cur_program].uniform = uniform;
}

PGLDEF void pglSetProgramUniform(GLuint program, void* uniform)
{
    // can set uniform for a "deleted" program ... but maybe I should still check and just
    // make an exception if it's the current program?
    PGL_ERR(program >= c->programs.size, GL_INVALID_OPERATION);

    c->programs.a[program].uniform = uniform;
}


PGLDEF void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    PGL_ERR((sfactor < GL_ZERO || sfactor >= NUM_BLEND_FUNCS || dfactor < GL_ZERO || dfactor >= NUM_BLEND_FUNCS), GL_INVALID_ENUM);

    c->blend_sRGB = sfactor;
    c->blend_sA = sfactor;
    c->blend_dRGB = dfactor;
    c->blend_dA = dfactor;
}

PGLDEF void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha)
{
    PGL_ERR((srcRGB < GL_ZERO || srcRGB >= NUM_BLEND_FUNCS ||
             dstRGB < GL_ZERO || dstRGB >= NUM_BLEND_FUNCS ||
             srcAlpha < GL_ZERO || srcAlpha >= NUM_BLEND_FUNCS ||
             dstAlpha < GL_ZERO || dstAlpha >= NUM_BLEND_FUNCS), GL_INVALID_ENUM);

    c->blend_sRGB = srcRGB;
    c->blend_sA = srcAlpha;
    c->blend_dRGB = dstRGB;
    c->blend_dA = dstAlpha;
}

PGLDEF void glBlendEquation(GLenum mode)
{
    PGL_ERR((mode < GL_FUNC_ADD || mode >= NUM_BLEND_EQUATIONS), GL_INVALID_ENUM);

    c->blend_eqRGB = mode;
    c->blend_eqA = mode;
}

PGLDEF void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha)
{
    PGL_ERR((modeRGB < GL_FUNC_ADD || modeRGB >= NUM_BLEND_EQUATIONS ||
        modeAlpha < GL_FUNC_ADD || modeAlpha >= NUM_BLEND_EQUATIONS), GL_INVALID_ENUM);

    c->blend_eqRGB = modeRGB;
    c->blend_eqA = modeAlpha;
}

PGLDEF void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    SET_V4(c->blend_color, clamp_01(red), clamp_01(green), clamp_01(blue), clamp_01(alpha));
}

PGLDEF void glLogicOp(GLenum opcode)
{
    PGL_ERR((opcode < GL_CLEAR || opcode > GL_INVERT), GL_INVALID_ENUM);
    c->logic_func = opcode;
}

PGLDEF void glPolygonOffset(GLfloat factor, GLfloat units)
{
    c->poly_factor = factor;
    c->poly_units = units;
}

PGLDEF void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    PGL_ERR((width < 0 || height < 0), GL_INVALID_VALUE);

    c->scissor_lx = x;
    c->scissor_ly = y;
    c->scissor_w = width;
    c->scissor_h = height;
    int ux = x+width;
    int uy = y+height;

    c->lx = MAX(x, 0);
    c->ly = MAX(y, 0);
    c->ux = MIN(ux, c->back_buffer.w);
    c->uy = MIN(uy, c->back_buffer.h);
}

#ifndef PGL_NO_STENCIL
PGLDEF void glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    PGL_ERR((func < GL_LESS || func > GL_NEVER), GL_INVALID_ENUM);

    c->stencil_func = func;
    c->stencil_func_back = func;

    // TODO clamp byte function?
    clampi(ref, 0, 255);

    c->stencil_ref = ref;
    c->stencil_ref_back = ref;

    c->stencil_valuemask = mask;
    c->stencil_valuemask_back = mask;
}

PGLDEF void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask)
{
    PGL_ERR((face < GL_FRONT || face > GL_FRONT_AND_BACK), GL_INVALID_ENUM);
    PGL_ERR((func < GL_LESS || func > GL_NEVER), GL_INVALID_ENUM);

    // TODO clamp byte function?
    clampi(ref, 0, 255);

    // Any better way to do this? I don't call glStencilFunc in case
    // I ever want/need debugging/logging info to show the function call
    if (face == GL_FRONT) {
        c->stencil_func = func;
        c->stencil_ref = ref;
        c->stencil_valuemask = mask;
    } else if (face == GL_BACK) {
        c->stencil_func_back = func;
        c->stencil_ref_back = ref;
        c->stencil_valuemask_back = mask;
    } else {
        c->stencil_func = func;
        c->stencil_ref = ref;
        c->stencil_valuemask = mask;

        c->stencil_func_back = func;
        c->stencil_ref_back = ref;
        c->stencil_valuemask_back = mask;
    }
}

PGLDEF void glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass)
{
    PGL_ERR((((sfail < GL_INVERT || sfail > GL_DECR_WRAP) && sfail != GL_ZERO) ||
            ((dpfail < GL_INVERT || dpfail > GL_DECR_WRAP) && dpfail != GL_ZERO) ||
            ((dppass < GL_INVERT || dppass > GL_DECR_WRAP) && dppass != GL_ZERO)), GL_INVALID_ENUM);

    c->stencil_sfail = sfail;
    c->stencil_dpfail = dpfail;
    c->stencil_dppass = dppass;

    c->stencil_sfail_back = sfail;
    c->stencil_dpfail_back = dpfail;
    c->stencil_dppass_back = dppass;
}

PGLDEF void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass)
{
    PGL_ERR((face < GL_FRONT || face > GL_FRONT_AND_BACK), GL_INVALID_ENUM);

    PGL_ERR((((sfail < GL_INVERT || sfail > GL_DECR_WRAP) && sfail != GL_ZERO) ||
            ((dpfail < GL_INVERT || dpfail > GL_DECR_WRAP) && dpfail != GL_ZERO) ||
            ((dppass < GL_INVERT || dppass > GL_DECR_WRAP) && dppass != GL_ZERO)), GL_INVALID_ENUM);


    if (face == GL_FRONT) {
        c->stencil_sfail = sfail;
        c->stencil_dpfail = dpfail;
        c->stencil_dppass = dppass;
    } else if (face == GL_BACK) {
        c->stencil_sfail_back = sfail;
        c->stencil_dpfail_back = dpfail;
        c->stencil_dppass_back = dppass;
    } else {
        c->stencil_sfail = sfail;
        c->stencil_dpfail = dpfail;
        c->stencil_dppass = dppass;

        c->stencil_sfail_back = sfail;
        c->stencil_dpfail_back = dpfail;
        c->stencil_dppass_back = dppass;
    }
}

PGLDEF void glClearStencil(GLint s)
{
    c->clear_stencil = s & PGL_STENCIL_MASK;
}

PGLDEF void glStencilMask(GLuint mask)
{
    c->stencil_writemask = mask;
    c->stencil_writemask_back = mask;
}

PGLDEF void glStencilMaskSeparate(GLenum face, GLuint mask)
{
    PGL_ERR((face < GL_FRONT || face > GL_FRONT_AND_BACK), GL_INVALID_ENUM);

    if (face == GL_FRONT) {
        c->stencil_writemask = mask;
    } else if (face == GL_BACK) {
        c->stencil_writemask_back = mask;
    } else {
        c->stencil_writemask = mask;
        c->stencil_writemask_back = mask;
    }
}
#endif


// Just wrap my pgl extension getter, unmap does nothing
PGLDEF void* glMapBuffer(GLenum target, GLenum access)
{
    PGL_ERR_RET_VAL((target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER), GL_INVALID_ENUM, NULL);

    PGL_ERR_RET_VAL((access != GL_READ_ONLY && access != GL_WRITE_ONLY && access != GL_READ_WRITE), GL_INVALID_ENUM, NULL);

    // adjust to access bound_buffers
    target -= GL_ARRAY_BUFFER;

    void* data = NULL;
    pglGetBufferData(c->bound_buffers[target], &data);
    return data;
}

PGLDEF void* glMapNamedBuffer(GLuint buffer, GLenum access)
{
    // TODO pglGetBufferData will verify buffer is valid, hmm
    PGL_ERR_RET_VAL((access != GL_READ_ONLY && access != GL_WRITE_ONLY && access != GL_READ_WRITE), GL_INVALID_ENUM, NULL);

    void* data = NULL;
    pglGetBufferData(buffer, &data);
    return data;
}


#ifndef PGL_EXCLUDE_STUBS

// Stubs to let real OpenGL libs compile with minimal modifications/ifdefs
// add what you need

PGLDEF const GLubyte* glGetStringi(GLenum name, GLuint index) { return NULL; }

PGLDEF void glColorMaski(GLuint buf, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {}

PGLDEF void glGenerateMipmap(GLenum target)
{
    //TODO not implemented, not sure it's worth it.
    //For example mipmap generation code see
    //https://github.com/thebeast33/cro_lib/blob/master/cro_mipmap.h
}

PGLDEF void glGetDoublev(GLenum pname, GLdouble* params) { }
PGLDEF void glGetInteger64v(GLenum pname, GLint64* params) { }

// Drawbuffers
PGLDEF void glDrawBuffers(GLsizei n, const GLenum* bufs) {}
PGLDEF void glNamedFramebufferDrawBuffers(GLuint framebuffer, GLsizei n, const GLenum* bufs) {}

// Framebuffers/Renderbuffers
PGLDEF void glGenFramebuffers(GLsizei n, GLuint* ids) {}
PGLDEF void glBindFramebuffer(GLenum target, GLuint framebuffer) {}
PGLDEF void glDeleteFramebuffers(GLsizei n, GLuint* framebuffers) {}
PGLDEF void glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level) {}
PGLDEF void glFramebufferTexture1D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {}
PGLDEF void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level) {}
PGLDEF void glFramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint layer) {}
PGLDEF GLboolean glIsFramebuffer(GLuint framebuffer) { return GL_FALSE; }

PGLDEF void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer) {}
PGLDEF void glNamedFramebufferTextureLayer(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLint layer) {}

PGLDEF void glReadBuffer(GLenum mode) {}
PGLDEF void glNamedFramebufferReadBuffer(GLuint framebuffer, GLenum mode) {}

PGLDEF void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) {}
PGLDEF void glBlitNamedFramebuffer(GLuint readFramebuffer, GLuint drawFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter) {}

PGLDEF void glGenRenderbuffers(GLsizei n, GLuint* renderbuffers) {}
PGLDEF void glBindRenderbuffer(GLenum target, GLuint renderbuffer) {}
PGLDEF void glDeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers) {}
PGLDEF void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height) {}
PGLDEF GLboolean glIsRenderbuffer(GLuint renderbuffer) { return GL_FALSE; }

PGLDEF void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) {}
PGLDEF void glNamedRenderbufferStorageMultisample(GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height) {}

PGLDEF void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer) {}
// Could also return GL_FRAMEBUFFER_UNDEFINED, but then I'd have to add all
// those enums and really 0 signaling an error makes more sense
PGLDEF GLenum glCheckFramebufferStatus(GLenum target) { return 0; }

PGLDEF void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint* value) {}
PGLDEF void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint* value) {}
PGLDEF void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value) {}
PGLDEF void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) {}
PGLDEF void glClearNamedFramebufferiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLint* value) {}
PGLDEF void glClearNamedFramebufferuiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLuint* value) {}
PGLDEF void glClearNamedFramebufferfv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat* value) {}
PGLDEF void glClearNamedFramebufferfi(GLuint framebuffer, GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) {}

PGLDEF void glGetProgramiv(GLuint program, GLenum pname, GLint* params) { }
PGLDEF void glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei* length, GLchar* infoLog) { }
PGLDEF void glAttachShader(GLuint program, GLuint shader) { }
PGLDEF void glCompileShader(GLuint shader) { }
PGLDEF void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog) { }
PGLDEF void glLinkProgram(GLuint program) { }
PGLDEF void glShaderSource(GLuint shader, GLsizei count, const GLchar** string, const GLint* length) { }
PGLDEF void glGetShaderiv(GLuint shader, GLenum pname, GLint* params) { }
PGLDEF void glDeleteShader(GLuint shader) { }
PGLDEF void glDetachShader(GLuint program, GLuint shader) { }

PGLDEF GLuint glCreateProgram(void) { return 0; }
PGLDEF GLuint glCreateShader(GLenum shaderType) { return 0; }
PGLDEF GLint glGetUniformLocation(GLuint program, const GLchar* name) { return 0; }
PGLDEF GLint glGetAttribLocation(GLuint program, const GLchar* name) { return 0; }

PGLDEF GLboolean glUnmapBuffer(GLenum target) { return GL_TRUE; }
PGLDEF GLboolean glUnmapNamedBuffer(GLuint buffer) { return GL_TRUE; }

// TODO?

PGLDEF void glActiveTexture(GLenum texture) { }
PGLDEF void glTexParameterf(GLenum target, GLenum pname, GLfloat param) {}

PGLDEF void glTextureParameterf(GLuint texture, GLenum pname, GLfloat param) {}

// TODO what the heck are these?
PGLDEF void glTexParameterliv(GLenum target, GLenum pname, const GLint* params) {}
PGLDEF void glTexParameterluiv(GLenum target, GLenum pname, const GLuint* params) {}

PGLDEF void glTextureParameterliv(GLuint texture, GLenum pname, const GLint* params) {}
PGLDEF void glTextureParameterluiv(GLuint texture, GLenum pname, const GLuint* params) {}

PGLDEF void glCompressedTexImage1D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const GLvoid* data) {}
PGLDEF void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid* data) {}
PGLDEF void glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid* data) {}

PGLDEF void glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer) { }
PGLDEF void glTextureBuffer(GLuint texture, GLenum internalformat, GLuint buffer) { }


PGLDEF void glUniform1f(GLint location, GLfloat v0) { }
PGLDEF void glUniform2f(GLint location, GLfloat v0, GLfloat v1) { }
PGLDEF void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) { }
PGLDEF void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) { }

PGLDEF void glUniform1i(GLint location, GLint v0) { }
PGLDEF void glUniform2i(GLint location, GLint v0, GLint v1) { }
PGLDEF void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2) { }
PGLDEF void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) { }

PGLDEF void glUniform1ui(GLint location, GLuint v0) { }
PGLDEF void glUniform2ui(GLint location, GLuint v0, GLuint v1) { }
PGLDEF void glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) { }
PGLDEF void glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) { }

PGLDEF void glUniform1fv(GLint location, GLsizei count, const GLfloat* value) { }
PGLDEF void glUniform2fv(GLint location, GLsizei count, const GLfloat* value) { }
PGLDEF void glUniform3fv(GLint location, GLsizei count, const GLfloat* value) { }
PGLDEF void glUniform4fv(GLint location, GLsizei count, const GLfloat* value) { }

PGLDEF void glUniform1iv(GLint location, GLsizei count, const GLint* value) { }
PGLDEF void glUniform2iv(GLint location, GLsizei count, const GLint* value) { }
PGLDEF void glUniform3iv(GLint location, GLsizei count, const GLint* value) { }
PGLDEF void glUniform4iv(GLint location, GLsizei count, const GLint* value) { }

PGLDEF void glUniform1uiv(GLint location, GLsizei count, const GLuint* value) { }
PGLDEF void glUniform2uiv(GLint location, GLsizei count, const GLuint* value) { }
PGLDEF void glUniform3uiv(GLint location, GLsizei count, const GLuint* value) { }
PGLDEF void glUniform4uiv(GLint location, GLsizei count, const GLuint* value) { }

PGLDEF void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }
PGLDEF void glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) { }

#endif


/*************************************
 *  GLSL(ish) functions
 *************************************/

/*
float clampf_01(float f)
{
    if (f < 0.0f) return 0.0f;
    if (f > 1.0f) return 1.0f;
    return f;
}

float clampf(float f, float min, float max)
{
    if (f < min) return min;
    if (f > max) return max;
    return f;
}

int clampi(int i, int min, int max)
{
    if (i < min) return min;
    if (i > max) return max;
    return i;
}
*/


// TODO maybe I should put this in crsw_math/rsw_math? static inline?
// guarantees positive mod result
#define positive_mod(a, b) (((a) % (b) + (b)) % (b))

// if I only wanted to support power of 2 textures...
#define positive_mod_pow_of_2(i, n) ((i) & ((n) - 1) + (n)) & ((n) - 1)

// TODO should this be in rsw_math
#define mirror(i) (i) >= 0 ? (i) : -(1 + (i))

// See page 174 of GL 3.3 core spec.
static int wrap(int i, int size, GLenum mode)
{
    switch (mode)
    {
    case GL_REPEAT:
        return positive_mod(i, size);

    // Border is too much of a pain to implement with render to
    // texture.  Trade offs in poor performance or ugly extra code
    // for a feature that almost no one actually uses and even
    // when it is used (barring rare/odd uv coordinates) it's not
    // even noticable.
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
    case GL_CLAMP_TO_BORDER:
        if (i >= 0 && i < size) return i;
        return -1;
        // Would use if we went back to literally surrounding textures with a border
        //return clampi(i, -1, size);
#else
    case GL_CLAMP_TO_BORDER:  // just so stuff that uses it compiles
#endif
    case GL_CLAMP_TO_EDGE:
        return clampi(i, 0, size-1);

    case GL_MIRRORED_REPEAT: {
        int sz2 = 2*size;
        i = positive_mod(i, sz2);
        i -= size;
        i = mirror(i);
        i = size - 1 - i;
        return i;
    } break;
    default:
        //should never happen, get rid of compile warning
        assert(0);
        return 0;
    }
}
#undef imod
#undef positive_mod
#undef positive_mod_pow_of_2


// hmm should I have these take a glTexture* somehow?
// It would save the check for 0 for every single access

// used in the following 4 texture access functions
// Not sure if it's actually necessary since wrap() clamps
#define EPSILON 0.000001
PGLDEF vec4 texture1D(GLuint tex, float x)
{
    int i0, i1;

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_1D-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;

    float w = t->w - EPSILON;

    float xw = x * w;

    if (t->mag_filter == GL_NEAREST) {
        i0 = wrap(floorf(xw), t->w, t->wrap_s);

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        if (i0 < 0) return t->border_color;
#endif

        return Color_to_v4(texdata[i0]);

    } else {
        // LINEAR
        // This seems right to me since pixel centers are 0.5 but
        // this isn't exactly what's described in the spec or FoCG
        i0 = wrap(floorf(xw - 0.5f), t->w, t->wrap_s);
        i1 = wrap(floorf(xw + 0.499999f), t->w, t->wrap_s);

        float tmp2;
        float alpha = modff(xw+0.5f, &tmp2);
        if (alpha < 0) ++alpha;

        //hermite smoothing is optional
        //looks like my nvidia implementation doesn't do it
        //but it can look a little better
#ifdef PGL_HERMITE_SMOOTHING
        alpha = alpha*alpha * (3 - 2*alpha);
#endif

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        vec4 ci, ci1;
        if (i0 < 0) ci = t->border_color;
        else ci = Color_to_v4(texdata[i0]);

        if (i1 < 0) ci1 = t->border_color;
        else ci1 = Color_to_v4(texdata[i1]);
#else
        vec4 ci = Color_to_v4(texdata[i0]);
        vec4 ci1 = Color_to_v4(texdata[i1]);
#endif

        ci = scale_v4(ci, (1-alpha));
        ci1 = scale_v4(ci1, alpha);

        ci = add_v4s(ci, ci1);

        return ci;
    }
}

PGLDEF vec4 texture2D(GLuint tex, float x, float y)
{
    int i0, j0, i1, j1;

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_2D-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;

    int w = t->w;
    int h = t->h;

    float dw = w - EPSILON;
    float dh = h - EPSILON;

    float xw = x * dw;
    float yh = y * dh;

    // TODO don't just use mag_filter all the time?
    // is it worth bothering?
    // Or maybe it makes more sense to use min_filter all the time
    // since that defaults to NEAREST?
    if (t->mag_filter == GL_NEAREST) {
        i0 = wrap(floorf(xw), w, t->wrap_s);
        j0 = wrap(floorf(yh), h, t->wrap_t);

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        if ((i0 | j0) < 0) return t->border_color;
#endif
        return Color_to_v4(texdata[j0*w + i0]);

    } else {
        // LINEAR
        // This seems right to me since pixel centers are 0.5 but
        // this isn't exactly what's described in the spec or FoCG
        i0 = wrap(floorf(xw - 0.5f), w, t->wrap_s);
        j0 = wrap(floorf(yh - 0.5f), h, t->wrap_t);
        i1 = wrap(floorf(xw + 0.499999f), w, t->wrap_s);
        j1 = wrap(floorf(yh + 0.499999f), h, t->wrap_t);

        float tmp2;
        float alpha = modff(xw+0.5f, &tmp2);
        float beta = modff(yh+0.5f, &tmp2);
        if (alpha < 0) ++alpha;
        if (beta < 0) ++beta;

        //hermite smoothing is optional
        //looks like my nvidia implementation doesn't do it
        //but it can look a little better
#ifdef PGL_HERMITE_SMOOTHING
        alpha = alpha*alpha * (3 - 2*alpha);
        beta = beta*beta * (3 - 2*beta);
#endif


#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        vec4 cij, ci1j, cij1, ci1j1;
        if ((i0 | j0) < 0) cij = t->border_color;
        else cij = Color_to_v4(texdata[j0*w + i0]);

        if ((i1 | j0) < 0) ci1j = t->border_color;
        else ci1j = Color_to_v4(texdata[j0*w + i1]);

        if ((i0 | j1) < 0) cij1 = t->border_color;
        else cij1 = Color_to_v4(texdata[j1*w + i0]);

        if ((i1 | j1) < 0) ci1j1 = t->border_color;
        else ci1j1 = Color_to_v4(texdata[j1*w + i1]);
#else
        vec4 cij = Color_to_v4(texdata[j0*w + i0]);
        vec4 ci1j = Color_to_v4(texdata[j0*w + i1]);
        vec4 cij1 = Color_to_v4(texdata[j1*w + i0]);
        vec4 ci1j1 = Color_to_v4(texdata[j1*w + i1]);
#endif

        cij = scale_v4(cij, (1-alpha)*(1-beta));
        ci1j = scale_v4(ci1j, alpha*(1-beta));
        cij1 = scale_v4(cij1, (1-alpha)*beta);
        ci1j1 = scale_v4(ci1j1, alpha*beta);

        cij = add_v4s(cij, ci1j);
        cij = add_v4s(cij, cij1);
        cij = add_v4s(cij, ci1j1);

        return cij;
    }
}

PGLDEF vec4 texture3D(GLuint tex, float x, float y, float z)
{
    int i0, j0, i1, j1, k0, k1;

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_3D-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;

    float dw = t->w - EPSILON;
    float dh = t->h - EPSILON;
    float dd = t->d - EPSILON;

    int w = t->w;
    int h = t->h;
    int d = t->d;
    int plane = w * t->h;
    float xw = x * dw;
    float yh = y * dh;
    float zd = z * dd;


    if (t->mag_filter == GL_NEAREST) {
        i0 = wrap(floorf(xw), w, t->wrap_s);
        j0 = wrap(floorf(yh), h, t->wrap_t);
        k0 = wrap(floorf(zd), d, t->wrap_r);

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        if ((i0 | j0 | k0) < 0) return t->border_color;
#endif

        return Color_to_v4(texdata[k0*plane + j0*w + i0]);

    } else {
        // LINEAR
        // This seems right to me since pixel centers are 0.5 but
        // this isn't exactly what's described in the spec or FoCG
        i0 = wrap(floorf(xw - 0.5f), w, t->wrap_s);
        j0 = wrap(floorf(yh - 0.5f), h, t->wrap_t);
        k0 = wrap(floorf(zd - 0.5f), d, t->wrap_r);
        i1 = wrap(floorf(xw + 0.499999f), w, t->wrap_s);
        j1 = wrap(floorf(yh + 0.499999f), h, t->wrap_t);
        k1 = wrap(floorf(zd + 0.499999f), d, t->wrap_r);

        float tmp2;
        float alpha = modff(xw+0.5f, &tmp2);
        float beta = modff(yh+0.5f, &tmp2);
        float gamma = modff(zd+0.5f, &tmp2);
        if (alpha < 0) ++alpha;
        if (beta < 0) ++beta;
        if (gamma < 0) ++gamma;

        //hermite smoothing is optional
        //looks like my nvidia implementation doesn't do it
        //but it can look a little better
#ifdef PGL_HERMITE_SMOOTHING
        alpha = alpha*alpha * (3 - 2*alpha);
        beta = beta*beta * (3 - 2*beta);
        gamma = gamma*gamma * (3 - 2*gamma);
#endif

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        vec4 cijk, ci1jk, cij1k, ci1j1k, cijk1, ci1jk1, cij1k1, ci1j1k1;
        if ((i0 | j0 | k0) < 0) cijk = t->border_color;
        else cijk = Color_to_v4(texdata[k0*plane + j0*w + i0]);

        if ((i1 | j0 | k0) < 0) ci1jk = t->border_color;
        else ci1jk = Color_to_v4(texdata[k0*plane + j0*w + i1]);

        if ((i0 | j1 | k0) < 0) cij1k = t->border_color;
        else cij1k = Color_to_v4(texdata[k0*plane + j1*w + i0]);

        if ((i1 | j1 | k0) < 0) ci1j1k = t->border_color;
        else ci1j1k = Color_to_v4(texdata[k0*plane + j1*w + i1]);

        if ((i0 | j0 | k1) < 0) cijk1 = t->border_color;
        else cijk1 = Color_to_v4(texdata[k1*plane + j0*w + i0]);

        if ((i1 | j0 | k1) < 0) ci1jk1 = t->border_color;
        else ci1jk1 = Color_to_v4(texdata[k1*plane + j0*w + i1]);

        if ((i0 | j1 | k1) < 0) cij1k1 = t->border_color;
        else cij1k1 = Color_to_v4(texdata[k1*plane + j1*w + i0]);

        if ((i1 | j1 | k1) < 0) ci1j1k1 = t->border_color;
        else ci1j1k1 = Color_to_v4(texdata[k1*plane + j1*w + i1]);
#else
        vec4 cijk = Color_to_v4(texdata[k0*plane + j0*w + i0]);
        vec4 ci1jk = Color_to_v4(texdata[k0*plane + j0*w + i1]);
        vec4 cij1k = Color_to_v4(texdata[k0*plane + j1*w + i0]);
        vec4 ci1j1k = Color_to_v4(texdata[k0*plane + j1*w + i1]);
        vec4 cijk1 = Color_to_v4(texdata[k1*plane + j0*w + i0]);
        vec4 ci1jk1 = Color_to_v4(texdata[k1*plane + j0*w + i1]);
        vec4 cij1k1 = Color_to_v4(texdata[k1*plane + j1*w + i0]);
        vec4 ci1j1k1 = Color_to_v4(texdata[k1*plane + j1*w + i1]);
#endif

        cijk = scale_v4(cijk, (1-alpha)*(1-beta)*(1-gamma));
        ci1jk = scale_v4(ci1jk, alpha*(1-beta)*(1-gamma));
        cij1k = scale_v4(cij1k, (1-alpha)*beta*(1-gamma));
        ci1j1k = scale_v4(ci1j1k, alpha*beta*(1-gamma));
        cijk1 = scale_v4(cijk1, (1-alpha)*(1-beta)*gamma);
        ci1jk1 = scale_v4(ci1jk1, alpha*(1-beta)*gamma);
        cij1k1 = scale_v4(cij1k1, (1-alpha)*beta*gamma);
        ci1j1k1 = scale_v4(ci1j1k1, alpha*beta*gamma);

        cijk = add_v4s(cijk, ci1jk);
        cijk = add_v4s(cijk, cij1k);
        cijk = add_v4s(cijk, ci1j1k);
        cijk = add_v4s(cijk, cijk1);
        cijk = add_v4s(cijk, ci1jk1);
        cijk = add_v4s(cijk, cij1k1);
        cijk = add_v4s(cijk, ci1j1k1);

        return cijk;
    }
}

// for now this should work
PGLDEF vec4 texture2DArray(GLuint tex, float x, float y, int z)
{
    int i0, j0, i1, j1;

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_2D_ARRAY-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;
    int w = t->w;
    int h = t->h;

    float dw = w - EPSILON;
    float dh = h - EPSILON;

    int plane = w * h;
    float xw = x * dw;
    float yh = y * dh;


    if (t->mag_filter == GL_NEAREST) {
        i0 = wrap(floorf(xw), w, t->wrap_s);
        j0 = wrap(floorf(yh), h, t->wrap_t);

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        if ((i0 | j0) < 0) return t->border_color;
#endif
        return Color_to_v4(texdata[z*plane + j0*w + i0]);

    } else {
        // LINEAR
        // This seems right to me since pixel centers are 0.5 but
        // this isn't exactly what's described in the spec or FoCG
        i0 = wrap(floorf(xw - 0.5f), w, t->wrap_s);
        j0 = wrap(floorf(yh - 0.5f), h, t->wrap_t);
        i1 = wrap(floorf(xw + 0.499999f), w, t->wrap_s);
        j1 = wrap(floorf(yh + 0.499999f), h, t->wrap_t);

        float tmp2;
        float alpha = modff(xw+0.5f, &tmp2);
        float beta = modff(yh+0.5f, &tmp2);
        if (alpha < 0) ++alpha;
        if (beta < 0) ++beta;

        //hermite smoothing is optional
        //looks like my nvidia implementation doesn't do it
        //but it can look a little better
#ifdef PGL_HERMITE_SMOOTHING
        alpha = alpha*alpha * (3 - 2*alpha);
        beta = beta*beta * (3 - 2*beta);
#endif

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        vec4 cij, ci1j, cij1, ci1j1;
        if ((i0 | j0) < 0) cij = t->border_color;
        else cij = Color_to_v4(texdata[z*plane + j0*w + i0]);

        if ((i1 | j0) < 0) ci1j = t->border_color;
        else ci1j = Color_to_v4(texdata[z*plane + j0*w + i1]);

        if ((i0 | j1) < 0) cij1 = t->border_color;
        else cij1 = Color_to_v4(texdata[z*plane + j1*w + i0]);

        if ((i1 | j1) < 0) ci1j1 = t->border_color;
        else ci1j1 = Color_to_v4(texdata[z*plane + j1*w + i1]);
#else
        vec4 cij = Color_to_v4(texdata[z*plane + j0*w + i0]);
        vec4 ci1j = Color_to_v4(texdata[z*plane + j0*w + i1]);
        vec4 cij1 = Color_to_v4(texdata[z*plane + j1*w + i0]);
        vec4 ci1j1 = Color_to_v4(texdata[z*plane + j1*w + i1]);
#endif

        cij = scale_v4(cij, (1-alpha)*(1-beta));
        ci1j = scale_v4(ci1j, alpha*(1-beta));
        cij1 = scale_v4(cij1, (1-alpha)*beta);
        ci1j1 = scale_v4(ci1j1, alpha*beta);

        cij = add_v4s(cij, ci1j);
        cij = add_v4s(cij, cij1);
        cij = add_v4s(cij, ci1j1);

        return cij;
    }
}

PGLDEF vec4 texture_rect(GLuint tex, float x, float y)
{
    int i0, j0, i1, j1;

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_RECTANGLE-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;

    int w = t->w;
    int h = t->h;

    float xw = x;
    float yh = y;

    //TODO don't just use mag_filter all the time?
    //is it worth bothering?
    if (t->mag_filter == GL_NEAREST) {
        i0 = wrap(floorf(xw), w, t->wrap_s);
        j0 = wrap(floorf(yh), h, t->wrap_t);

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        if ((i0 | j0) < 0) return t->border_color;
#endif
        return Color_to_v4(texdata[j0*w + i0]);

    } else {
        // LINEAR
        // This seems right to me since pixel centers are 0.5 but
        // this isn't exactly what's described in the spec or FoCG
        i0 = wrap(floorf(xw - 0.5f), w, t->wrap_s);
        j0 = wrap(floorf(yh - 0.5f), h, t->wrap_t);
        i1 = wrap(floorf(xw + 0.499999f), w, t->wrap_s);
        j1 = wrap(floorf(yh + 0.499999f), h, t->wrap_t);

        float tmp2;
        float alpha = modff(xw+0.5f, &tmp2);
        float beta = modff(yh+0.5f, &tmp2);
        if (alpha < 0) ++alpha;
        if (beta < 0) ++beta;

        //hermite smoothing is optional
        //looks like my nvidia implementation doesn't do it
        //but it can look a little better
#ifdef PGL_HERMITE_SMOOTHING
        alpha = alpha*alpha * (3 - 2*alpha);
        beta = beta*beta * (3 - 2*beta);
#endif

#ifdef PGL_ENABLE_CLAMP_TO_BORDER
        vec4 cij, ci1j, cij1, ci1j1;
        if ((i0 | j0) < 0) cij = t->border_color;
        else cij = Color_to_v4(texdata[j0*w + i0]);

        if ((i1 | j0) < 0) ci1j = t->border_color;
        else ci1j = Color_to_v4(texdata[j0*w + i1]);

        if ((i0 | j1) < 0) cij1 = t->border_color;
        else cij1 = Color_to_v4(texdata[j1*w + i0]);

        if ((i1 | j1) < 0) ci1j1 = t->border_color;
        else ci1j1 = Color_to_v4(texdata[j1*w + i1]);
#else
        vec4 cij = Color_to_v4(texdata[j0*w + i0]);
        vec4 ci1j = Color_to_v4(texdata[j0*w + i1]);
        vec4 cij1 = Color_to_v4(texdata[j1*w + i0]);
        vec4 ci1j1 = Color_to_v4(texdata[j1*w + i1]);
#endif

        cij = scale_v4(cij, (1-alpha)*(1-beta));
        ci1j = scale_v4(ci1j, alpha*(1-beta));
        cij1 = scale_v4(cij1, (1-alpha)*beta);
        ci1j1 = scale_v4(ci1j1, alpha*beta);

        cij = add_v4s(cij, ci1j);
        cij = add_v4s(cij, cij1);
        cij = add_v4s(cij, ci1j1);

        return cij;
    }
}

PGLDEF vec4 texture_cubemap(GLuint texture, float x, float y, float z)
{
    glTexture* tex = NULL;
    if (texture) {
        tex = &c->textures.a[texture];
    } else {
        tex = &c->default_textures[GL_TEXTURE_CUBE_MAP-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)tex->data;

    float x_mag = (x < 0) ? -x : x;
    float y_mag = (y < 0) ? -y : y;
    float z_mag = (z < 0) ? -z : z;

    float s, t, max;

    int p, i0, j0, i1, j1;

    //there should be a better/shorter way to do this ...
    if (x_mag > y_mag) {
        if (x_mag > z_mag) {  //x largest
            max = x_mag;
            t = -y;
            if (x_mag == x) {
                p = 0;
                s = -z;
            } else {
                p = 1;
                s = z;
            }
        } else { //z largest
            max = z_mag;
            t = -y;
            if (z_mag == z) {
                p = 4;
                s = x;
            } else {
                p = 5;
                s = -x;
            }
        }
    } else {
        if (y_mag > z_mag) {  //y largest
            max = y_mag;
            s = x;
            if (y_mag == y) {
                p = 2;
                t = z;
            } else {
                p = 3;
                t = -z;
            }
        } else { //z largest
            max = z_mag;
            t = -y;
            if (z_mag == z) {
                p = 4;
                s = x;
            } else {
                p = 5;
                s = -x;
            }
        }
    }

    // TODO As I understand this, this prevents x and y from ever being
    // outside [0, 1] so there's no need for me to put CLAMP_TO_BORDER ifdefs
    // in here, since even CLAMP_TO_EDGE should never happen.
    x = (s/max + 1.0f)/2.0f;
    y = (t/max + 1.0f)/2.0f;

    int w = tex->w;
    int h = tex->h;

    float dw = w - EPSILON;
    float dh = h - EPSILON;

    int plane = w*w;
    float xw = x * dw;
    float yh = y * dh;

    if (tex->mag_filter == GL_NEAREST) {
        i0 = wrap(floorf(xw), w, tex->wrap_s);
        j0 = wrap(floorf(yh), h, tex->wrap_t);

        vec4 tmpvec4 = Color_to_v4(texdata[p*plane + j0*w + i0]);
        return tmpvec4;

    } else {
        // LINEAR
        // This seems right to me since pixel centers are 0.5 but
        // this isn't exactly what's described in the spec or FoCG
        i0 = wrap(floorf(xw - 0.5f), tex->w, tex->wrap_s);
        j0 = wrap(floorf(yh - 0.5f), tex->h, tex->wrap_t);
        i1 = wrap(floorf(xw + 0.499999f), tex->w, tex->wrap_s);
        j1 = wrap(floorf(yh + 0.499999f), tex->h, tex->wrap_t);

        float tmp2;
        float alpha = modff(xw+0.5f, &tmp2);
        float beta = modff(yh+0.5f, &tmp2);
        if (alpha < 0) ++alpha;
        if (beta < 0) ++beta;

        //hermite smoothing is optional
        //looks like my nvidia implementation doesn't do it
        //but it can look a little better
#ifdef PGL_HERMITE_SMOOTHING
        alpha = alpha*alpha * (3 - 2*alpha);
        beta = beta*beta * (3 - 2*beta);
#endif

        vec4 cij = Color_to_v4(texdata[p*plane + j0*w + i0]);
        vec4 ci1j = Color_to_v4(texdata[p*plane + j0*w + i1]);
        vec4 cij1 = Color_to_v4(texdata[p*plane + j1*w + i0]);
        vec4 ci1j1 = Color_to_v4(texdata[p*plane + j1*w + i1]);

        cij = scale_v4(cij, (1-alpha)*(1-beta));
        ci1j = scale_v4(ci1j, alpha*(1-beta));
        cij1 = scale_v4(cij1, (1-alpha)*beta);
        ci1j1 = scale_v4(ci1j1, alpha*beta);

        cij = add_v4s(cij, ci1j);
        cij = add_v4s(cij, cij1);
        cij = add_v4s(cij, ci1j1);

        return cij;
    }
}

PGLDEF vec4 texelFetch1D(GLuint tex, int x, int lod)
{
    PGL_UNUSED(lod);

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_1D-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;

    return Color_to_v4(texdata[x]);
}

PGLDEF vec4 texelFetch2D(GLuint tex, int x, int y, int lod)
{
    PGL_UNUSED(lod);

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_2D-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;
    return Color_to_v4(texdata[x*t->w + y]);
}

PGLDEF vec4 texelFetch3D(GLuint tex, int x, int y, int z, int lod)
{
    PGL_UNUSED(lod);

    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_3D-GL_TEXTURE_1D];
    }
    Color* texdata = (Color*)t->data;
    int w = t->w;
    int plane = w * t->h;
    return Color_to_v4(texdata[z*plane + y*w + x]);
}

PGLDEF ivec3 textureSize(GLuint tex, GLint lod)
{
    PGL_UNUSED(lod);
    glTexture* t = NULL;
    if (tex) {
        t = &c->textures.a[tex];
    } else {
        t = &c->default_textures[GL_TEXTURE_1D-GL_TEXTURE_1D];
    }
    return make_iv3(t->w, t->h, t->d);
}

#undef EPSILON


//Raw draw functions that bypass the OpenGL pipeline and draw
//points/lines/triangles directly to the framebuffer, modify as needed.
//
//Example modifications:
//add the blending part of OpenGL to put_pixel
//change them to take vec4's instead of Color's
//change put_triangle to draw all one color or have a separate path/function
//that draws a single color triangle faster (no need to blend)
//
//pass the framebuffer in instead of drawing to c->back_buffer so 
//you can use it elsewhere, independently of a glContext
//etc.
//
PGLDEF void pglClearScreen(void)
{
    memset(c->back_buffer.buf, 255, c->back_buffer.w * c->back_buffer.h * sizeof(pix_t));
}

PGLDEF void pglSetInterp(GLsizei n, GLenum* interpolation)
{
    c->programs.a[c->cur_program].vs_output_size = n;
    c->vs_output.size = n;

    memcpy(c->programs.a[c->cur_program].interpolation, interpolation, n*sizeof(GLenum));

    // c->vs_output.output_buf was pre-allocated to max size needed in init_glContext
    // otherwise would need to assure it's at least
    // c->vs_output_size * PGL_MAX_VERTS * sizeof(float) right here

    //vs_output.interpolation would be already pointing at current program's array
    //unless the programs array was realloced since the last glUseProgram because
    //they've created a bunch of programs.  Unlikely they'd be changing a shader
    //before creating all their shaders but whatever.
    c->vs_output.interpolation = c->programs.a[c->cur_program].interpolation;
}


// Uses default_vs for vertex shader (passes vertex unchanged, no other attributes or outputs)
// This function is designed to be used with pglDrawFrame(), you don't need it for pglDrawFrame2()
PGLDEF GLuint pglCreateFragProgram(frag_func fragment_shader, GLboolean fragdepth_or_discard)
{
    // Using glAttachShader error if shader is not a shader object which
    // is the closest analog
    PGL_ERR_RET_VAL((!fragment_shader), GL_INVALID_OPERATION, 0);

    glProgram tmp = {default_vs, fragment_shader, NULL, 0, {0}, fragdepth_or_discard, GL_FALSE };

    for (int i=1; i<c->programs.size; ++i) {
        if (c->programs.a[i].deleted && (GLuint)i != c->cur_program) {
            c->programs.a[i] = tmp;
            return i;
        }
    }

    cvec_push_glProgram(&c->programs, tmp);
    return c->programs.size-1;
}

//TODO
//pglDrawRect(x, y, w, h)
//pglDrawPoint(x, y)
//
// TODO worth another draw_pixel() that never does fragment_processing?
// worth duplicating the loops for initial discard check?
PGLDEF void pglDrawFrame(void)
{
    frag_func frag_shader = c->programs.a[c->cur_program].fragment_shader;
    void* uniforms = c->programs.a[c->cur_program].uniform;

    Shader_Builtins builtins;
    //#pragma omp parallel for private(builtins)
    for (int y=0; y<c->back_buffer.h; ++y) {
        for (int x=0; x<c->back_buffer.w; ++x) {

            //ignore z and w components
            builtins.gl_FragCoord.x = x + 0.5f;
            builtins.gl_FragCoord.y = y + 0.5f;

            builtins.discard = GL_FALSE;
            frag_shader(NULL, &builtins, uniforms);
            if (!builtins.discard)
                draw_pixel(builtins.gl_FragColor, x, y, 0.0f, GL_FALSE);  //scissor/stencil/depth aren't used for pglDrawFrame
        }
    }

}

PGLDEF void pglDrawFrame2(frag_func frag_shader, void* uniforms)
{
    Shader_Builtins builtins;
    //#pragma omp parallel for private(builtins)
    for (int y=0; y<c->back_buffer.h; ++y) {
        for (int x=0; x<c->back_buffer.w; ++x) {

            //ignore z and w components
            builtins.gl_FragCoord.x = x + 0.5f;
            builtins.gl_FragCoord.y = y + 0.5f;

            builtins.discard = GL_FALSE;
            frag_shader(NULL, &builtins, uniforms);
            if (!builtins.discard)
                draw_pixel(builtins.gl_FragColor, x, y, 0.0f, GL_FALSE);  //scissor/stencil/depth aren't used for pglDrawFrame
        }
    }
}

PGLDEF void pglBufferData(GLenum target, GLsizei size, const GLvoid* data, GLenum usage)
{
    //TODO check for usage later
    PGL_UNUSED(usage);

    PGL_ERR((target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER), GL_INVALID_ENUM);

    target -= GL_ARRAY_BUFFER;
    PGL_ERR(!c->bound_buffers[target], GL_INVALID_OPERATION);

    // data can't be null for user_owned data
    PGL_ERR(!data, GL_INVALID_VALUE);

    // TODO Should I change this in spec functions too?  Or just say don't mix them
    // otherwise bad things/undefined behavior??
    if (!c->buffers.a[c->bound_buffers[target]].user_owned) {
        free(c->buffers.a[c->bound_buffers[target]].data);
    }

    // user_owned buffer, just assign the pointer, will not free
    c->buffers.a[c->bound_buffers[target]].data = (u8*)data;

    c->buffers.a[c->bound_buffers[target]].user_owned = GL_TRUE;
    c->buffers.a[c->bound_buffers[target]].size = size;

    if (target == GL_ELEMENT_ARRAY_BUFFER) {
        c->vertex_arrays.a[c->cur_vertex_array].element_buffer = c->bound_buffers[target];
    }
}

// TODO/NOTE
// All pglTexImage* functions expect the user to pass in packed GL_RGBA
// data. Unlike glTexImage*, no conversion is done, and format != GL_RGBA
// is an INVALID_ENUM error
//
// At least the latter part will change if I ever expand internal format
// support
PGLDEF void pglTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_ERR(target != GL_TEXTURE_1D, GL_INVALID_ENUM);
    GLuint cur_tex = c->bound_textures[target-GL_TEXTURE_UNBOUND-1];

    pglTextureImage1D(cur_tex, level, internalformat, width, border, format, type, data);
}

PGLDEF void pglTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    // NOTE, since this is mapping data, the entire cubemap has to already be arranged in memory in the correct order and we only
    // accept GL_TEXTURE_CUBE_MAP, not any of the individual planes as that wouldn't make sense
    PGL_ERR((target != GL_TEXTURE_2D &&
             target != GL_TEXTURE_RECTANGLE &&
             target != GL_TEXTURE_CUBE_MAP), GL_INVALID_ENUM);

    GLuint cur_tex = c->bound_textures[target-GL_TEXTURE_UNBOUND-1];

    pglTextureImage2D(cur_tex, level, internalformat, width, height, border, format, type, data);
}

PGLDEF void pglTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_ERR((target != GL_TEXTURE_3D && target != GL_TEXTURE_2D_ARRAY), GL_INVALID_ENUM);

    GLuint cur_tex = c->bound_textures[target-GL_TEXTURE_UNBOUND-1];

    pglTextureImage3D(cur_tex, level, internalformat, width, height, depth, border, format, type, data);
}

PGLDEF void pglTextureImage1D(GLuint texture, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    // ignore level and internalformat for now
    // (the latter is always converted to RGBA32 anyway)
    PGL_UNUSED(level);
    PGL_UNUSED(internalformat);

    PGL_ERR(border, GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);

    // data can't be null for user_owned data
    PGL_ERR(!data, GL_INVALID_VALUE);

    // I do not support DSA mapping of default texture 0, for convenience, no way to know which target it was
    // and I don't want to duplicate code or add extra functions
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);

    c->textures.a[texture].w = width;
    c->textures.a[texture].h = 1;
    c->textures.a[texture].d = 1;

    // TODO see pglBufferData
    if (!c->textures.a[texture].user_owned)
        free(c->textures.a[texture].data);

    //TODO support other internal formats? components should be of internalformat not format
    c->textures.a[texture].data = (u8*)data;
    c->textures.a[texture].user_owned = GL_TRUE;
}

PGLDEF void pglTextureImage2D(GLuint texture, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);
    PGL_UNUSED(internalformat);

    PGL_ERR(border, GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);

    // data can't be null for user_owned data
    PGL_ERR(!data, GL_INVALID_VALUE);

    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);

    // have to convert type back from offset to actual enum value
    GLenum target = c->textures.a[texture].type + GL_TEXTURE_UNBOUND + 1;
    if (target == GL_TEXTURE_2D || target == GL_TEXTURE_RECTANGLE) {
        c->textures.a[texture].w = width;
        c->textures.a[texture].h = height;
        c->textures.a[texture].d = 1;

        // TODO see pglBufferData
        if (!c->textures.a[texture].user_owned)
            free(c->textures.a[texture].data);

        // If you're using these pgl mapped functions, it assumes you are respecting
        // your own current unpack alignment settings already
        c->textures.a[texture].data = (u8*)data;
        c->textures.a[texture].user_owned = GL_TRUE;

    } else {  //CUBE_MAP
        // We only accept all the data already arranged, since we're mapping,
        // no individual planes/copying

        // TODO see pglBufferData
        if (!c->textures.a[texture].user_owned)
            free(c->textures.a[texture].data);

        //TODO spec says INVALID_VALUE, man pages say INVALID_ENUM ?
        PGL_ERR(width != height, GL_INVALID_VALUE);

        c->textures.a[texture].w = width;
        c->textures.a[texture].h = height;
        c->textures.a[texture].d = 1;

        c->textures.a[texture].data = (u8*)data;
        c->textures.a[texture].user_owned = GL_TRUE;

    } //end CUBE_MAP

}

PGLDEF void pglTextureImage3D(GLuint texture, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid* data)
{
    PGL_UNUSED(level);
    PGL_UNUSED(internalformat);

    PGL_ERR(border, GL_INVALID_VALUE);
    PGL_ERR(type != GL_UNSIGNED_BYTE, GL_INVALID_ENUM);
    PGL_ERR(format != GL_RGBA, GL_INVALID_ENUM);

    // data can't be null for user_owned data
    PGL_ERR(!data, GL_INVALID_VALUE);

    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);

    c->textures.a[texture].w = width;
    c->textures.a[texture].h = height;
    c->textures.a[texture].d = depth;

    // TODO see pglBufferData
    if (!c->textures.a[texture].user_owned)
        free(c->textures.a[texture].data);

    c->textures.a[texture].data = (u8*)data;
    c->textures.a[texture].user_owned = GL_TRUE;

}

PGLDEF void pglGetBufferData(GLuint buffer, GLvoid** data)
{
    // why'd you even call it?
    PGL_ERR(!data, GL_INVALID_VALUE);

    // matching error code of binding invalid buffecr
    PGL_ERR((!buffer || buffer >= c->buffers.size || c->buffers.a[buffer].deleted),
            GL_INVALID_OPERATION);

    *data = c->buffers.a[buffer].data;
}

PGLDEF void pglGetTextureData(GLuint texture, GLvoid** data)
{
    // why'd you even call it?
    PGL_ERR(!data, GL_INVALID_VALUE);

    // TODO texture 0?
    PGL_ERR((texture >= c->textures.size || c->textures.a[texture].deleted), GL_INVALID_OPERATION);

    *data = c->textures.a[texture].data;
}

// TODO hmm, void*, or u8*, or GLvoid*?
GLvoid* pglGetBackBuffer(void)
{
    return c->back_buffer.buf;
}

PGLDEF void pglSetBackBuffer(GLvoid* backbuf, GLsizei w, GLsizei h, GLboolean user_owned)
{
    c->back_buffer.w = w;
    c->back_buffer.h = h;
    c->back_buffer.buf = (u8*)backbuf;
    c->back_buffer.lastrow = c->back_buffer.buf + (h-1)*w*sizeof(pix_t);

    c->user_alloced_backbuf = user_owned;
}

PGLDEF void pglSetTexBackBuffer(GLuint texture)
{
    // NOTE, I do not support texture 0
    PGL_ERR((!texture || texture >= c->textures.size || c->textures.a[texture].deleted ||
             c->textures.a[texture].type+GL_TEXTURE_UNBOUND+1 != GL_TEXTURE_2D), GL_INVALID_OPERATION);
    glTexture* t = &c->textures.a[texture];
    pglSetBackBuffer((GLvoid*)t->data, t->w, t->h, t->user_owned);
}


// Not sure where else to put these two functions, they're helper/stopgap
// measures to deal with PGL only supporting RGBA but they're
// also useful functions on their own and not really "extensions"
// so I don't feel right putting them here or giving them a pgl prefix.
//
// Takes an image with GL_UNSIGNED_BYTE channels in
// a format other than packed GL_RGBA and returns it in (tightly packed) GL_RGBA
// (with the same rules as GLSL texture access for filling the other channels).
// See section 3.6.2 page 65 of the OpenGL ES 2.0.25 spec pdf
//
// IOW this creates an image that will give you the same values in the
// shader that you would have gotten had you used the unsupported
// format.  Passing in a GL_RGBA where pitch == w*4 reduces to a single memcpy
//
// If output is NULL, it will allocate the output image for you
// pitch is the length of a row in bytes.
//
// Returns the resulting packed RGBA image
PGLDEF u8* convert_format_to_packed_rgba(u8* output, u8* input, int w, int h, int pitch, GLenum format)
{
    int i, j, size = w*h;
    int rb = pitch;
    u8* out = output;
    if (!out) {
        out = (u8*)PGL_MALLOC(size*4);
    }
    memset(out, 0, size*4);

    u8* p = out;

    if (format == PGL_ONE_ALPHA) {
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = UINT8_MAX;
                p[1] = UINT8_MAX;
                p[2] = UINT8_MAX;
                p[3] = input[i*rb+j];
            }
        }
    } else if (format == GL_ALPHA) {
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[3] = input[i*rb+j];
            }
        }
    } else if (format == GL_LUMINANCE) {
#if PGL_NEON_ENABLED
        pgl_neon_convert_luminance_to_rgba(out, input, w, h, rb);
#else
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j];
                p[1] = input[i*rb+j];
                p[2] = input[i*rb+j];
                p[3] = UINT8_MAX;
            }
        }
#endif
    } else if (format == GL_RED) {
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j];
                p[3] = UINT8_MAX;
            }
        }
    } else if (format == GL_LUMINANCE_ALPHA) {
#if PGL_NEON_ENABLED
        pgl_neon_convert_luminance_alpha_to_rgba(out, input, w, h, rb);
#else
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j*2];
                p[1] = input[i*rb+j*2];
                p[2] = input[i*rb+j*2];
                p[3] = input[i*rb+j*2+1];
            }
        }
#endif
    } else if (format == GL_RG) {
#if PGL_NEON_ENABLED
        pgl_neon_convert_rg_to_rgba(out, input, w, h, rb);
#else
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j*2];
                p[1] = input[i*rb+j*2+1];
                p[3] = UINT8_MAX;
            }
        }
#endif
    } else if (format == GL_RGB) {
#if PGL_NEON_ENABLED
        pgl_neon_convert_rgb_to_rgba(out, input, w, h, rb);
#else
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j*3];
                p[1] = input[i*rb+j*3+1];
                p[2] = input[i*rb+j*3+2];
                p[3] = UINT8_MAX;
            }
        }
#endif
    } else if (format == GL_BGR) {
#if PGL_NEON_ENABLED
        pgl_neon_convert_bgr_to_rgba(out, input, w, h, rb);
#else
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j*3+2];
                p[1] = input[i*rb+j*3+1];
                p[2] = input[i*rb+j*3];
                p[3] = UINT8_MAX;
            }
        }
#endif
    } else if (format == GL_BGRA) {
#if PGL_NEON_ENABLED
        pgl_neon_convert_bgra_to_rgba(out, input, w, h, rb);
#else
        for (i=0; i<h; ++i) {
            for (j=0; j<w; ++j, p+=4) {
                p[0] = input[i*rb+j*4+2];
                p[1] = input[i*rb+j*4+1];
                p[2] = input[i*rb+j*4];
                p[3] = input[i*rb+j*4+3];
            }
        }
#endif
    } else if (format == GL_RGBA) {
        if (pitch == w*4) {
            // Just a plain copy
            memcpy(out, input, w*h*4);
        } else {
            // get rid of row padding
            int bw = w*4;
            for (i=0; i<h; ++i) {
                memcpy(&out[i*bw], &input[i*rb], bw);
            }
        }
    } else {
        puts("Unrecognized or unsupported input format!");
        free(out);
        out = NULL;
    }
    return out;
}

// pass in packed single channel 8 bit image where background=0, foreground=255
// and get a packed 4-channel rgba image using the colors provided
PGLDEF u8* convert_grayscale_to_rgba(u8* input, int size, u32 bg_rgba, u32 text_rgba)
{
    float rb, gb, bb, ab, rt, gt, bt, at;

    u8* tmp = (u8*)&bg_rgba;
    rb = tmp[0];
    gb = tmp[1];
    bb = tmp[2];
    ab = tmp[3];

    tmp = (u8*)&text_rgba;
    rt = tmp[0];
    gt = tmp[1];
    bt = tmp[2];
    at = tmp[3];

    //printf("background = (%f, %f, %f, %f)\ntext = (%f, %f, %f, %f)\n", rb, gb, bb, ab, rt, gt, bt, at);

    u8* color_image = (u8*)PGL_MALLOC(size * 4);
    float t;
    for (int i=0; i<size; ++i) {
        t = (input[i] - 0) / 255.0;
        color_image[i*4] = rt * t + rb * (1 - t);
        color_image[i*4+1] = gt * t + gb * (1 - t);
        color_image[i*4+2] = bt * t + bb * (1 - t);
        color_image[i*4+3] = at * t + ab * (1 - t);
    }


    return color_image;
}


// Just a convenience to have default textures return a specific color, like white here,
// so a textured shading algorithm will look untextured if you bind texture 0
PGLDEF int setup_default_textures(void)
{
    // just 1 white pixel
    // Could make it static and map it so we don't have a 12 tiny allocations
    GLuint image[1] = {
        0xFFFFFFFF
    };
    int w = 1;
    int h = 1;
    int d = 1;
    int frames = 1;
    // If this was called in init_glContext() or immediately after we would know
    // 0 was already bound
    glBindTexture(GL_TEXTURE_1D, 0);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_COMPRESSED_RGBA, w, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    glBindTexture(GL_TEXTURE_2D, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    glBindTexture(GL_TEXTURE_3D, 0);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_COMPRESSED_RGBA, w, h, d, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    glBindTexture(GL_TEXTURE_1D_ARRAY, 0);
    glTexImage2D(GL_TEXTURE_1D_ARRAY, 0, GL_COMPRESSED_RGBA, w, frames, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_COMPRESSED_RGBA, w, h, frames, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    glBindTexture(GL_TEXTURE_RECTANGLE, 0);
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    GLenum cube[6] =
    {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
        GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
    };
    for (int i=0; i<6; i++) {
        glTexImage2D(cube[i], 0, GL_COMPRESSED_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, image);
    }

    return GL_TRUE;
}

PGLDEF void put_pixel(Color color, int x, int y)
{
    //u32* dest = &((u32*)c->back_buffer.lastrow)[-y*c->back_buffer.w + x];
    pix_t* dest = &((pix_t*)c->back_buffer.buf)[y*c->back_buffer.w + x];
    //*dest = (u32)color.a << PGL_ASHIFT | (u32)color.r << PGL_RSHIFT | (u32)color.g << PGL_GSHIFT | (u32)color.b << PGL_BSHIFT;
    *dest = RGBA_TO_PIXEL(color.r, color.g, color.b, color.a);
}

PGLDEF void put_pixel_blend(vec4 src, int x, int y)
{
    //u32* dest = &((u32*)c->back_buffer.lastrow)[-y*c->back_buffer.w + x];
    u32* dest = &((u32*)c->back_buffer.buf)[y*c->back_buffer.w + x];

    //Color dest_color = make_Color((*dest & PGL_RMASK) >> PGL_RSHIFT, (*dest & PGL_GMASK) >> PGL_GSHIFT, (*dest & PGL_BMASK) >> PGL_BSHIFT, (*dest & PGL_AMASK) >> PGL_ASHIFT);
    Color dest_color = PIXEL_TO_COLOR(*dest);

    vec4 dst = Color_to_v4(dest_color);

    // standard alpha blending xyzw = rgba
    vec4 final;
    final.x = src.x * src.w + dst.x * (1.0f - src.w);
    final.y = src.y * src.w + dst.y * (1.0f - src.w);
    final.z = src.z * src.w + dst.z * (1.0f - src.w);
    final.w = src.w + dst.w * (1.0f - src.w);

    Color color = v4_to_Color(final);
    //*dest = (u32)color.a << PGL_ASHIFT | (u32)color.r << PGL_RSHIFT | (u32)color.g << PGL_GSHIFT | (u32)color.b << PGL_BSHIFT;
    *dest = RGBA_TO_PIXEL(color.r, color.g, color.b, color.a);
}

PGLDEF void put_wide_line_simple(Color the_color, float width, float x1, float y1, float x2, float y2)
{
    float tmp;

    //always draw from left to right
    if (x2 < x1) {
        tmp = x1;
        x1 = x2;
        x2 = tmp;
        tmp = y1;
        y1 = y2;
        y2 = tmp;
    }

    //calculate slope and implicit line parameters once
    float m = (y2-y1)/(x2-x1);
    Line line = make_Line(x1, y1, x2, y2);

    vec2 ab = make_v2(line.A, line.B);
    normalize_v2(&ab);

    int x, y;

    float x_min = MAX(0, MIN(x1, x2));
    float x_max = MIN(c->back_buffer.w-1, MAX(x1, x2));
    float y_min = MAX(0, MIN(y1, y2));
    float y_max = MIN(c->back_buffer.h-1, MAX(y1, y2));

    //4 cases based on slope
    if (m <= -1) {           //(-infinite, -1]
        x = x1;
        for (y=y_max; y>=y_min; --y) {
            for (float j=x-width/2; j<x+width/2; j++) {
                put_pixel(the_color, j, y);
            }
            if (line_func(&line, x+0.5f, y-1) < 0)
                x++;
        }
    } else if (m <= 0) {     //(-1, 0]
        y = y1;
        for (x=x_min; x<=x_max; ++x) {
            for (float j=y-width/2; j<y+width/2; j++) {
                put_pixel(the_color, x, j);
            }
            if (line_func(&line, x+1, y-0.5f) > 0)
                y--;
        }
    } else if (m <= 1) {     //(0, 1]
        y = y1;
        for (x=x_min; x<=x_max; ++x) {
            for (float j=y-width/2; j<y+width/2; j++) {
                put_pixel(the_color, x, j);
            }

            //put_pixel(the_color, x, y);
            if (line_func(&line, x+1, y+0.5f) < 0)
                y++;
        }

    } else {                 //(1, +infinite)
        x = x1;
        for (y=y_min; y<=y_max; ++y) {
            for (float j=x-width/2; j<x+width/2; j++) {
                put_pixel(the_color, j, y);
            }
            if (line_func(&line, x+0.5f, y+1) > 0)
                x++;
        }
    }
}

PGLDEF void put_wide_line(Color color1, Color color2, float width, float x1, float y1, float x2, float y2)
{
    vec2 a = { x1, y1 };
    vec2 b = { x2, y2 };
    vec2 tmp;
    Color tmpc;

    if (x2 < x1) {
        tmp = a;
        a = b;
        b = tmp;
        tmpc = color1;
        color1 = color2;
        color2 = tmpc;
    }

    vec4 c1 = Color_to_v4(color1);
    vec4 c2 = Color_to_v4(color2);

    // need half the width to calculate
    width /= 2.0f;

    float m = (y2-y1)/(x2-x1);
    Line line = make_Line(x1, y1, x2, y2);
    normalize_line(&line);
    vec2 c;

    vec2 ab = sub_v2s(b, a);
    vec2 ac;

    float dot_abab = dot_v2s(ab, ab);

    float x_min = floor(a.x - width) + 0.5f;
    float x_max = floor(b.x + width) + 0.5f;
    float y_min, y_max;
    if (m <= 0) {
        y_min = floor(b.y - width) + 0.5f;
        y_max = floor(a.y + width) + 0.5f;
    } else {
        y_min = floor(a.y - width) + 0.5f;
        y_max = floor(b.y + width) + 0.5f;
    }

    float x, y, e, dist, t;
    float w2 = width*width;
    //int last = 1;
    Color out_c;

    for (y = y_min; y <= y_max; ++y) {
        c.y = y;
        for (x = x_min; x <= x_max; x++) {
            // TODO optimize
            c.x = x;
            ac = sub_v2s(c, a);
            e = dot_v2s(ac, ab);
            
            // c lies past the ends of the segment ab
            if (e <= 0.0f || e >= dot_abab) {
                continue;
            }

            // can do this because we normalized the line equation
            // TODO square or fabsf?
            dist = line_func(&line, c.x, c.y);
            if (dist*dist < w2) {
                t = e / dot_abab;
                out_c = v4_to_Color(mixf_v4(c1, c2, t));
                put_pixel(out_c, x, y);
            }
        }
    }
}

//Should I have it take a glFramebuffer as paramater?
PGLDEF void put_line(Color the_color, float x1, float y1, float x2, float y2)
{
    float tmp;

    //always draw from left to right
    if (x2 < x1) {
        tmp = x1;
        x1 = x2;
        x2 = tmp;
        tmp = y1;
        y1 = y2;
        y2 = tmp;
    }

    //calculate slope and implicit line parameters once
    float m = (y2-y1)/(x2-x1);
    Line line = make_Line(x1, y1, x2, y2);

    int x, y;

    float x_min = MAX(0, MIN(x1, x2));
    float x_max = MIN(c->back_buffer.w-1, MAX(x1, x2));
    float y_min = MAX(0, MIN(y1, y2));
    float y_max = MIN(c->back_buffer.h-1, MAX(y1, y2));

    x_min = floorf(x_min) + 0.5f;
    x_max = floorf(x_max) + 0.5f;
    y_min = floorf(y_min) + 0.5f;
    y_max = floorf(y_max) + 0.5f;

    //4 cases based on slope
    if (m <= -1) {           //(-infinite, -1]
        x = x_min;
        for (y=y_max; y>=y_min; --y) {
            put_pixel(the_color, x, y);
            if (line_func(&line, x+0.5f, y-1) < 0)
                x++;
        }
    } else if (m <= 0) {     //(-1, 0]
        y = y_max;
        for (x=x_min; x<=x_max; ++x) {
            put_pixel(the_color, x, y);
            if (line_func(&line, x+1, y-0.5f) > 0)
                y--;
        }
    } else if (m <= 1) {     //(0, 1]
        y = y_min;
        for (x=x_min; x<=x_max; ++x) {
            put_pixel(the_color, x, y);
            if (line_func(&line, x+1, y+0.5f) < 0)
                y++;
        }

    } else {                 //(1, +infinite)
        x = x_min;
        for (y=y_min; y<=y_max; ++y) {
            put_pixel(the_color, x, y);
            if (line_func(&line, x+0.5f, y+1) > 0)
                x++;
        }
    }
}


// can't think of a better/cleaner way to do this than these lines
#define CLIP_TRIANGLE() \
    do { \
    x_min = MIN(p1.x, p2.x); \
    x_max = MAX(p1.x, p2.x); \
    y_min = MIN(p1.y, p2.y); \
    y_max = MAX(p1.y, p2.y); \
 \
    x_min = MIN(p3.x, x_min); \
    x_max = MAX(p3.x, x_max); \
    y_min = MIN(p3.y, y_min); \
    y_max = MAX(p3.y, y_max); \
 \
    x_min = MAX(c->lx, x_min); \
    x_max = MIN(c->ux, x_max); \
    y_min = MAX(c->ly, y_min); \
    y_max = MIN(c->uy, y_max); \
    } while (0)

#define MAKE_IMPLICIT_LINES() \
    do { \
    l12 = make_Line(p1.x, p1.y, p2.x, p2.y); \
    l23 = make_Line(p2.x, p2.y, p3.x, p3.y); \
    l31 = make_Line(p3.x, p3.y, p1.x, p1.y); \
    } while (0)

#define ANY_COLORS_NOT_WHITE(c) \
    (c0.r != 255 || c1.r != 255 || c2.r != 255 || \
    c0.g != 255 || c1.g != 255 || c2.g != 255 || \
    c0.b != 255 || c1.b != 255 || c2.b != 255)

PGLDEF void put_triangle_uniform(vec4 color, vec2 p1, vec2 p2, vec2 p3)
{
    float x_min,x_max,y_min,y_max;
    Line l12, l23, l31;
    float alpha, beta, gamma;

    CLIP_TRIANGLE();
    MAKE_IMPLICIT_LINES();

    x_min = floorf(x_min) + 0.5f;
    y_min = floorf(y_min) + 0.5f;

    for (float y=y_min; y<y_max; ++y) {
        for (float x=x_min; x<x_max; ++x) {
            gamma = line_func(&l12, x, y)/line_func(&l12, p3.x, p3.y);
            beta = line_func(&l31, x, y)/line_func(&l31, p2.x, p2.y);
            alpha = 1 - beta - gamma;

            if (alpha >= 0 && beta >= 0 && gamma >= 0) {
                //if it's on the edge (==0), draw if the opposite vertex is on the same side as arbitrary point -1, -1
                //this is a deterministic way of choosing which triangle gets a pixel for trinagles that share
                //edges
                if ((alpha > 0 || line_func(&l23, p1.x, p1.y) * line_func(&l23, -1, -1) > 0) &&
                    (beta >  0 || line_func(&l31, p2.x, p2.y) * line_func(&l31, -1, -1) > 0) &&
                    (gamma > 0 || line_func(&l12, p3.x, p3.y) * line_func(&l12, -1, -1) > 0)) {
                    // blend
                    put_pixel_blend(color, x, y);
                    //put_pixel(color, x, y);
                }
            }
        }
    }
}

PGLDEF void put_triangle(Color c1, Color c2, Color c3, vec2 p1, vec2 p2, vec2 p3)
{
    float x_min,x_max,y_min,y_max;
    Line l12, l23, l31;
    float alpha, beta, gamma;
    Color col;
    col.a = 255; // hmm

    CLIP_TRIANGLE();
    MAKE_IMPLICIT_LINES();

    x_min = floorf(x_min) + 0.5f;
    y_min = floorf(y_min) + 0.5f;

    for (float y=y_min; y<y_max; ++y) {
        for (float x=x_min; x<x_max; ++x) {
            gamma = line_func(&l12, x, y)/line_func(&l12, p3.x, p3.y);
            beta = line_func(&l31, x, y)/line_func(&l31, p2.x, p2.y);
            alpha = 1 - beta - gamma;

            if (alpha >= 0 && beta >= 0 && gamma >= 0) {
                //if it's on the edge (==0), draw if the opposite vertex is on the same side as arbitrary point -1, -1
                //this is a deterministic way of choosing which triangle gets a pixel for trinagles that share
                //edges
                if ((alpha > 0 || line_func(&l23, p1.x, p1.y) * line_func(&l23, -1, -1) > 0) &&
                    (beta >  0 || line_func(&l31, p2.x, p2.y) * line_func(&l31, -1, -1) > 0) &&
                    (gamma > 0 || line_func(&l12, p3.x, p3.y) * line_func(&l12, -1, -1) > 0)) {
                    //calculate interoplation here
                    col.r = alpha*c1.r + beta*c2.r + gamma*c3.r;
                    col.g = alpha*c1.g + beta*c2.g + gamma*c3.g;
                    col.b = alpha*c1.b + beta*c2.b + gamma*c3.b;
                    //col.a = alpha*c1.a + beta*c2.a + gamma*c3.a;
                    //put_pixel_blend(c, x, y);
                    put_pixel(col, x, y);
                }
            }
        }
    }
}

PGLDEF void put_triangle_tex(int tex, vec2 uv1, vec2 uv2, vec2 uv3, vec2 p1, vec2 p2, vec2 p3)
{
    float x_min,x_max,y_min,y_max;
    Line l12, l23, l31;
    float alpha, beta, gamma;

    CLIP_TRIANGLE();
    MAKE_IMPLICIT_LINES();

#if 0
    print_v2(p1, " p1\n");
    print_v2(p2, " p2\n");
    print_v2(p3, " p3\n");
    print_v2(uv1, " uv1\n");
    print_v2(uv2, " uv2\n");
    print_v2(uv3, " uv3\n");
#endif

    x_min = floorf(x_min) + 0.5f;
    y_min = floorf(y_min) + 0.5f;
    vec2 uv;

    for (float y=y_min; y<y_max; ++y) {
        for (float x=x_min; x<x_max; ++x) {
            gamma = line_func(&l12, x, y)/line_func(&l12, p3.x, p3.y);
            beta = line_func(&l31, x, y)/line_func(&l31, p2.x, p2.y);
            alpha = 1 - beta - gamma;

            if (alpha >= 0 && beta >= 0 && gamma >= 0) {
                //if it's on the edge (==0), draw if the opposite vertex is on the same side as arbitrary point -1, -1
                //this is a deterministic way of choosing which triangle gets a pixel for trinagles that share
                //edges
                if ((alpha > 0 || line_func(&l23, p1.x, p1.y) * line_func(&l23, -1, -1) > 0) &&
                    (beta >  0 || line_func(&l31, p2.x, p2.y) * line_func(&l31, -1, -1) > 0) &&
                    (gamma > 0 || line_func(&l12, p3.x, p3.y) * line_func(&l12, -1, -1) > 0)) {
                    //calculate interoplation here
                    uv = add_v2s(scale_v2(uv1, alpha), scale_v2(uv2, beta));
                    uv = add_v2s(uv, scale_v2(uv3, gamma));
                    put_pixel_blend(texture2D(tex, uv.x, uv.y), x, y);
                }
            }
        }
    }
}

PGLDEF void put_triangle_tex_modulate(int tex, vec2 uv1, vec2 uv2, vec2 uv3, vec2 p1, vec2 p2, vec2 p3, Color c1, Color c2, Color c3)
{
    float x_min,x_max,y_min,y_max;
    Line l12, l23, l31;
    float alpha, beta, gamma;
    Color col;

    CLIP_TRIANGLE();
    MAKE_IMPLICIT_LINES();

#if 0
    print_v2(p1, " p1\n");
    print_v2(p2, " p2\n");
    print_v2(p3, " p3\n");
    print_v2(uv1, " uv1\n");
    print_v2(uv2, " uv2\n");
    print_v2(uv3, " uv3\n");
    print_Color(c1, " c1\n");
    print_Color(c2, " c2\n");
    print_Color(c3, " c3\n");
#endif

    x_min = floorf(x_min) + 0.5f;
    y_min = floorf(y_min) + 0.5f;
    vec2 uv;

    for (float y=y_min; y<y_max; ++y) {
        for (float x=x_min; x<x_max; ++x) {
            gamma = line_func(&l12, x, y)/line_func(&l12, p3.x, p3.y);
            beta = line_func(&l31, x, y)/line_func(&l31, p2.x, p2.y);
            alpha = 1 - beta - gamma;

            if (alpha >= 0 && beta >= 0 && gamma >= 0) {
                //if it's on the edge (==0), draw if the opposite vertex is on the same side as arbitrary point -1, -1
                //this is a deterministic way of choosing which triangle gets a pixel for trinagles that share
                //edges
                if ((alpha > 0 || line_func(&l23, p1.x, p1.y) * line_func(&l23, -1, -1) > 0) &&
                    (beta >  0 || line_func(&l31, p2.x, p2.y) * line_func(&l31, -1, -1) > 0) &&
                    (gamma > 0 || line_func(&l12, p3.x, p3.y) * line_func(&l12, -1, -1) > 0)) {
                    //calculate interoplation here
                    uv = add_v2s(scale_v2(uv1, alpha), scale_v2(uv2, beta));
                    uv = add_v2s(uv, scale_v2(uv3, gamma));

                    col.r = alpha*c1.r + beta*c2.r + gamma*c3.r;
                    col.g = alpha*c1.g + beta*c2.g + gamma*c3.g;
                    col.b = alpha*c1.b + beta*c2.b + gamma*c3.b;
                    col.a = alpha*c1.a + beta*c2.a + gamma*c3.a;
                    vec4 cv = Color_to_v4(col);
                    vec4 texcolor = texture2D(tex, uv.x, uv.y);
                    
                    put_pixel_blend(mult_v4s(cv, texcolor), x, y);
                }
            }
        }
    }
}

#define COLOR_EQ(c1, c2) ((c1).r == (c2).r && (c1).g == (c2).g && (c1).b == (c2).b && (c1).a == (c2).a)


// TODO Color* or vec4*? float* for xy/uv or vec2*?
PGLDEF void pgl_draw_geometry_raw(int tex, const float* xy, int xy_stride, const Color* color, int color_stride, const float* uv, int uv_stride, int n_verts, const void* indices, int n_indices, int sz_indices)
{
    int i,j;
    float* x;
    float* u;
    int count = indices ? n_indices : n_verts;

    // TODO make PGL_INVALID_VALUE et all?
    PGL_ERR(!xy, GL_INVALID_VALUE);

    // Matching SDL_RenderGeometryRaw but I feel like they should be able to pass
    // NULL and just use the texture
    PGL_ERR(!color, GL_INVALID_VALUE);


    PGL_ERR(count % 3, GL_INVALID_VALUE);
    PGL_ERR(!(sz_indices==1 || sz_indices==2 || sz_indices==4), GL_INVALID_VALUE);

    if (n_verts < 3) return;

    PGL_ASSERT((get_max_vertices() * GL_MAX_VERTEX_OUTPUT_COMPONENTS * sizeof(float))/sizeof(pgl_copy_data) >= (size_t)count);
    // Allow default texture 0?  many implementations return black (0,0,0,1) when sampling
    // tex 0
    if (tex > 0) {
        PGL_ERR(!uv, GL_INVALID_VALUE);

        PGL_ERR((tex >= c->textures.size || c->textures.a[tex].deleted), GL_INVALID_VALUE);

        PGL_ERR(c->textures.a[tex].type != GL_TEXTURE_2D-(GL_TEXTURE_UNBOUND+1), GL_INVALID_OPERATION);

        pgl_copy_data* verts = (pgl_copy_data*)&c->vs_output.output_buf[0];
        for (i=0; i<count; ++i) {
            if (sz_indices == 1) j = ((GLubyte*)indices)[i];
            else if (sz_indices == 2) j = ((GLushort*)indices)[i];
            else if (sz_indices == 4) j = ((GLuint*)indices)[i];
            else j = i;

            x = (float*)((u8*)xy + j*xy_stride);
            u = (float*)((u8*)uv + j*uv_stride);

            verts[i].c = *(Color*)((u8*)color + j*color_stride);

            // TODO convert to ints here for efficiency or leave floats for
            // flexibility/subpixel accuracy?
            verts[i].src.x = u[0];
            verts[i].src.y = u[1];

            // scale?
            verts[i].dst.x = x[0];
            verts[i].dst.y = x[1];
        }

        vec4 tex_color;
        pgl_copy_data* p = verts;
        int is_uniform = GL_FALSE;
        int has_modulation = GL_FALSE;
        int tex_uniform = GL_FALSE;
        for (i=0; i<count; i+=3, p+=3) {
            is_uniform = (COLOR_EQ(p[0].c, p[1].c) && COLOR_EQ(p[1].c, p[2].c));
            if (is_uniform) {
                has_modulation = (p[0].c.r != 255 || p[0].c.g != 255 || p[0].c.b != 255 || p[0].c.a != 255);
            } else {
                has_modulation = GL_TRUE;
            }
            tex_uniform = (equal_v2s(p[0].src, p[1].src) && equal_v2s(p[1].src, p[2].src));
            if (tex_uniform) tex_color = texture2D(tex, p[0].src.x, p[0].src.y);

            if (has_modulation) {
                if (is_uniform) {
                    if (tex_uniform) {
                        // uniform color triangle, likely uniform color rect
                        vec4 color = mult_v4s(tex_color, Color_to_v4(p[0].c));
                        put_triangle_uniform(color, p[0].dst, p[1].dst, p[2].dst);
                    } else {
                        // need another variant that takes a single color so only
                        // interpolates uv
                        put_triangle_tex_modulate(tex, p[0].src, p[1].src, p[2].src, p[0].dst, p[1].dst, p[2].dst, p[0].c, p[1].c, p[2].c);
                    }
                } else {
                    put_triangle_tex_modulate(tex, p[0].src, p[1].src, p[2].src, p[0].dst, p[1].dst, p[2].dst, p[0].c, p[1].c, p[2].c);
                }
            } else {
                if (tex_uniform) {
                    // uniform color triangle, likely uniform color rect
                    put_triangle_uniform(tex_color, p[0].dst, p[1].dst, p[2].dst);
                } else {
                    put_triangle_tex(tex, p[0].src, p[1].src, p[2].src, p[0].dst, p[1].dst, p[2].dst);
                }
            }
        }
    } else {
        pgl_fill_data* verts = (pgl_fill_data*)&c->vs_output.output_buf[0];
        for (i=0; i<count; ++i) {
            if (sz_indices == 1) j = ((GLubyte*)indices)[i];
            else if (sz_indices == 2) j = ((GLushort*)indices)[i];
            else if (sz_indices == 4) j = ((GLuint*)indices)[i];
            else j = i;

            x = (float*)((u8*)xy + j*xy_stride);
            verts[i].c = *(Color*)((u8*)color + j*color_stride);

            // scale?
            verts[i].dst.x = x[0];
            verts[i].dst.y = x[1];
        }

        pgl_fill_data* p = verts;
        for (i=0; i<count; i+=3, p+=3) {
            put_triangle(p[0].c, p[1].c, p[2].c, p[0].dst, p[1].dst, p[2].dst);
        }
    }
}



#define plot(X,Y,D) do{ c.w = (D); put_pixel_blend(c, X, Y); } while (0)

#define ipart_(X) ((int)(X))
#define round_(X) ((int)(((float)(X))+0.5f))
#define fpart_(X) (((float)(X))-(float)ipart_(X))
#define rfpart_(X) (1.0f-fpart_(X))

#define swap_(a, b) do{ __typeof__(a) tmp;  tmp = a; a = b; b = tmp; } while(0)
PGLDEF void put_aa_line(vec4 c, float x1, float y1, float x2, float y2)
{
    float dx = x2 - x1;
    float dy = y2 - y1;
    if (fabs(dx) > fabs(dy)) {
        if (x2 < x1) {
            swap_(x1, x2);
            swap_(y1, y2);
        }
        float gradient = dy / dx;
        float xend = round_(x1);
        float yend = y1 + gradient*(xend - x1);
        float xgap = rfpart_(x1 + 0.5);
        int xpxl1 = xend;
        int ypxl1 = ipart_(yend);
        plot(xpxl1, ypxl1, rfpart_(yend)*xgap);
        plot(xpxl1, ypxl1+1, fpart_(yend)*xgap);
        printf("xgap = %f\n", xgap);
        printf("%f %f\n", rfpart_(yend), fpart_(yend));
        printf("%f %f\n", rfpart_(yend)*xgap, fpart_(yend)*xgap);
        float intery = yend + gradient;

        xend = round_(x2);
        yend = y2 + gradient*(xend - x2);
        xgap = fpart_(x2+0.5);
        int xpxl2 = xend;
        int ypxl2 = ipart_(yend);
        plot(xpxl2, ypxl2, rfpart_(yend) * xgap);
        plot(xpxl2, ypxl2 + 1, fpart_(yend) * xgap);

        int x;
        for(x=xpxl1+1; x < xpxl2; x++) {
            plot(x, ipart_(intery), rfpart_(intery));
            plot(x, ipart_(intery) + 1, fpart_(intery));
            intery += gradient;
        }
    } else {
        if ( y2 < y1 ) {
            swap_(x1, x2);
            swap_(y1, y2);
        }
        float gradient = dx / dy;
        float yend = round_(y1);
        float xend = x1 + gradient*(yend - y1);
        float ygap = rfpart_(y1 + 0.5);
        int ypxl1 = yend;
        int xpxl1 = ipart_(xend);
        plot(xpxl1, ypxl1, rfpart_(xend)*ygap);
        plot(xpxl1 + 1, ypxl1, fpart_(xend)*ygap);
        float interx = xend + gradient;

        yend = round_(y2);
        xend = x2 + gradient*(yend - y2);
        ygap = fpart_(y2+0.5);
        int ypxl2 = yend;
        int xpxl2 = ipart_(xend);
        plot(xpxl2, ypxl2, rfpart_(xend) * ygap);
        plot(xpxl2 + 1, ypxl2, fpart_(xend) * ygap);

        int y;
        for(y=ypxl1+1; y < ypxl2; y++) {
            plot(ipart_(interx), y, rfpart_(interx));
            plot(ipart_(interx) + 1, y, fpart_(interx));
            interx += gradient;
        }
    }
}


PGLDEF void put_aa_line_interp(vec4 c1, vec4 c2, float x1, float y1, float x2, float y2)
{
    vec4 c;
    float t;

    float dx = x2 - x1;
    float dy = y2 - y1;

    if (fabs(dx) > fabs(dy)) {
        if (x2 < x1) {
            swap_(x1, x2);
            swap_(y1, y2);
            swap_(c1, c2);
        }

        vec2 p1 = { x1, y1 }, p2 = { x2, y2 };
        vec2 pr, sub_p2p1 = sub_v2s(p2, p1);
        float line_length_squared = len_v2(sub_p2p1);
        line_length_squared *= line_length_squared;

        c = c1;

        float gradient = dy / dx;
        float xend = round_(x1);
        float yend = y1 + gradient*(xend - x1);
        float xgap = rfpart_(x1 + 0.5);
        int xpxl1 = xend;
        int ypxl1 = ipart_(yend);
        plot(xpxl1, ypxl1, rfpart_(yend)*xgap);
        plot(xpxl1, ypxl1+1, fpart_(yend)*xgap);
        printf("xgap = %f\n", xgap);
        printf("%f %f\n", rfpart_(yend), fpart_(yend));
        printf("%f %f\n", rfpart_(yend)*xgap, fpart_(yend)*xgap);
        float intery = yend + gradient;

        c = c2;
        xend = round_(x2);
        yend = y2 + gradient*(xend - x2);
        xgap = fpart_(x2+0.5);
        int xpxl2 = xend;
        int ypxl2 = ipart_(yend);
        plot(xpxl2, ypxl2, rfpart_(yend) * xgap);
        plot(xpxl2, ypxl2 + 1, fpart_(yend) * xgap);

        int x;
        for(x=xpxl1+1; x < xpxl2; x++) {
            pr.x = x;
            pr.y = intery;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            c = mixf_v4(c1, c2, t);

            plot(x, ipart_(intery), rfpart_(intery));
            plot(x, ipart_(intery) + 1, fpart_(intery));
            intery += gradient;
        }
    } else {
        if ( y2 < y1 ) {
            swap_(x1, x2);
            swap_(y1, y2);
            swap_(c1, c2);
        }

        vec2 p1 = { x1, y1 }, p2 = { x2, y2 };
        vec2 pr, sub_p2p1 = sub_v2s(p2, p1);
        float line_length_squared = len_v2(sub_p2p1);
        line_length_squared *= line_length_squared;

        c = c1;

        float gradient = dx / dy;
        float yend = round_(y1);
        float xend = x1 + gradient*(yend - y1);
        float ygap = rfpart_(y1 + 0.5);
        int ypxl1 = yend;
        int xpxl1 = ipart_(xend);
        plot(xpxl1, ypxl1, rfpart_(xend)*ygap);
        plot(xpxl1 + 1, ypxl1, fpart_(xend)*ygap);
        float interx = xend + gradient;


        c = c2;
        yend = round_(y2);
        xend = x2 + gradient*(yend - y2);
        ygap = fpart_(y2+0.5);
        int ypxl2 = yend;
        int xpxl2 = ipart_(xend);
        plot(xpxl2, ypxl2, rfpart_(xend) * ygap);
        plot(xpxl2 + 1, ypxl2, fpart_(xend) * ygap);

        int y;
        for(y=ypxl1+1; y < ypxl2; y++) {
            pr.x = interx;
            pr.y = y;
            t = dot_v2s(sub_v2s(pr, p1), sub_p2p1) / line_length_squared;
            c = mixf_v4(c1, c2, t);

            plot(ipart_(interx), y, rfpart_(interx));
            plot(ipart_(interx) + 1, y, fpart_(interx));
            interx += gradient;
        }
    }
}


#undef swap_
#undef plot
#undef ipart_
#undef fpart_
#undef round_
#undef rfpart_



// Collection of standard shaders based on
// https://github.com/rswinkle/oglsuperbible5/blob/master/Src/GLTools/src/GLShaderManager.cpp
//
// Meant to ease the transition from old fixed function a little.  You might be able
// to get away without writing any new shaders, but you'll still need to use uniforms
// and enable attributes etc. things unless you write a full compatibility layer

// Identity Shader, no transformation, uniform color
static void pgl_identity_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(vs_output);
    PGL_UNUSED(uniforms);
    builtins->gl_Position = vertex_attribs[PGL_ATTR_VERT];
}

static void pgl_identity_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(fs_input);
    builtins->gl_FragColor = ((pgl_uniforms*)uniforms)->color;
}

// Flat Shader, Applies the uniform model view matrix transformation, uniform color
static void flat_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(vs_output);
    builtins->gl_Position = mult_m4_v4(*((mat4*)uniforms), vertex_attribs[PGL_ATTR_VERT]);
}

// flat_fs is identical to pgl_identity_fs

// Shaded Shader, interpolates per vertex colors
static void pgl_shaded_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    ((vec4*)vs_output)[0] = vertex_attribs[PGL_ATTR_COLOR]; //color

    builtins->gl_Position = mult_m4_v4(*((mat4*)uniforms), vertex_attribs[PGL_ATTR_VERT]);
}

static void pgl_shaded_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    PGL_UNUSED(uniforms);
    builtins->gl_FragColor = ((vec4*)fs_input)[0];
}

// Default Light Shader
// simple diffuse directional light, vertex based shading
// uniforms:
// mat4 mvp_mat
// mat3 normal_mat
// vec4 color
//
// attributes:
// vec4 vertex
// vec3 normal
static void pgl_dflt_light_vs(float* vs_output, vec4* v_attrs, Shader_Builtins* builtins, void* uniforms)
{
    pgl_uniforms* u = (pgl_uniforms*)uniforms;

    vec3 norm = norm_v3(mult_m3_v3(u->normal_mat, *(vec3*)&v_attrs[PGL_ATTR_NORMAL]));

    vec3 light_dir = { 0.0f, 0.0f, 1.0f };
    float tmp = dot_v3s(norm, light_dir);
    float fdot = MAX(0.0f, tmp);

    vec4 c = u->color;

    // outgoing fragcolor to be interpolated
    ((vec4*)vs_output)[0] = make_v4(c.x*fdot, c.y*fdot, c.z*fdot, c.w);

    builtins->gl_Position = mult_m4_v4(u->mvp_mat, v_attrs[PGL_ATTR_VERT]);
}

// default_light_fs is the same as pgl_shaded_fs

// Point Light Diff Shader
// point light, diffuse lighting only
// uniforms:
// mat4 mvp_mat
// mat4 mv_mat
// mat3 normal_mat
// vec4 color
// vec3 light_pos
//
// attributes:
// vec4 vertex
// vec3 normal
static void pgl_pnt_light_diff_vs(float* vs_output, vec4* v_attrs, Shader_Builtins* builtins, void* uniforms)
{
    pgl_uniforms* u = (pgl_uniforms*)uniforms;

    vec3 norm = norm_v3(mult_m3_v3(u->normal_mat, *(vec3*)&v_attrs[PGL_ATTR_NORMAL]));

    vec4 ec_pos = mult_m4_v4(u->mv_mat, v_attrs[PGL_ATTR_VERT]);
    vec3 ec_pos3 = v4_to_v3h(ec_pos);

    vec3 light_dir = norm_v3(sub_v3s(u->light_pos, ec_pos3));

    float tmp = dot_v3s(norm, light_dir);
    float fdot = MAX(0.0f, tmp);

    vec4 c = u->color;

    // outgoing fragcolor to be interpolated
    ((vec4*)vs_output)[0] = make_v4(c.x*fdot, c.y*fdot, c.z*fdot, c.w);

    builtins->gl_Position = mult_m4_v4(u->mvp_mat, v_attrs[PGL_ATTR_VERT]);
}

// point_light_diff_fs is the same as pgl_shaded_fs


// Texture Replace Shader
// Just paste the texture on the triangles
// uniforms:
// mat4 mvp_mat
// GLuint tex0
//
// attributes:
// vec4 vertex
// vec2 texcoord0
static void pgl_tex_rplc_vs(float* vs_output, vec4* v_attrs, Shader_Builtins* builtins, void* uniforms)
{
    pgl_uniforms* u = (pgl_uniforms*)uniforms;

    ((vec2*)vs_output)[0] = *(vec2*)&v_attrs[PGL_ATTR_TEXCOORD0]; //tex_coords

    builtins->gl_Position = mult_m4_v4(u->mvp_mat, v_attrs[PGL_ATTR_VERT]);

}

static void pgl_tex_rplc_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    vec2 tex_coords = ((vec2*)fs_input)[0];
    GLuint tex = ((pgl_uniforms*)uniforms)->tex0;

    builtins->gl_FragColor = texture2D(tex, tex_coords.x, tex_coords.y);
}



// Texture Rect Replace Shader
// Just paste the texture on the triangles except using rect textures
// uniforms:
// mat4 mvp_mat
// GLuint tex0
//
// attributes:
// vec4 vertex
// vec2 texcoord0

// texture_rect_rplc_vs is the same as pgl_tex_rplc_vs
static void pgl_tex_rect_rplc_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    vec2 tex_coords = ((vec2*)fs_input)[0];
    GLuint tex = ((pgl_uniforms*)uniforms)->tex0;

    builtins->gl_FragColor = texture_rect(tex, tex_coords.x, tex_coords.y);
}


// Texture Modulate Shader
// Paste texture on triangles but multiplied by a uniform color
// uniforms:
// mat4 mvp_mat
// GLuint tex0
//
// attributes:
// vec4 vertex
// vec2 texcoord0

// texture_modulate_vs is the same as pgl_tex_rplc_vs

static void pgl_tex_modulate_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    pgl_uniforms* u = (pgl_uniforms*)uniforms;

    vec2 tex_coords = ((vec2*)fs_input)[0];

    GLuint tex = u->tex0;

    builtins->gl_FragColor = mult_v4s(u->color, texture2D(tex, tex_coords.x, tex_coords.y));
}


// Texture Point Light Diff
// point light, diffuse only with texture
// uniforms:
// mat4 mvp_mat
// mat4 mv_mat
// mat3 normal_mat
// vec4 color
// vec3 light_pos
//
// attributes:
// vec4 vertex
// vec3 normal
static void pgl_tex_pnt_light_diff_vs(float* vs_output, vec4* v_attrs, Shader_Builtins* builtins, void* uniforms)
{
    pgl_uniforms* u = (pgl_uniforms*)uniforms;

    vec3 norm = norm_v3(mult_m3_v3(u->normal_mat, *(vec3*)&v_attrs[PGL_ATTR_NORMAL]));

    vec4 ec_pos = mult_m4_v4(u->mv_mat, v_attrs[PGL_ATTR_VERT]);
    vec3 ec_pos3 = v4_to_v3h(ec_pos);

    vec3 light_dir = norm_v3(sub_v3s(u->light_pos, ec_pos3));

    float tmp = dot_v3s(norm, light_dir);
    float fdot = MAX(0.0f, tmp);

    vec4 c = u->color;

    // outgoing fragcolor to be interpolated
    ((vec4*)vs_output)[0] = make_v4(c.x*fdot, c.y*fdot, c.z*fdot, c.w);
    // fragcolor takes up 4 floats, ie 2*sizeof(vec2)
    ((vec2*)vs_output)[2] =  *(vec2*)&v_attrs[PGL_ATTR_TEXCOORD0];

    builtins->gl_Position = mult_m4_v4(u->mvp_mat, v_attrs[PGL_ATTR_VERT]);
}


static void pgl_tex_pnt_light_diff_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    pgl_uniforms* u = (pgl_uniforms*)uniforms;

    vec2 tex_coords = ((vec2*)fs_input)[2];

    GLuint tex = u->tex0;

    builtins->gl_FragColor = mult_v4s(((vec4*)fs_input)[0], texture2D(tex, tex_coords.x, tex_coords.y));
}


PGLDEF void pgl_init_std_shaders(GLuint programs[PGL_NUM_SHADERS])
{
    pgl_prog_info std_shaders[PGL_NUM_SHADERS] =
    {
        { pgl_identity_vs, pgl_identity_fs, 0, {0}, GL_FALSE },
        { flat_vs, pgl_identity_fs, 0, {0}, GL_FALSE },
        { pgl_shaded_vs, pgl_shaded_fs, 4, {PGL_SMOOTH4}, GL_FALSE },
        { pgl_dflt_light_vs, pgl_shaded_fs, 4, {PGL_SMOOTH4}, GL_FALSE },
        { pgl_pnt_light_diff_vs, pgl_shaded_fs, 4, {PGL_SMOOTH4}, GL_FALSE },
        { pgl_tex_rplc_vs, pgl_tex_rplc_fs, 2, {PGL_SMOOTH2}, GL_FALSE },
        { pgl_tex_rplc_vs, pgl_tex_modulate_fs, 2, {PGL_SMOOTH2}, GL_FALSE },
        { pgl_tex_pnt_light_diff_vs, pgl_tex_pnt_light_diff_fs, 6, {PGL_SMOOTH4, PGL_SMOOTH2}, GL_FALSE },


        { pgl_tex_rplc_vs, pgl_tex_rect_rplc_fs, 2, {PGL_SMOOTH2}, GL_FALSE }
    };

    for (int i=0; i<PGL_NUM_SHADERS; i++) {
        pgl_prog_info* p = &std_shaders[i];
        programs[i] = pglCreateProgram(p->vs, p->fs, p->vs_out_sz, p->interp, p->uses_fragdepth_or_discard);
    }
}
