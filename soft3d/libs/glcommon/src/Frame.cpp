
#include "Frame.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Helper functions
static inline vec3 cross(const vec3& a, const vec3& b) {
    return vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

static inline float dot(const vec3& a, const vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline float length(const vec3& v) {
    return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
}

static inline vec3 normalize_vec3(const vec3& v) {
    float len = length(v);
    if (len > 0.0001f) {
        return vec3(v.x / len, v.y / len, v.z / len);
    }
    return v;
}

static inline vec3 operator+(const vec3& a, const vec3& b) {
    return vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline vec3 operator-(const vec3& a, const vec3& b) {
    return vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline vec3 operator-(const vec3& v) {
    return vec3(-v.x, -v.y, -v.z);
}

static inline vec3 operator*(const vec3& v, float s) {
    return vec3(v.x * s, v.y * s, v.z * s);
}

static inline vec3 operator*(float s, const vec3& v) {
    return v * s;
}

static inline vec3& operator+=(vec3& a, const vec3& b) {
    a.x += b.x; a.y += b.y; a.z += b.z;
    return a;
}

static inline vec3& operator-=(vec3& a, const vec3& b) {
    a.x -= b.x; a.y -= b.y; a.z -= b.z;
    return a;
}

static inline vec3& operator*=(vec3& v, float s) {
    v.x *= s; v.y *= s; v.z *= s;
    return v;
}

// Rotation matrix functions
static void load_rotation_mat3(float* mat, const vec3& axis, float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    float t = 1.0f - c;
    
    vec3 n = normalize_vec3(axis);
    float x = n.x, y = n.y, z = n.z;
    
    mat[0] = t*x*x + c;     mat[1] = t*x*y + s*z;   mat[2] = t*x*z - s*y;
    mat[3] = t*x*y - s*z;   mat[4] = t*y*y + c;     mat[5] = t*y*z + s*x;
    mat[6] = t*x*z + s*y;   mat[7] = t*y*z - s*x;   mat[8] = t*z*z + c;
}

static vec3 mat3_mult_vec3(const float* mat, const vec3& v) {
    return vec3(
        mat[0] * v.x + mat[1] * v.y + mat[2] * v.z,
        mat[3] * v.x + mat[4] * v.y + mat[5] * v.z,
        mat[6] * v.x + mat[7] * v.y + mat[8] * v.z
    );
}

Frame::Frame(bool camera, vec3 origin_) {
    origin = origin_;
    
    up.x = 0.0f; up.y = 1.0f; up.z = 0.0f;
    
    if (!camera) {
        forward.x = 0.0f; forward.y = 0.0f; forward.z = 1.0f;
    } else {
        forward.x = 0.0f; forward.y = 0.0f; forward.z = -1.0f;
    }
}

vec3 Frame::get_x() {
    return cross(up, forward);
}

void Frame::move_forward(float fDelta) {
    origin += forward * fDelta;
}

void Frame::move_up(float fDelta) {
    origin += up * fDelta;
}

void Frame::move_right(float fDelta) {
    vec3 x = cross(up, forward);
    origin += x * fDelta;
}

void Frame::get_matrix(float* mat, bool rotation_only) {
    vec3 x = cross(up, forward);
    
    // Column-major order
    mat[0] = x.x;     mat[4] = up.x;     mat[8] = forward.x;  mat[12] = rotation_only ? 0.0f : origin.x;
    mat[1] = x.y;     mat[5] = up.y;     mat[9] = forward.y;  mat[13] = rotation_only ? 0.0f : origin.y;
    mat[2] = x.z;     mat[6] = up.z;     mat[10] = forward.z; mat[14] = rotation_only ? 0.0f : origin.z;
    mat[3] = 0.0f;    mat[7] = 0.0f;     mat[11] = 0.0f;      mat[15] = 1.0f;
}

void Frame::get_camera_matrix(float* mat, bool rotation_only) {
    vec3 z = -forward;
    vec3 x = cross(up, z);
    
    // Column-major order for rotation (transposed)
    mat[0] = x.x;     mat[4] = x.y;     mat[8] = x.z;      mat[12] = 0.0f;
    mat[1] = up.x;    mat[5] = up.y;    mat[9] = up.z;     mat[13] = 0.0f;
    mat[2] = z.x;     mat[6] = z.y;     mat[10] = z.z;     mat[14] = 0.0f;
    mat[3] = 0.0f;    mat[7] = 0.0f;    mat[11] = 0.0f;    mat[15] = 1.0f;
    
    if (!rotation_only) {
        // Apply translation
        mat[12] = -(x.x * origin.x + x.y * origin.y + x.z * origin.z);
        mat[13] = -(up.x * origin.x + up.y * origin.y + up.z * origin.z);
        mat[14] = -(z.x * origin.x + z.y * origin.y + z.z * origin.z);
    }
}

void Frame::rotate_local_y(float fAngle) {
    float mat[9];
    load_rotation_mat3(mat, up, fAngle);
    forward = mat3_mult_vec3(mat, forward);
}

void Frame::rotate_local_z(float fAngle) {
    float mat[9];
    load_rotation_mat3(mat, forward, fAngle);
    up = mat3_mult_vec3(mat, up);
}

void Frame::rotate_local_x(float fAngle) {
    float mat[9];
    vec3 localX = cross(up, forward);
    load_rotation_mat3(mat, localX, fAngle);
    up = mat3_mult_vec3(mat, up);
    forward = mat3_mult_vec3(mat, forward);
}

void Frame::normalize(bool keep_forward) {
    vec3 vCross;
    
    if (!keep_forward) {
        vCross = cross(up, forward);
        forward = cross(vCross, up);
    } else {
        vCross = cross(forward, up);
        up = cross(vCross, forward);
    }
    
    up = normalize_vec3(up);
    forward = normalize_vec3(forward);
}

void Frame::rotate_world(float fAngle, float x, float y, float z) {
    float mat[9];
    load_rotation_mat3(mat, vec3(x, y, z), fAngle);
    up = mat3_mult_vec3(mat, up);
    forward = mat3_mult_vec3(mat, forward);
}

void Frame::rotate_local(float fAngle, float x, float y, float z) {
    vec3 vLocal(x, y, z);
    vec3 vWorld = local_to_world(vLocal, true);
    rotate_world(fAngle, vWorld.x, vWorld.y, vWorld.z);
}

vec3 Frame::local_to_world(const vec3 vLocal, bool bRotOnly) {
    vec3 vWorld;
    
    float mat[16];
    get_matrix(mat, true);
    
    vWorld.x = mat[0] * vLocal.x + mat[4] * vLocal.y + mat[8] * vLocal.z;
    vWorld.y = mat[1] * vLocal.x + mat[5] * vLocal.y + mat[9] * vLocal.z;
    vWorld.z = mat[2] * vLocal.x + mat[6] * vLocal.y + mat[10] * vLocal.z;
    
    if (!bRotOnly)
        vWorld += origin;
    
    return vWorld;
}
