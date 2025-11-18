#include <cglm/cglm.h>
#include <stdio.h>

int main() {
    vec3 v1 = {1.0f, 2.0f, 3.0f};
    vec3 v2 = {4.0f, 5.0f, 6.0f};
    vec3 result;


    // 向量加法
    glm_vec3_add(v1, v2, result);
    printf("Vector addition result: [%f, %f, %f]\n", result[0], result[1], result[2]);

    // 向量点积
    float dotProduct = glm_vec3_dot(v1, v2);
    printf("Dot product: %f\n", dotProduct);

    // 向量叉积
    glm_vec3_cross(result, v1, v2);
    printf("Cross product result: [%f, %f, %f]\n", result[0], result[1], result[2]);

    return 0;
}