#include "native_UniObject.h"
#include "UniObject/UniObject.h"
#include <tinyjson/tinyjson.h>
#include <vm_json_var.h>
#include <string>

using namespace std;
using namespace Ewok;

static var_t* native_UniObject_call(vm_t* vm, var_t* env, void* data) {
	UniObject* obj = (UniObject*)get_raw(env, THIS);
	if(obj == NULL)
		return NULL;

	string func = get_str(env, "func");
	var_t* arg = get_obj(env, "arg");

	json_var_t* json_arg = vm_var_to_json_var(arg);
	json_var_t* res = obj->call(func, json_arg);
	if(json_arg != NULL)
		json_var_unref(json_arg);

	var_t* ret = json_var_to_vm_var(vm, res);
	if(res != NULL)
		json_var_unref(res);;
	return ret;
}

static var_t* native_UniObject_get(vm_t* vm, var_t* env, void* data) {
	UniObject* obj = (UniObject*)get_raw(env, THIS);
	if(obj == NULL)
		return NULL;

	string name = get_str(env, "name");
	json_var_t* value = obj->get(name);	
	var_t* ret = json_var_to_vm_var(vm, value);

	if(value != NULL)
		json_var_unref(value);
	return ret;
}

static var_t* native_UniObject_set(vm_t* vm, var_t* env, void* data) {
	UniObject* obj = (UniObject*)get_raw(env, THIS);
	if(obj == NULL)
		return NULL;

	string name = get_str(env, "name");
	var_t* value = get_obj(env, "value");

	json_var_t* json_value = vm_var_to_json_var(value);
	obj->set(name, json_value);
	if(json_value != NULL)
		json_var_unref(json_value);
	return NULL;
}

void vm_reg_uniobject_native(vm_t* vm, var_t* cls, void* data) {
	vm_reg_native(vm, cls, "get(name)", native_UniObject_get, NULL); 
	vm_reg_native(vm, cls, "set(name, value)", native_UniObject_set, NULL); 
	vm_reg_native(vm, cls, "call(func, arg)", native_UniObject_call, NULL); 
}