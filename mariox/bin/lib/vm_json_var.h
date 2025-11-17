#ifndef MARIO_VM_JSON_VAR_HH
#define MARIO_VM_JSON_VAR_HH

#include "mario.h"
#include <tinyjson/tinyjson.h>

#ifdef __cplusplus /* __cplusplus */
extern "C" {
#endif

var_t* json_var_to_vm_var(vm_t* vm, json_var_t* var);
json_var_t* vm_var_to_json_var(var_t* var);

#ifdef __cplusplus /* __cplusplus */
}
#endif

#endif
