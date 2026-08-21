#ifndef MESH_H
#define MESH_H

#include <stdint.h>

typedef struct {
    float x;
    float y;
    float z;
} vec3_t;

typedef struct {
    float u;
    float v;
} tex2_t;

typedef struct {
    int a;
    int b;
    int c;
    tex2_t a_uv;
    tex2_t b_uv;
    tex2_t c_uv;
    uint32_t color;
} face_t;

typedef struct {
    vec3_t* vertices;
    face_t* faces;
    vec3_t rotation;
    vec3_t scale;
    vec3_t translation;
} mesh_t;

extern int num_faces;
extern vec3_t vertices[];
extern face_t faces[];
extern mesh_t mesh;

#endif
