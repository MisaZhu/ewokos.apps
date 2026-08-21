#pragma once
#ifndef GLFRAME_H_
#define GLFRAME_H_

// Simple vec3 struct without rsw_math dependency
struct vec3 {
    float x, y, z;
    vec3() : x(0), y(0), z(0) {}
    vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

struct Frame
{
    vec3 origin;	// Where am I?
    vec3 forward;	// Where am I going?
    vec3 up;		// Which way is up?

    Frame(bool camera=false, vec3 origin_ = vec3());

    vec3 get_z() { return forward; }
    vec3 get_y() { return up; }
    vec3 get_x();

    void translate_world(float x, float y, float z)
        { origin.x += x; origin.y += y; origin.z += z; }

    void translate_local(float x, float y, float z)
        { move_forward(z); move_up(y); move_right(x);	}

    void move_forward(float fDelta);
    void move_up(float fDelta);
    void move_right(float fDelta);

    void get_matrix(float* mat, bool bRotationOnly = false);
    void get_camera_matrix(float* mat, bool bRotationOnly = false);

    void rotate_local_y(float fAngle);
    void rotate_local_z(float fAngle);
    void rotate_local_x(float fAngle);

    void normalize(bool keep_forward);

    void rotate_world(float fAngle, float x, float y, float z);
    void rotate_local(float fAngle, float x, float y, float z);

    vec3 local_to_world(const vec3 vLocal, bool bRotOnly = false);
};

#endif /* GLFRAME_H_ */
