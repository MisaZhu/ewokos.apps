#include "vm_json_var.h"

#ifdef __cplusplus /* __cplusplus */
extern "C" {
#endif

var_t* json_var_to_vm_var(vm_t* vm, json_var_t* var) {
	if(var == NULL)
		return NULL;

	str_t* str = str_new("");
	json_var_to_json_str(var, str, 0);
	var_t* vm_var = json_parse(vm, str->cstr);
	str_free(str);
	return vm_var;
}

json_var_t* vm_var_to_json_var(var_t* var) {
	if(var == NULL)
		return NULL;

	mstr_t* str = mstr_new("");
	var_to_json_str(var, str, 0);
	json_var_t* json_var = json_parse_str(str->cstr);
	mstr_free(str);
	return json_var;
}

#ifdef __cplusplus /* __cplusplus */
}
#endif
