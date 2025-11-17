#include <WidgetEx/LayoutWidget.h>
#include <WidgetEx/LayoutWin.h>
#include <Widget/WidgetX.h>

#include <x++/X.h>
#include <unistd.h>

#include <tinyjson/tinyjson.h>

#include "js.h"
#include "mbc.h"
#include "platform.h"
#include "native_UniObject.h"
#include "mem.h"

/**
load extra native libs.
*/

#ifdef __cplusplus /* __cplusplus */
extern "C" {
#endif

void reg_natives(vm_t* vm);
void reg_native_wjs(vm_t* vm, void* arg);
bool js_compile(bytecode_t *bc, const char* input);

static inline var_t* vm_load_var(vm_t* vm, const char* name, bool create) {
	node_t* n = vm_load_node(vm, name, create);
	if(n != NULL)
		return n->var;
	return NULL;
}

static inline void vm_load_basic_classes(vm_t* vm) {
	vm->var_String = vm_load_var(vm, "String", false);
	vm->var_Array = vm_load_var(vm, "Array", false);
	vm->var_Number = vm_load_var(vm, "Number", false);
}

vm_t* init_js(void) {
	platform_init();
	mem_init();
	vm_t* vm = vm_new(js_compile);
	vm->gc_buffer_size = 1024;
	vm_init(vm, reg_natives, NULL);
	vm_load_basic_classes(vm);
	return vm;
}

bool load_wjs(vm_t* vm, const char* fname) {
	if(fname == NULL || fname[0] == 0)
		return false;
	bool res = false;
	if(strstr(fname, ".js") != NULL)
		res = load_js(vm, fname);
	else if(strstr(fname, ".mbc") != NULL) {
		bc_release(&vm->bc);
		res = vm_load_mbc(vm, fname);
	}
	return res;
}

void quit_js(vm_t* vm) {
	vm_close(vm);
	mem_quit();
}

#ifdef __cplusplus /* __cplusplus */
}
#endif

vm_t* _vm;

using namespace Ewok;
#define CLS_Widget "Widget"

static void onMenuItemFunc(MenuItem* it, void* data) {
	vm_t* vm = _vm;
	var_t* args = var_new(vm);
	var_add(args, "menuID", var_new_int(vm, it->id));
	call_m_func_by_name(_vm, NULL, "_onMenuItemEvent", args);
	var_unref(args);
}

static void onEventFunc(Widget* wd, xevent_t* xev, void* arg) {
	vm_t* vm = _vm;
	
	var_t* evt_arg = var_new_obj(vm, NULL, NULL);
	var_add(evt_arg, "type", var_new_int(vm, xev->type));
	var_add(evt_arg, "state", var_new_int(vm, xev->state));

	var_t* mouse_arg = var_new_obj(vm, NULL, NULL);
	var_add(mouse_arg, "x", var_new_int(vm, xev->value.mouse.x));
	var_add(mouse_arg, "y", var_new_int(vm, xev->value.mouse.y));
	var_add(mouse_arg, "rx", var_new_int(vm, xev->value.mouse.rx));
	var_add(mouse_arg, "ry", var_new_int(vm, xev->value.mouse.ry));
	var_add(evt_arg, "mouse", mouse_arg);

	var_t* im_arg = var_new_obj(vm, NULL, NULL);
	var_add(im_arg, "value", var_new_int(vm, xev->value.im.value));
	var_add(evt_arg, "im", im_arg);

	var_t* args = var_new(vm);
	var_t* var_wd = new_obj(vm, CLS_Widget, 0);
	var_wd->value = wd;
	var_wd->free_func = free_none;
	var_add(args, "widget", var_wd);
	var_add(args, "event", evt_arg);

	call_m_func_by_name(_vm, NULL, "_onWidgetEvent", args);
	var_unref(args);
}

static int doargs(int argc, char* argv[]) {
	int c = 0;
	while (c != -1) {
		c = getopt (argc, argv, "d");
		if(c == -1)
			break;

		switch (c) {
		case 'd':
			_m_debug = true;
			break;
		case '?':
			return -1;
		default:
			c = -1;
			break;
		}
	}
	return optind;
}

static bool loadWJS(const string& wjs_fname, string& layout_fname, string& js_fname) {
    json_var_t* conf_var = json_parse_file(wjs_fname.c_str());
    if(conf_var == NULL)
        return false;
		
	layout_fname = json_get_str(conf_var, "layout");
	js_fname = json_get_str(conf_var, "js");
    
    json_var_unref(conf_var);
    return true;
}


int main(int argc, char** argv) {
	int argind = doargs(argc, argv);
	if(argind < 0) {
		return -1;
	}

	const char* wsj_fname = "";
	if(argind < argc) {
		wsj_fname = argv[argind];
		argind++;
	}

	if(wsj_fname[0] == 0) {
		slog("Usage: %s <xxxx.wjs>\n", argv[0]);
		return -1;
	}

	string layout_fname, js_fname;
	if(!loadWJS(wsj_fname, layout_fname, js_fname)) {
		return -1;
	}

	X x;
	LayoutWin win;
	LayoutWidget* layout = win.getLayoutWidget();

	_vm = init_js();
	reg_native_wjs(_vm, layout);
	load_wjs(_vm, js_fname.c_str());
	vm_run(_vm);

	layout->setMenuItemFunc(onMenuItemFunc);
	layout->setEventFunc(onEventFunc);

	win.loadConfig(layout_fname.c_str()); // 加载布局文件
	win.open(&x, -1, -1, -1, 0, 0, argv[1], XWIN_STYLE_NORMAL);
	win.setTimer(16);

	widgetXRun(&x, &win);	
	quit_js(_vm);
	return 0;
}