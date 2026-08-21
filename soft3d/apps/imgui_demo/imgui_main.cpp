// Dear ImGui: standalone example application for EwokOS xwin
// Ported from SDL2 to xwin

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <ewoksys/proc.h>
#include <ewoksys/kernel_tic.h>
#include <ewoksys/timer.h>
#include <ewoksys/klog.h>
#include <x/x.h>
#include <x/xwin.h>
#include <graph/graph.h>

#include "glcommon/gltools.h"

#define IMGUI_IMPLEMENTATION
#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_pgl_geometry.h"


static int win_width = 640;
static int win_height = 480;

static glContext the_context;
static pix_t* backbuf = NULL;

// ImGui state
bool show_demo_window = true;
bool show_another_window = false;
ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

// Mouse state
static int mouse_x = 0;
static int mouse_y = 0;
static bool mouse_buttons[3] = {false, false, false}; // left, right, middle

// Keyboard state
static bool key_ctrl = false;
static bool key_shift = false;
static bool key_alt = false;
static bool key_super = false;

// FPS counter
static int fps = 0;
static int frame_count = 0;
static uint32_t last_tic = 0;

static void update_fps(void)
{
    uint32_t low;
    kernel_tic32(NULL, NULL, &low);
    
    if (last_tic == 0 || (low - last_tic) >= 3000000) {
        last_tic = low;
        fps = frame_count / 3;
        frame_count = 0;
    }
    frame_count++;
}

// Simple triangle for background
float points[] = { -0.5, -0.5, 0,
                    0.5, -0.5, 0,
                    0,    0.5, 0 };

typedef struct My_Uniforms
{
    mat4 mvp_mat;
    vec4 v_color;
} My_Uniforms;

void normal_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
{
    builtins->gl_Position = mult_m4_v4(*((mat4*)uniforms), vertex_attribs[0]);
}

void normal_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
{
    builtins->gl_FragColor = ((My_Uniforms*)uniforms)->v_color;
}

static void init_imgui(void)
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    
    // Disable imgui.ini
    io.IniFilename = NULL;
    
    // Setup display size
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);
    
    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    
    // Setup Renderer backend
    ImGui_ImplPGL_Geometry_Init();
    
    // Load Fonts
    io.Fonts->AddFontFromFileTTF("/usr/system/fonts/system.ttf", 16.0f);
}

// Map EwokOS key code to ImGuiKey
static ImGuiKey KeyCodeToImGuiKey(int keycode)
{
    switch (keycode)
    {
        case 9: return ImGuiKey_Tab;
        case 27: return ImGuiKey_Escape;
        case 13: return ImGuiKey_Enter;
        case 8: return ImGuiKey_Backspace;
        case 127: return ImGuiKey_Delete;
        case 32: return ImGuiKey_Space;
        
        // Arrow keys
        case 276: return ImGuiKey_LeftArrow;
        case 275: return ImGuiKey_RightArrow;
        case 273: return ImGuiKey_UpArrow;
        case 274: return ImGuiKey_DownArrow;
        
        // Page/Home/End
        case 278: return ImGuiKey_Home;
        case 279: return ImGuiKey_End;
        case 280: return ImGuiKey_PageUp;
        case 281: return ImGuiKey_PageDown;
        case 277: return ImGuiKey_Insert;
        
        // Numbers
        case '0': return ImGuiKey_0;
        case '1': return ImGuiKey_1;
        case '2': return ImGuiKey_2;
        case '3': return ImGuiKey_3;
        case '4': return ImGuiKey_4;
        case '5': return ImGuiKey_5;
        case '6': return ImGuiKey_6;
        case '7': return ImGuiKey_7;
        case '8': return ImGuiKey_8;
        case '9': return ImGuiKey_9;
        
        // Letters
        case 'a': case 'A': return ImGuiKey_A;
        case 'b': case 'B': return ImGuiKey_B;
        case 'c': case 'C': return ImGuiKey_C;
        case 'd': case 'D': return ImGuiKey_D;
        case 'e': case 'E': return ImGuiKey_E;
        case 'f': case 'F': return ImGuiKey_F;
        case 'g': case 'G': return ImGuiKey_G;
        case 'h': case 'H': return ImGuiKey_H;
        case 'i': case 'I': return ImGuiKey_I;
        case 'j': case 'J': return ImGuiKey_J;
        case 'k': case 'K': return ImGuiKey_K;
        case 'l': case 'L': return ImGuiKey_L;
        case 'm': case 'M': return ImGuiKey_M;
        case 'n': case 'N': return ImGuiKey_N;
        case 'o': case 'O': return ImGuiKey_O;
        case 'p': case 'P': return ImGuiKey_P;
        case 'q': case 'Q': return ImGuiKey_Q;
        case 'r': case 'R': return ImGuiKey_R;
        case 's': case 'S': return ImGuiKey_S;
        case 't': case 'T': return ImGuiKey_T;
        case 'u': case 'U': return ImGuiKey_U;
        case 'v': case 'V': return ImGuiKey_V;
        case 'w': case 'W': return ImGuiKey_W;
        case 'x': case 'X': return ImGuiKey_X;
        case 'y': case 'Y': return ImGuiKey_Y;
        case 'z': case 'Z': return ImGuiKey_Z;
        
        // Function keys
        case 282: return ImGuiKey_F1;
        case 283: return ImGuiKey_F2;
        case 284: return ImGuiKey_F3;
        case 285: return ImGuiKey_F4;
        case 286: return ImGuiKey_F5;
        case 287: return ImGuiKey_F6;
        case 288: return ImGuiKey_F7;
        case 289: return ImGuiKey_F8;
        case 290: return ImGuiKey_F9;
        case 291: return ImGuiKey_F10;
        case 292: return ImGuiKey_F11;
        case 293: return ImGuiKey_F12;
        
        // Special characters
        case ',': return ImGuiKey_Comma;
        case '.': return ImGuiKey_Period;
        case '/': return ImGuiKey_Slash;
        case ';': return ImGuiKey_Semicolon;
        case '\'': return ImGuiKey_Apostrophe;
        case '[': return ImGuiKey_LeftBracket;
        case ']': return ImGuiKey_RightBracket;
        case '-': return ImGuiKey_Minus;
        case '=': return ImGuiKey_Equal;
        case '`': return ImGuiKey_GraveAccent;
        
        default: return ImGuiKey_None;
    }
}

static void process_imgui_frame(void)
{
    ImGuiIO& io = ImGui::GetIO();
    
    // Update display size (in case of resize)
    io.DisplaySize = ImVec2((float)win_width, (float)win_height);
    
    // Update mouse position
    io.MousePos = ImVec2((float)mouse_x, (float)mouse_y);
    
    // Update mouse buttons
    io.MouseDown[0] = mouse_buttons[0]; // left
    io.MouseDown[1] = mouse_buttons[1]; // right
    io.MouseDown[2] = mouse_buttons[2]; // middle
    
    // Update keyboard modifiers
    io.KeyCtrl = key_ctrl;
    io.KeyShift = key_shift;
    io.KeyAlt = key_alt;
    io.KeySuper = key_super;
    
    // Start the Dear ImGui frame
    ImGui_ImplPGL_Geometry_NewFrame();
    ImGui::NewFrame();
    
    // 1. Show the big demo window
    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);
    
    // 2. Show a simple window
    {
        static float f = 0.0f;
        static int counter = 0;
        
        ImGui::Begin("Hello, world!");
        ImGui::Text("This is some useful text.");
        ImGui::Checkbox("Demo Window", &show_demo_window);
        ImGui::Checkbox("Another Window", &show_another_window);
        
        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
        ImGui::ColorEdit3("clear color", (float*)&clear_color);
        
        if (ImGui::Button("Button")) {
            counter++;
            printf("%.1f FPS\n", ImGui::GetIO().Framerate);
        }
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);
        
        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 
                    1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    }
    
    // 3. Show another simple window
    if (show_another_window)
    {
        ImGui::Begin("Another Window", &show_another_window);
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            show_another_window = false;
        ImGui::End();
    }
}

static void render_frame(void)
{
    My_Uniforms the_uniforms;
    mat4 identity = IDENTITY_M4();
    
    // Ensure viewport and scissor match window size
    // PortableGL's glScissor modifies c->ux/c->uy even without GL_SCISSOR_TEST
    // so we must reset it before glClear or only part of the screen gets cleared
    glScissor(0, 0, win_width, win_height);
    
    // Clear background (color and depth)
    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    // Draw triangle
    memcpy(the_uniforms.mvp_mat, identity, sizeof(mat4));
    the_uniforms.v_color.x = 1.0f;
    the_uniforms.v_color.y = 0.0f;
    the_uniforms.v_color.z = 0.0f;
    the_uniforms.v_color.w = 1.0f;
    SetUniform(&the_uniforms);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    
    // GUI Rendering
    ImGui::Render();
    ImGui_ImplPGL_Geometry_RenderDrawData(ImGui::GetDrawData());
}

static void on_repaint(xwin_t* xwin, graph_t* g)
{
    (void)xwin;
    
    // Process ImGui frame
    process_imgui_frame();
    
    // Render frame
    render_frame();
    
    // Blit to screen
    graph_t bg;
    graph_init(&bg, backbuf, win_width, win_height);
    graph_blt(&bg, 0, 0, win_width, win_height, g, 0, 0, win_width, win_height);
    
    update_fps();
}

static void on_event(xwin_t* xwin, xevent_t* ev)
{
    ImGuiIO& io = ImGui::GetIO();
    
    if (ev->type == XEVT_IM) {
        int key = ev->value.im.value;
        uint32_t shift = ev->value.im.shift;
        uint32_t ctrl = ev->value.im.ctrl;
        int32_t state = ev->state; // XIM_STATE_PRESS or XIM_STATE_RELEASE
        
        // Update modifier keys
        key_shift = (shift != 0);
        key_ctrl = (ctrl != 0);
        
        // Handle ESC key to close window (only on press)
        if (key == 27 && state == XIM_STATE_PRESS) {
            xwin_close(xwin);
            return;
        }
        
        // Map key to ImGuiKey and add event
        ImGuiKey imgui_key = KeyCodeToImGuiKey(key);
        if (imgui_key != ImGuiKey_None) {
            bool pressed = (state == XIM_STATE_PRESS);
            io.AddKeyEvent(imgui_key, pressed);
        }
        
        // Add character input for printable characters (only on press)
        if (state == XIM_STATE_PRESS && key >= 32 && key < 127) {
            io.AddInputCharacter(key);
        }
    }
    else if (ev->type == XEVT_MOUSE) {
        gpos_t pos = xwin_get_inside_pos(xwin, ev->value.mouse.x, ev->value.mouse.y);
        
        mouse_x = pos.x;
        mouse_y = pos.y;
        
        // Update mouse buttons based on state (state is at top level of xevent_t)
        int32_t state = ev->state;
        int32_t button = ev->value.mouse.button;
        
        if (button == MOUSE_BUTTON_LEFT) {
            mouse_buttons[0] = (state == MOUSE_STATE_DOWN || state == MOUSE_STATE_DRAG);
        }
        else if (button == MOUSE_BUTTON_RIGHT) {
            mouse_buttons[1] = (state == MOUSE_STATE_DOWN || state == MOUSE_STATE_DRAG);
        }
        else if (button == MOUSE_BUTTON_MID) {
            mouse_buttons[2] = (state == MOUSE_STATE_DOWN || state == MOUSE_STATE_DRAG);
        }
    }
}

static void on_resize(xwin_t* xwin)
{
    if(xwin == NULL || xwin->xinfo == NULL)
        return;
    
    int width = xwin->xinfo->wsr.w;
    int height = xwin->xinfo->wsr.h;
    
    if (width <= 0 || height <= 0)
        return;
    
    win_width = width;
    win_height = height;
    
    // Resize PortableGL framebuffer
    ResizeFramebuffer(win_width, win_height);
    backbuf = (pix_t*)GetBackBuffer();
    
    // Update viewport
    glViewport(0, 0, win_width, win_height);
    
    // Clear the framebuffer after resize to avoid artifacts (color and depth)
    glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

static void loop(void* p)
{
    xwin_t* xwin = (xwin_t*)p;
    xwin_repaint(xwin);
    proc_usleep(1000);
}

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    
    x_t x;
    x_init(&x, NULL);
    
    x.on_loop = loop;
    xwin_t* xwin = xwin_open(&x, -1, 32, 32, win_width, win_height, "ImGui Demo", XWIN_STYLE_NORMAL);
    if (!xwin) {
        printf("Failed to open window\n");
        return 1;
    }
    
    xwin->on_repaint = on_repaint;
    xwin->on_event = on_event;
    xwin->on_resize = on_resize;
    
    pgl_set_max_vertices(PGL_TINY_MAX_VERTICES);

    // Initialize PortableGL context
    if (!init_glContext(&the_context, &backbuf, win_width, win_height)) {
        printf("Failed to initialize glContext\n");
        return 1;
    }
    
    // Set viewport to match window size
    glViewport(0, 0, win_width, win_height);
    
    // Create and bind VAO
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    
    // Create triangle buffer
    GLuint triangle;
    glGenBuffers(1, &triangle);
    glBindBuffer(GL_ARRAY_BUFFER, triangle);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat)*9, points, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
    
    // Create shader
    GLuint myshader = CreateProgram(normal_vs, normal_fs, 0, NULL, GL_FALSE);
    glUseProgram(myshader);
    
    // Enable depth test for proper clearing
    glEnable(GL_DEPTH_TEST);
    
    // Initialize ImGui
    init_imgui();
    
    // Make window visible
    xwin_set_visible(xwin, true);
    
    x_run(&x, xwin);
    
    // Cleanup
    ImGui_ImplPGL_Geometry_Shutdown();
    ImGui::DestroyContext();
    
    xwin_destroy(xwin);
    free_glContext(&the_context);
    
    return 0;
}
