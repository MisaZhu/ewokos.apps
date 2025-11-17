#include <WidgetEx/LayoutWidget.h>

#include <unistd.h>
#include <tinyjson/tinyjson.h>
#include "native_UniObject.h"

#define CLS_WJS "WJS"
#define CLS_XEvent "XEvent"
#define CLS_Widget "Widget"

using namespace Ewok;

var_t* native_wjs_getByName(vm_t* vm, var_t* env, void* data) {
	LayoutWidget* layout = (LayoutWidget*)data;
	const char* name = get_str(env, "name");
	Widget* wd = layout->getChild(name);
	if(wd == NULL)
		return NULL;

	var_t* var_wd = new_obj(vm, CLS_Widget, 0);
	var_wd->value = wd;
	var_wd->free_func = free_none;
	return var_wd;
}

var_t* native_wjs_getByID(vm_t* vm, var_t* env, void* data) {
	LayoutWidget* layout = (LayoutWidget*)data;
	uint32_t id = get_int(env, "id");
	Widget* wd = layout->getChild(id);
	if(wd == NULL)
		return NULL;

	var_t* var_wd = new_obj(vm, CLS_Widget, 0);
	var_wd->value = wd;
	var_wd->free_func = free_none;
	return var_wd;
}


#ifdef __cplusplus /* __cplusplus */
extern "C" {
#endif

void reg_native_wjs(vm_t* vm, void* arg) {
	var_t* cls = vm_new_class(vm, CLS_WJS);
	vm_reg_static(vm, cls, "getWidgetByName(name)", native_wjs_getByName, arg); 
	vm_reg_static(vm, cls, "getWidgetByID(id)", native_wjs_getByID, arg); 

	cls = vm_new_class(vm, CLS_XEvent);
	vm_reg_var(vm, cls, "MOUSE", var_new_int(vm, XEVT_MOUSE), true);
	vm_reg_var(vm, cls, "IM", var_new_int(vm, XEVT_IM), true);
	vm_reg_var(vm, cls, "MOUSE_MOVE", var_new_int(vm, MOUSE_STATE_MOVE), true);
	vm_reg_var(vm, cls, "MOUSE_DOWN", var_new_int(vm, MOUSE_STATE_DOWN), true);
	vm_reg_var(vm, cls, "MOUSE_UP", var_new_int(vm, MOUSE_STATE_UP), true);
	vm_reg_var(vm, cls, "MOUSE_DRAG", var_new_int(vm, MOUSE_STATE_DRAG), true);
	vm_reg_var(vm, cls, "MOUSE_CLICK", var_new_int(vm, MOUSE_STATE_CLICK), true);

	cls = vm_new_class(vm, CLS_Widget);
	vm_reg_uniobject_native(vm, cls, NULL); 
}

#ifdef __cplusplus /* __cplusplus */
}
#endif