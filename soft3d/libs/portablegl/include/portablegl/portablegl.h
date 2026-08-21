/*

PortableGL 0.101.0 MIT licensed software renderer that closely mirrors OpenGL 3.x
portablegl.com
robertwinkler.com

Do this:
    #define PORTABLEGL_IMPLEMENTATION
before you include this file in *one* C or C++ file to create the implementation.

If you plan on using your own 3D vector/matrix library rather than crsw_math
that is built into PortableGL and your names are the standard glsl vec[2-4],
mat[3-4] etc., define PGL_PREFIX_TYPES too before including portablegl to
prefix all those builtin types with pgl_ to avoid the clash. Note, if
you use PGL_PREFIX_TYPES and write your own shaders, the type for vertex_attribs
is also affected, changing from vec4* to pgl_vec4*.

You can check all the C++ examples and demos, I use my C++ rsw_math library.

// i.e. it should look like this:
#include ...
#include ...
#include ...
// if required
#define PGL_PREFIX_TYPES

#define PORTABLEGL_IMPLEMENTATION
#include "portablegl/portablegl.h"

You can define PGL_ASSERT before the #include to avoid using assert.h.
You can define PGL_MALLOC, PGL_REALLOC, and PGL_FREE to avoid using malloc,
realloc, and free.
You can define PGL_MEMMOVE to avoid using memmove.

However, even if you define all of those before including portablegl, you
will still be using the standard library (math.h, string.h, stdlib.h, stdio.h
stdint.h, possibly others). It's not worth removing PortableGL's dependency on
the C standard library as it would make it far larger and more complicated
for no real benefit.


QUICK NOTES:
    Primarily of interest to game/graphics developers and other people who
    just want to play with the graphics pipeline and don't need peak
    performance or the the entirety of OpenGL or Vulkan features.

    For textures, GL_UNSIGNED_BYTE is the only supported type.
    Internally, GL_RGBA is the only supported format, however other formats
    are converted automatically to RGBA unless PGL_DONT_CONVERT_TEXTURES is
    defined (in which case a format other than GL_RGBA is a GL_INVALID_ENUM
    error). The argument internalformat is ignored to ease porting.

    Only GL_TEXTURE_MAG_FILTER is actually used internally but you can set the
    MIN_FILTER for a texture. Mipmaps are not supported (GenerateMipMap is
    a stub and the level argument is ignored/assumed 0) and *MIPMAP* filter
    settings are silently converted to NEAREST or LINEAR.

    The framebuffer format is a compile time setting which defaults
    to 32-bit RGBA memory order, though PGL supports any 32 or 16 bit pixel format
    as long as the appropriate macros are defined.
    Several variants are predefined for easy use, named for the integer order
    not the memory order:

        PGL_RGBA32: RGBA memory order on MSB architecture
        PGL_ABGR32: RGBA memory order on LSB architecture, default
        PGL_ARGB32/PGL_BGRA32: Two other common 32-bit formats
        PGL_RGB565/PGL_BGR565: Very common 16 bit formats
        PGL_RGBA5551/PGL_ABGR1555: Less common 16 bit formats

    Search PGL for those macros to see how to set up the controlling macros
    for other formats; it's pretty self explanatory though I may try to improve
    it in the future.

    The depth and stencil buffer defaults to a combined buffer with the high
    24 bits used for the depth value and the low 8 bits used for the stencil.
    This format is called PGL_D24S8 internally. The only other format supported
    is a 16 bit depth buffer and a separate 8-bit buffer for the stencil. This
    is selected by defining PGL_D16 before including PGL.

    If you define PGL_D16, you may also define PGL_NO_STENCIL to disable the
    stencil buffer entirely to save a bit more memory. However if you define
    PGL_NO_STENCIL you must define PGL_D16 as it makes no sense with
    the default PGL_D24S8.

    Lastly, you can define PGL_NO_DEPTH_NO_STENCIL which will of course
    disable both the depth and stencil buffers entirely.

    There are several predefined configuration depending on how much memory
    you want to/can use that select settings for the framebuffer formats and
    the vertex scratch space size:

    PGL_TINY_MEM: RGB565, D16, NO_STENCIL, 4 vertex attribs, 80 KB scratch space
    PGL_SMALL_MEM: Same as TINY but 800 KB scratch space
    PGL_MED_MEM: RGB565, 4 vertex attribs, 1.6 MB scratch space
    default: ABGR32, D24S8, 8 vertex attribs, 16 MB scratch space

    Obviously most of the time the default is fine, and if none of the
    presets match what you want you can mix and match and adjust any of
    the finer grained options individually, but don't define a preset *and*
    define individual settings as that will cause problems.


DOCUMENTATION
=============

Any PortableGL program has roughly this structure, with some things
possibly declared globally or passed around in function parameters
as needed:

    #define WIDTH 640
    #define HEIGHT 480

    // shaders are functions matching these prototypes
    void smooth_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms);
    void smooth_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms);

    typedef struct My_Uniforms {
        mat4 mvp_mat;
        vec4 v_color;
    } My_Uniforms;

    pix_t* backbuf = NULL;
    glContext the_context;

    if (!init_glContext(&the_context, &backbuf, WIDTH, HEIGHT)) {
        puts("Failed to initialize glContext");
        exit(0);
    }

    // interpolation is an array with an entry of PGL_SMOOTH, PGL_FLAT or
    // PGL_NOPERSPECTIVE for each float being interpolated between the
    // vertex and fragment shaders.  Convenience macros are available
    // for 2, 3, and 4 components, ie
    // PGL_FLAT3 expands to PGL_FLAT, PGL_FLAT, PGL_FLAT

    // the last parameter is whether the fragment shader writes to
    // gl_FragDepth or discard. When it is false, PGL may do early
    // fragment processing (scissor, depth, stencil etc) for a minor
    // performance boost but canonicaly these happen after the frag
    // shader
    GLenum interpolation[4] = { PGL_SMOOTH4 };
    GLuint myshader = pglCreateProgram(smooth_vs, smooth_fs, 4, interpolation, GL_FALSE);
    glUseProgram(myshader);

    // v_color is not actually used since we're using per vert color
    My_Uniform the_uniforms = { IDENTITY_MAT4() };
    pglSetUniform(&the_uniforms);

    // Your standard OpenGL buffer setup etc. here
    // Like the compatibility profile, we allow/enable a default
    // VAO.  We also have a default shader program for the same reason,
    // something to fill index 0.
    // see implementation of init_glContext for details

    while (1) {

        // standard glDraw calls, switching shaders etc.

        // use backbuf however you want, whether that's blitting
        // it to some framebuffer in your GUI system, or even writing
        // it out to disk with something like stb_image_write.
    }

    free_glContext(&the_context);

    // compare with equivalent glsl below
    void smooth_vs(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms)
    {
        ((vec4*)vs_output)[0] = vertex_attribs[1]; //color

        builtins->gl_Position = mult_mat4_vec4(*((mat4*)uniforms), vertex_attribs[0]);
    }

    void smooth_fs(float* fs_input, Shader_Builtins* builtins, void* uniforms)
    {
        builtins->gl_FragColor = ((vec4*)fs_input)[0];
    }

    // note smooth is the default so this is the same as smooth out vec4 vary_color
    // https://www.khronos.org/opengl/wiki/Type_Qualifier_(GLSL)#Interpolation_qualifiers
    uniform mvp_mat
    layout (location = 0) in vec4 in_vertex;
    layout (location = 1) in vec4 in_color;
    out vec4 vary_color;
    void main(void)
    {
        vary_color = in_color;
        gl_Position = mvp_mat * in_vertex;
    }

    in vec4 vary_color;
    out vec4 frag_color;
    void main(void)
    {
        frag_color = vary_color;
    }

    // You might also want to resize the framebuffer if your window is resizable
    // instead of just letting your GUI system scale the output in which case when
    // you handle a resize event you would do something like this:

    pglResizeFramebuffer(new_width, new_height);
    backbuf = (pix_t*)pglGetBackBuffer();
    glViewport(0, 0, new_width, new_height);
    // anything else you need for your particular GUI/windowing system here

    // alternatively, if your backbuffer (ie color buffer) is changing
    // ie, you're switching between rendering to a texture and the normal
    // backbuffer, you would call pglSetBackBuffer() and pglSetTexBackBuffer()
    // as needed:

    pglSetTexBackBuffer(tex_handle);
    pglSetBackBuffer(backbuf, width, height);

    // A few important things to note about these functions:
    // 1. The framebuffer pixel format must be 32-bit RGBA (this is the default
    //    PGL_ABGR32 aka RGBA32 on LSB) if you want to do render-to-texture
    //    because that's the only texture format supported. I have ideas for loosening
    //    this restriction at least a little in the future.
    //
    // 2. Neither function changes the the depth/stencil buffers so assuming
    //    you have depth and/or stencil, the only way to use these safely is
    //    to make sure the texture is the same dimensions or you provide the same
    //    width and height to pglSetBackBuffer().
    //
    // 3. PGLSetBackBuffer() does not change the ownership of the framebuffer memory,
    //    nor does it free the existing framebuffer even if it did own it.
    //    If the backbuffer was not user owned before the call, it will still
    //    not be after the call. If you didn't already get a pointer to the pixels
    //    (via pglGetBackBuffer() for example) you will have created a memory leak.
    //    Though this behavior may seem counter-intuitive, it is to support the most
    //    common use case more easily, where you let PGL handle the allocations and
    //    resizing but you hold a pointer so you can switch back and forth.
    //
    // 4. PGLSetTexBackBuffer() does change the owership of the framebuffer
    //    to match the texture used. Since this is almost always PGL owned
    //    it is usually a non-issue.
    //
    //  See the lesson16 example for examples of these functions in action.

That's basically it.  There are some other non-standard features like
pglSetInterp that lets you change the interpolation of a shader
whenever you want.  In real OpenGL you'd have to have 2 (or more) separate
but almost identical shaders to do that.


ADDITIONAL CONFIGURATION
========================

We've already mentioned several configuration macros above but here are
all of the non-framebuffer/memory related ones:

PGL_UNSAFE
    This replaces the old portablegl_unsafe.h
    It turns off all error checking and debug message/logging the same way
    NDEBUG turns off assert(). By default PGL is a GL_DEBUG_CONTEXT with
    GL_DEBUG_OUTPUT on and a default callback function printing to stdout.
    You can use Enable/Disable and DebugMessageCallback to turn it on/off
    or use your own callback function like normal. However with PGL_UNSAFE
    defined, there's nothing compiled in at all so I would only use it
    when you're pushing for every ounce of perf.

PGL_PREFIX_TYPES
    This prefixes the standard glsl types (and a couple other internal types)
    with pgl_ (ie vec2 becomes pgl_vec2)

PGL_ASSERT
PGL_MALLOC/PGL_REALLOC/PGL_FREE
PGL_MEMMOVE
    These overrride the standard functions of the same names

PGL_DONT_CONVERT_TEXTURES
    This makes passing PGL a texture with a format other than GL_RGBA an error.
    By default other types are automatically converted. You can perform the
    conversion manually using the function convert_format_to_packed_rgba().
    The included function convert_grayscale_to_rgba() is also useful,
    especially for font textures.

PGL_PREFIX_GLSL or PGL_SUFFIX_GLSL
    These replace PGL_EXCLUDE_GLSL. Since PGL depends on at least a few
    glsl functions and potentially more in the future it doesn't make
    sense to exclude GLSL entirely, especially since they're all inline so
    it really doesn't save you anything in the final executable.
    Instead, using one of these two macros you can change the handful of
    functions that are likely to cause a conflict with an external
    math library like glm (with a using declaration/directive of course).
    So smoothstep() would become either pgl_smoothstep() or smoothstepf(). So far it is less than
    10 functions that are modified but feel free to add more.

PGL_HERMITE_SMOOTHING
    Turn on hermite smoothing when doing linear interpolation of textures.
    It is not required by the spec and it does slow it down but it does
    look smoother so it's worth trying if you're curious. Note, most
    implementations do not use it.

PGL_BETTER_THICK_LINES
    If defined, use a more mathematically correct thick line drawing algorithm
    than the one in the official OpenGL spec.  It is about 15-17% slower but
    has the correct width. The default draws exactly width pixels in the
    minor axis, which results in only horizontal and vertical lines being
    correct. It also means the ends are not perpendicular to the line which
    looks worse the thicker the line.  The better algorithm is about what is
    specified for GL_LINE_SMOOTH/AA lines except without the actual
    anti-aliasing (ie no changes to the alpha channel).

PGL_DISABLE_COLOR_MASK
    If defined, color masking (which is set using glColorMask()) is ignored
    which provides some performance benefit though it varies depending on
    what you're doing.

PGL_EXCLUDE_STUBS
    If defined, PGL will exclude stubs for dozens of OpenGL functions that
    make porting existing OpenGL projects and reusing existing OpenGL
    helper/library code with PortableGL much easier.  This might make
    sense to define if you're starting a PGL project from scratch.

PGL_ENABLE_CLAMP_TO_BORDER
    By default it's ignored and treated the same as CLAMP_TO_EDGE because
    I can only think of two ways to implement it. The first way was to
    manually add a 1 pixel border around textures which was far more
    painful and ugly that it sounds to make work and means I can't have
    mapped texture data (ie pglTexImage2D that uses the pointer passed in)
    as well as making any future render to texture functionality more complicated.
    Th second way is with a bunch of extra if statements in the texture sampling
    code which slows down all accesses regardless of if they're using a border
    or not. So it's off by default and you can turn it on with this macro.

There are also several predefined maximums which you can change.
However, considering the performance limitations of PortableGL, the defaults
are probably more than enough, and in fact you might want to decrease PGL_MAX_VERTICES
and GL_MAX_VERTEX_ATTRIBS to save memory, see PGL memory presets in QUICK_NOTES above.

MAX_DRAW_BUFFERS, MAX_COLOR_ATTACHMENTS aren't used since those features aren't implemented.
PGL_MAX_VERTICES refers to the number of output vertices of a single draw call.

#define GL_MAX_VERTEX_ATTRIBS 8
#define GL_MAX_VERTEX_OUTPUT_COMPONENTS (4*GL_MAX_VERTEX_ATTRIBS)
#define GL_MAX_DRAW_BUFFERS 4
#define GL_MAX_COLOR_ATTACHMENTS 4

#define PGL_MAX_VERTICES 500000
#define PGL_MAX_ALIASED_WIDTH 2048.0f
#define PGL_MAX_TEXTURE_SIZE 16384
#define PGL_MAX_3D_TEXTURE_SIZE 8192
#define PGL_MAX_ARRAY_TEXTURE_LAYERS 8192

MIT License
Copyright (c) 2011-2025 Robert Winkler
Copyright (c) 1997-2025 Fabrice Bellard (clipping code from TinyGL)

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.


*/

#ifdef PGL_PREFIX_TYPES
#define vec2 pgl_vec2
#define vec3 pgl_vec3
#define vec4 pgl_vec4
#define ivec2 pgl_ivec2
#define ivec3 pgl_ivec3
#define ivec4 pgl_ivec4
#define uvec2 pgl_uvec2
#define uvec3 pgl_uvec3
#define uvec4 pgl_uvec4
#define bvec2 pgl_bvec2
#define bvec3 pgl_bvec3
#define bvec4 pgl_bvec4
#define mat2 pgl_mat2
#define mat3 pgl_mat3
#define mat4 pgl_mat4
#define Color pgl_Color
#define Line pgl_Line
#define Plane pgl_Plane
#endif


// I really need to think about these
// Maybe suffixes should just be the default since I already give many glsl
// functions suffixes but then we still have the problem if I ever want
// to support doubles with no suffix like C math funcs..
//
// For now it's just functions that are used inside PortableGL itself
// as that is what will definitely break without them if these macros
// are used

// Add/remove as needed as long as you also modify
// matching undef section in close_pgl.h

#ifdef PGL_PREFIX_GLSL
#define smoothstep pgl_smoothstep
#define clamp_01 pgl_clamp_01
#define clamp_01_v4 pgl_clamp_01_v4
#define clamp pgl_clamp
#define clampi pgl_clampi

#elif defined(PGL_SUFFIX_GLSL)

#define smoothstep smoothstepf
#define clamp_01 clampf_01
#define clamp_01_v4 clampf_01_v4
#define clamp clampf
#define clampi clampi
#endif


#ifndef GL_H
#define GL_H


#ifdef __cplusplus
extern "C" {
#endif

// Fast math approximations
#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// Fast math function declarations
float pgl_fast_sin(float x);
float pgl_fast_cos(float x);
float pgl_fast_sqrt(float x);
float pgl_fast_tan(float x);
float pgl_fast_acos(float x);
float pgl_fast_atan(float x);
float pgl_fast_atan2(float y, float x);


#ifndef PGLDEF
#ifdef PGL_STATIC
#define PGLDEF static
#else
#define PGLDEF extern
#endif
#endif

#ifndef PGL_ASSERT
#include <assert.h>
#define PGL_ASSERT(x) assert(x)
#endif

#ifndef CVEC_ASSERT
#define CVEC_ASSERT(x) PGL_ASSERT(x)
#endif

#if defined(PGL_MALLOC) && defined(PGL_FREE) && defined(PGL_REALLOC)
/* ok */
#elif !defined(PGL_MALLOC) && !defined(PGL_FREE) && !defined(PGL_REALLOC)
/* ok */
#else
#error "Must define all or none of PGL_MALLOC, PGL_FREE, and PGL_REALLOC."
#endif

#ifndef PGL_MALLOC
#define PGL_MALLOC(sz)      malloc(sz)
#define PGL_REALLOC(p, sz)  realloc(p, sz)
#define PGL_FREE(p)         free(p)
#endif

#define CVEC_MALLOC(sz) PGL_MALLOC(sz)
#define CVEC_REALLOC(p, sz) PGL_REALLOC(p, sz)
#define CVEC_FREE(p) PGL_FREE(p)

#ifndef PGL_MEMMOVE
#include <string.h>
#define PGL_MEMMOVE(dst, src, sz)   memmove(dst, src, sz)
#else
#define CVEC_MEMMOVE(dst, src, sz) PGL_MEMMOVE(dst, src, sz)
#endif

// Get rid of signed/unsigned comparison warnings when looping through vectors
#ifndef CVEC_SIZE_T
#define CVEC_SIZE_T i64
#endif




// Feel free to change these presets
// Do not use one of these combined with individual settings; you can cause problems
// if multiple selections within the same category are defined. I guard for
// depth/stencil settings but not framebuffer settings

#if !defined(PGL_D16) && !defined(PGL_D24S8) && !defined(PGL_NO_DEPTH_NO_STENCIL)
#  ifdef PGL_TINY_MEM
     // framebuffer mem use = 4*w*h
#    define PGL_RGB565
#    define PGL_D16
#    define PGL_NO_STENCIL
#  elif defined(PGL_SMALL_MEM)
     // 4*w*h
#    define PGL_RGB565
#    define PGL_D16
#    define PGL_NO_STENCIL
#  elif defined(PGL_MED_MEM)
     // 6*w*h
#    define PGL_RGB565
#    define PGL_D24S8
#  else
     // 8*w*h
     // defining this just for users convenience to detect different builds
     // though if you manually select smaller framebuffers this becomes confusing
#    define PGL_NORMAL_MEM

#    define PGL_D24S8
     // pixel format default to PGL_ABGR32 set below if no other defined
#  endif
#endif


// depth settings check
#ifdef PGL_NO_DEPTH_NO_STENCIL
#  if defined(PGL_D16) || defined(PGL_D24S8)
#    error "PGL_D16 and PGL_D24S8 are incompatible with PGL_NO_DEPTH_NO_STENCIL"
#  endif
#  ifdef PGL_NO_STENCIL
   //#warning is technically not standard till C23 and C++23 but supported by most compilers
#    warning "You don't need to define PGL_NO_STENCIL if you defined PGL_NO_DEPTH_NO_STENCIL"
#  else
#    define PGL_NO_STENCIL
#  endif
#endif


#if defined(PGL_AMASK) && defined(PGL_RMASK) && defined(PGL_GMASK) && defined(PGL_BMASK) && \
    defined(PGL_ASHIFT) && defined(PGL_RSHIFT) && defined(PGL_GSHIFT) && defined(PGL_BSHIFT) && \
    defined(PGL_RMAX) && defined(PGL_GMAX) && defined(PGL_BMAX) && defined(PGL_AMAX) && defined(PGL_BITDEPTH)
/* ok */
#elif !defined(PGL_AMASK) && !defined(PGL_RMASK) && !defined(PGL_GMASK) && !defined(PGL_BMASK) && \
    !defined(PGL_ASHIFT) && !defined(PGL_RSHIFT) && !defined(PGL_GSHIFT) && !defined(PGL_BSHIFT) && \
    !defined(PGL_RMAX) && !defined(PGL_GMAX) && !defined(PGL_BMAX) && !defined(PGL_AMAX) && !defined(PGL_BITDEPTH)
/* ok */
#else
#error "Must define all PGL_(RGBA)MASK, PGL_(RGBA)SHIFT, PGL_(RGBA)MAX, and PGL_BITDEPTH or none (which will give default PGL_AGBR32 format)"
#endif

// TODO more 32-bit formats
#ifdef PGL_RGBA32
 #define PGL_RMASK 0xFF000000
 #define PGL_GMASK 0x00FF0000
 #define PGL_BMASK 0x0000FF00
 #define PGL_AMASK 0x000000FF
 #define PGL_RSHIFT 24
 #define PGL_GSHIFT 16
 #define PGL_BSHIFT 8
 #define PGL_ASHIFT 0
 #define PGL_RMAX 255
 #define PGL_GMAX 255
 #define PGL_BMAX 255
 #define PGL_AMAX 255
 #define PGL_BITDEPTH 32
 #define PGL_PIX_STR "RGBA32"
#elif defined(PGL_ARGB32)
 #define PGL_AMASK 0xFF000000
 #define PGL_RMASK 0x00FF0000
 #define PGL_GMASK 0x0000FF00
 #define PGL_BMASK 0x000000FF
 #define PGL_ASHIFT 24
 #define PGL_RSHIFT 16
 #define PGL_GSHIFT 8
 #define PGL_BSHIFT 0
 #define PGL_RMAX 255
 #define PGL_GMAX 255
 #define PGL_BMAX 255
 #define PGL_AMAX 255
 #define PGL_BITDEPTH 32
 #define PGL_PIX_STR "ARGB32"
#elif defined(PGL_BGRA32)
 #define PGL_BMASK 0xFF000000
 #define PGL_GMASK 0x00FF0000
 #define PGL_RMASK 0x0000FF00
 #define PGL_AMASK 0x000000FF
 #define PGL_BSHIFT 24
 #define PGL_GSHIFT 16
 #define PGL_RSHIFT 8
 #define PGL_ASHIFT 0
 #define PGL_RMAX 255
 #define PGL_GMAX 255
 #define PGL_BMAX 255
 #define PGL_AMAX 255
 #define PGL_BITDEPTH 32
 #define PGL_PIX_STR "BGRA32"
#elif defined(PGL_RGB565)
 #define PGL_RMASK 0xF800
 #define PGL_GMASK 0x07E0
 #define PGL_BMASK 0x001F
 #define PGL_AMASK 0x0000
 #define PGL_RSHIFT 11
 #define PGL_GSHIFT 5
 #define PGL_BSHIFT 0
 #define PGL_ASHIFT 0
 #define PGL_RMAX 31
 #define PGL_GMAX 63
 #define PGL_BMAX 31
 #define PGL_AMAX 0
 #define PGL_BITDEPTH 16
 #define PGL_PIX_STR "RGB565"
#elif defined(PGL_BGR565)
 #define PGL_BMASK 0xF800
 #define PGL_GMASK 0x07E0
 #define PGL_RMASK 0x001F
 #define PGL_AMASK 0x0000
 #define PGL_BSHIFT 11
 #define PGL_GSHIFT 5
 #define PGL_RSHIFT 0
 #define PGL_ASHIFT 0
 #define PGL_RMAX 31
 #define PGL_GMAX 63
 #define PGL_BMAX 31
 #define PGL_AMAX 0
 #define PGL_BITDEPTH 16
 #define PGL_PIX_STR "BGR565"
#elif defined(PGL_RGBA5551)
 #define PGL_RMASK 0xF800
 #define PGL_GMASK 0x07C0
 #define PGL_BMASK 0x003E
 #define PGL_AMASK 0x0001
 #define PGL_RSHIFT 11
 #define PGL_GSHIFT 6
 #define PGL_BSHIFT 1
 #define PGL_ASHIFT 0
 #define PGL_RMAX 31
 #define PGL_GMAX 31
 #define PGL_BMAX 31
 #define PGL_AMAX 1
 #define PGL_BITDEPTH 16
 #define PGL_PIX_STR "RGBA5551"
#elif defined(PGL_ABGR1555)
 #define PGL_AMASK 0x8000
 #define PGL_BMASK 0x7C00
 #define PGL_GMASK 0x03E0
 #define PGL_RMASK 0x001F
 #define PGL_ASHIFT 15
 #define PGL_BSHIFT 10
 #define PGL_GSHIFT 5
 #define PGL_RSHIFT 0
 #define PGL_RMAX 31
 #define PGL_GMAX 31
 #define PGL_BMAX 31
 #define PGL_AMAX 1
 #define PGL_BITDEPTH 16
 #define PGL_PIX_STR "ABGR1555"
#else
 // default to PGL_ARGB32 for ARGB memory order on LSB
 #define PGL_ARGB32
 #define PGL_AMASK 0xFF000000
 #define PGL_RMASK 0x00FF0000
 #define PGL_GMASK 0x0000FF00
 #define PGL_BMASK 0x000000FF
 #define PGL_ASHIFT 24
 #define PGL_RSHIFT 16
 #define PGL_GSHIFT 8
 #define PGL_BSHIFT 0
 #define PGL_RMAX 255
 #define PGL_GMAX 255
 #define PGL_BMAX 255
 #define PGL_AMAX 255
 #define PGL_BITDEPTH 32
 #define PGL_PIX_STR "ARGB32"
#endif


// for now all 32 bit pixel types are 8888, no weird 10,10,10,2
#if PGL_BITDEPTH == 32
 #define RGBA_TO_PIXEL(r,g,b,a) ((u32)(a) << PGL_ASHIFT | (u32)(r) << PGL_RSHIFT | (u32)(g) << PGL_GSHIFT | (u32)(b) << PGL_BSHIFT)
 #define PIXEL_TO_COLOR(p) make_Color(((p) & PGL_RMASK) >> PGL_RSHIFT, ((p) & PGL_GMASK) >> PGL_GSHIFT, ((p) & PGL_BMASK) >> PGL_BSHIFT, ((p) & PGL_AMASK) >> PGL_ASHIFT)
 #define pix_t u32
 #define COLOR_TO_VEC4(c) Color_to_v4(c)
 #define VEC4_TO_COLOR(v) v4_to_Color(v)
#elif PGL_BITDEPTH == 16
 #if PGL_AMASK == 0
  #define RGBA_TO_PIXEL(r,g,b,a) ((int)(r) << PGL_RSHIFT | (int)(g) << PGL_GSHIFT | (int)(b) << PGL_BSHIFT)
  #define PIXEL_TO_COLOR(p) make_Color(((p) & PGL_RMASK) >> PGL_RSHIFT, ((p) & PGL_GMASK) >> PGL_GSHIFT, ((p) & PGL_BMASK) >> PGL_BSHIFT, 255)
 #else
  #define RGBA_TO_PIXEL(r,g,b,a) ((int)(a) << PGL_ASHIFT | (int)(r) << PGL_RSHIFT | (int)(g) << PGL_GSHIFT | (int)(b) << PGL_BSHIFT)
  #define PIXEL_TO_COLOR(p) make_Color(((p) & PGL_RMASK) >> PGL_RSHIFT, ((p) & PGL_GMASK) >> PGL_GSHIFT, ((p) & PGL_BMASK) >> PGL_BSHIFT, ((p) & PGL_AMASK) >> PGL_ASHIFT)
 #endif

 #define pix_t u16
 #define PIXEL_TO_VEC4(p) make_v4((((p) & PGL_RMASK) >> PGL_RSHIFT)/(float)PGL_RMAX, (((p) & PGL_GMASK) >> PGL_GSHIFT)/(float)PGL_GMAX, (((p) & PGL_BMASK) >> PGL_BSHIFT)/(float)PGL_BMAX, (((p) & PGL_AMASK) >> PGL_ASHIFT)/(float)PGL_AMAX)

 #define COLOR_TO_VEC4(c) make_v4((c).r/(float)PGL_RMAX, (c).g/(float)PGL_GMAX, (c).b/(float)PGL_BMAX, (c).a/(float)PGL_AMAX)
 #define VEC4_TO_COLOR(v) make_Color(v.x*PGL_RMAX, v.y*PGL_GMAX, v.z*PGL_BMAX, v.w*PGL_AMAX)
#endif


// TODO these are messy. It makes the code cleaner but I should try to simplify
// these some more. It's the different needs of glClear() vs fragment_processing()
// combined with the different formats that makes it a headache.
#ifdef PGL_D16
 #define PGL_MAX_Z 0xFFFF
 #define PGL_ZSHIFT 0
 #define GET_ZPIX(i) ((u16*)c->zbuf.lastrow)[(i)]
 #define GET_ZPIX_TOP(i) ((u16*)c->zbuf.buf)[(i)]
 #define GET_Z(i) GET_ZPIX(i)
 #define SET_Z_PRESHIFTED(i, v) GET_ZPIX(i) = (v)
 #define SET_Z_PRESHIFTED_TOP(i, v) GET_ZPIX_TOP(i) = (v)
 //#define SET_Z(i, orig_zpix, v) GET_ZPIX(i) = (v)
 #define SET_Z(i, v) GET_ZPIX(i) = (v)

 #define stencil_pix_t u8
 #define GET_STENCIL_PIX(i) c->stencil_buf.lastrow[(i)]
 #define GET_STENCIL_PIX_TOP(i) c->stencil_buf.buf[(i)]
 #define EXTRACT_STENCIL(stencil_pix) (stencil_pix)
 #define GET_STENCIL(i) GET_STENCIL_PIX(i)
 #define GET_STENCIL_TOP(i) GET_STENCIL_PIX_TOP(i)
 #define SET_STENCIL(i, v) GET_STENCIL_PIX(i) = (v)
 #define SET_STENCIL_TOP(i, v) GET_STENCIL_PIX_TOP(i) = (v)
#elif defined(PGL_D24S8)
 #ifdef PGL_NO_STENCIL
 #error "PGL_NO_STENCIL is incompatible with PGL_D24S8 format, use with PGL_D16"
 #endif

 #define PGL_MAX_Z 0xFFFFFF
 // could use GL_STENCIL_BITS..?
 #define PGL_ZSHIFT 8

 #define GET_ZPIX(i) ((u32*)c->zbuf.lastrow)[(i)]
 #define GET_ZPIX_TOP(i) ((u32*)c->zbuf.buf)[(i)]
 #define GET_Z(i) (GET_ZPIX(i) >> PGL_ZSHIFT)
 #define SET_Z_PRESHIFTED(i, v) \
     GET_ZPIX(i) &= PGL_STENCIL_MASK; \
     GET_ZPIX(i) |= (v)

 #define SET_Z_PRESHIFTED_TOP(i, v) \
     GET_ZPIX_TOP(i) &= PGL_STENCIL_MASK; \
     GET_ZPIX_TOP(i) |= (v)

// TO use this method I need to refactor to have the stencil val *after*
// the stencil test/op run, returned from stencil_op() perhaps.
// TODO compare perf eventually
/*
 #define SET_Z(i, stencil_val, v) \
     GET_ZPIX(i) = ((stencil_val) & PGL_STENCIL_MASK) | ((v) << PGL_ZSHIFT);
*/

 #define SET_Z(i, v) \
     GET_ZPIX(i) &= PGL_STENCIL_MASK; \
     GET_ZPIX(i) |= ((v) << PGL_ZSHIFT)

 #define stencil_pix_t u32
 #define GET_STENCIL_PIX(i) ((stencil_pix_t*)c->stencil_buf.lastrow)[(i)]
 #define GET_STENCIL_PIX_TOP(i) ((stencil_pix_t*)c->stencil_buf.buf)[(i)]
 #define EXTRACT_STENCIL(stencil_pix) ((stencil_pix) & PGL_STENCIL_MASK)
 #define GET_STENCIL(i) (GET_STENCIL_PIX(i) & PGL_STENCIL_MASK)
 #define GET_STENCIL_TOP(i) (GET_STENCIL_PIX_TOP(i) & PGL_STENCIL_MASK)
 #define SET_STENCIL(i, v) \
     GET_STENCIL_PIX(i) &= ~PGL_STENCIL_MASK; \
     GET_STENCIL_PIX(i) |= (v)

 #define SET_STENCIL_TOP(i, v) \
     GET_STENCIL_PIX_TOP(i) &= ~PGL_STENCIL_MASK; \
     GET_STENCIL_PIX_TOP(i) |= (v)
#elif defined(PGL_NO_DEPTH_NO_STENCIL)
/* ok */
#else
#error "Must define one of PGL_D16, PGL_D24S8, PGL_NO_DEPTH_NO_STENCIL"
#endif

#ifndef CRSW_MATH_H
#define CRSW_MATH_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// Unfortunately this is not supported in gcc even though
// it's in the C99+ spec.  Have to use compiler option
// -ffp-contract=off for gcc (which defaults to =fast)
// unlike clang
//
//  https://stackoverflow.com/questions/43352510/difference-in-gcc-ffp-contract-options

#define RM_PI (3.14159265358979323846)
#define RM_2PI (2.0 * RM_PI)
#define PI_DIV_180 (0.017453292519943296)
#define INV_PI_DIV_180 (57.2957795130823229)

#define DEG_TO_RAD(x)   ((x)*PI_DIV_180)
#define RAD_TO_DEG(x)   ((x)*INV_PI_DIV_180)

/* Hour angles */
#define HR_TO_DEG(x)    ((x) * (1.0 / 15.0))
#define HR_TO_RAD(x)    DEG_TO_RAD(HR_TO_DEG(x))

#define DEG_TO_HR(x)    ((x) * 15.0)
#define RAD_TO_HR(x)    DEG_TO_HR(RAD_TO_DEG(x))

// TODO rename RM_MAX/RSW_MAX?  make proper inline functions?
#ifndef MAX
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#endif

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;


typedef struct vec2
{
	float x;
	float y;
} vec2;

#define SET_V2(v, _x, _y) \
	do {\
	(v).x = _x;\
	(v).y = _y;\
	} while (0)

inline vec2 make_v2(float x, float y)
{
	vec2 v = { x, y };
	return v;
}

inline vec2 neg_v2(vec2 v)
{
	vec2 r = { -v.x, -v.y };
	return r;
}

inline void fprint_v2(FILE* f, vec2 v, const char* append)
{
	fprintf(f, "(%f, %f)%s", v.x, v.y, append);
}

inline void print_v2(vec2 v, const char* append)
{
	printf("(%f, %f)%s", v.x, v.y, append);
}

inline int fread_v2(FILE* f, vec2* v)
{
	int tmp = fscanf(f, " (%f, %f)", &v->x, &v->y);
	return (tmp == 2);
}

inline float len_v2(vec2 a)
{
	return sqrt(a.x * a.x + a.y * a.y);
}

inline vec2 norm_v2(vec2 a)
{
	float l = len_v2(a);
	vec2 c = { a.x/l, a.y/l };
	return c;
}

inline void normalize_v2(vec2* a)
{
	float l = len_v2(*a);
	a->x /= l;
	a->y /= l;
}

inline vec2 add_v2s(vec2 a, vec2 b)
{
	vec2 c = { a.x + b.x, a.y + b.y };
	return c;
}

inline vec2 sub_v2s(vec2 a, vec2 b)
{
	vec2 c = { a.x - b.x, a.y - b.y };
	return c;
}

inline vec2 mult_v2s(vec2 a, vec2 b)
{
	vec2 c = { a.x * b.x, a.y * b.y };
	return c;
}

inline vec2 div_v2s(vec2 a, vec2 b)
{
	vec2 c = { a.x / b.x, a.y / b.y };
	return c;
}

inline float dot_v2s(vec2 a, vec2 b)
{
	return a.x*b.x + a.y*b.y;
}

inline vec2 scale_v2(vec2 a, float s)
{
	vec2 b = { a.x * s, a.y * s };
	return b;
}

inline int equal_v2s(vec2 a, vec2 b)
{
	return (a.x == b.x && a.y == b.y);
}

inline int equal_epsilon_v2s(vec2 a, vec2 b, float epsilon)
{
	return (fabs(a.x-b.x) < epsilon && fabs(a.y - b.y) < epsilon);
}

inline float cross_v2s(vec2 a, vec2 b)
{
	return a.x * b.y - a.y * b.x;
}

inline float angle_v2s(vec2 a, vec2 b)
{
	return acos(dot_v2s(a, b) / (len_v2(a) * len_v2(b)));
}


typedef struct vec3
{
	float x;
	float y;
	float z;
} vec3;

#define SET_V3(v, _x, _y, _z) \
	do {\
	(v).x = _x;\
	(v).y = _y;\
	(v).z = _z;\
	} while (0)

inline vec3 make_v3(float x, float y, float z)
{
	vec3 v = { x, y, z };
	return v;
}

inline vec3 neg_v3(vec3 v)
{
	vec3 r = { -v.x, -v.y, -v.z };
	return r;
}

inline void fprint_v3(FILE* f, vec3 v, const char* append)
{
	fprintf(f, "(%f, %f, %f)%s", v.x, v.y, v.z, append);
}

inline void print_v3(vec3 v, const char* append)
{
	printf("(%f, %f, %f)%s", v.x, v.y, v.z, append);
}

inline int fread_v3(FILE* f, vec3* v)
{
	int tmp = fscanf(f, " (%f, %f, %f)", &v->x, &v->y, &v->z);
	return (tmp == 3);
}

inline float len_v3(vec3 a)
{
	return sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}

inline vec3 norm_v3(vec3 a)
{
	float l = len_v3(a);
	vec3 c = { a.x/l, a.y/l, a.z/l };
	return c;
}

inline void normalize_v3(vec3* a)
{
	float l = len_v3(*a);
	a->x /= l;
	a->y /= l;
	a->z /= l;
}

inline vec3 add_v3s(vec3 a, vec3 b)
{
	vec3 c = { a.x + b.x, a.y + b.y, a.z + b.z };
	return c;
}

inline vec3 sub_v3s(vec3 a, vec3 b)
{
	vec3 c = { a.x - b.x, a.y - b.y, a.z - b.z };
	return c;
}

inline vec3 mult_v3s(vec3 a, vec3 b)
{
	vec3 c = { a.x * b.x, a.y * b.y, a.z * b.z };
	return c;
}

inline vec3 div_v3s(vec3 a, vec3 b)
{
	vec3 c = { a.x / b.x, a.y / b.y, a.z / b.z };
	return c;
}

inline float dot_v3s(vec3 a, vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline vec3 scale_v3(vec3 a, float s)
{
	vec3 b = { a.x * s, a.y * s, a.z * s };
	return b;
}

inline int equal_v3s(vec3 a, vec3 b)
{
	return (a.x == b.x && a.y == b.y && a.z == b.z);
}

inline int equal_epsilon_v3s(vec3 a, vec3 b, float epsilon)
{
	return (fabs(a.x-b.x) < epsilon && fabs(a.y - b.y) < epsilon &&
			fabs(a.z - b.z) < epsilon);
}

inline vec3 cross_v3s(const vec3 u, const vec3 v)
{
	vec3 result;
	result.x = u.y*v.z - v.y*u.z;
	result.y = -u.x*v.z + v.x*u.z;
	result.z = u.x*v.y - v.x*u.y;
	return result;
}

inline float angle_v3s(const vec3 u, const vec3 v)
{
	return acos(dot_v3s(u, v));
}


typedef struct vec4
{
	float x;
	float y;
	float z;
	float w;
} vec4;

#define SET_V4(v, _x, _y, _z, _w) \
	do {\
	(v).x = _x;\
	(v).y = _y;\
	(v).z = _z;\
	(v).w = _w;\
	} while (0)

inline vec4 make_v4(float x, float y, float z, float w)
{
	vec4 v = { x, y, z, w };
	return v;
}

inline vec4 neg_v4(vec4 v)
{
	vec4 r = { -v.x, -v.y, -v.z, -v.w };
	return r;
}

inline void fprint_v4(FILE* f, vec4 v, const char* append)
{
	fprintf(f, "(%f, %f, %f, %f)%s", v.x, v.y, v.z, v.w, append);
}

inline void print_v4(vec4 v, const char* append)
{
	printf("(%f, %f, %f, %f)%s", v.x, v.y, v.z, v.w, append);
}

inline int fread_v4(FILE* f, vec4* v)
{
	int tmp = fscanf(f, " (%f, %f, %f, %f)", &v->x, &v->y, &v->z, &v->w);
	return (tmp == 4);
}

inline float len_v4(vec4 a)
{
	return sqrt(a.x * a.x + a.y * a.y + a.z * a.z + a.w * a.w);
}

inline vec4 norm_v4(vec4 a)
{
	float l = len_v4(a);
	vec4 c = { a.x/l, a.y/l, a.z/l, a.w/l };
	return c;
}

inline void normalize_v4(vec4* a)
{
	float l = len_v4(*a);
	a->x /= l;
	a->y /= l;
	a->z /= l;
	a->w /= l;
}

inline vec4 add_v4s(vec4 a, vec4 b)
{
	vec4 c = { a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w };
	return c;
}

inline vec4 sub_v4s(vec4 a, vec4 b)
{
	vec4 c = { a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w };
	return c;
}

inline vec4 mult_v4s(vec4 a, vec4 b)
{
	vec4 c = { a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w };
	return c;
}

inline vec4 div_v4s(vec4 a, vec4 b)
{
	vec4 c = { a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w };
	return c;
}

inline float dot_v4s(vec4 a, vec4 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

inline vec4 scale_v4(vec4 a, float s)
{
	vec4 b = { a.x * s, a.y * s, a.z * s, a.w * s };
	return b;
}

inline int equal_v4s(vec4 a, vec4 b)
{
	return (a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w);
}

inline int equal_epsilon_v4s(vec4 a, vec4 b, float epsilon)
{
	return (fabs(a.x-b.x) < epsilon && fabs(a.y - b.y) < epsilon &&
	        fabs(a.z - b.z) < epsilon && fabs(a.w - b.w) < epsilon);
}


typedef struct ivec2
{
	int x;
	int y;
} ivec2;

inline ivec2 make_iv2(int x, int y)
{
	ivec2 v = { x, y };
	return v;
}

inline void fprint_iv2(FILE* f, ivec2 v, const char* append)
{
	fprintf(f, "(%d, %d)%s", v.x, v.y, append);
}

inline int fread_iv2(FILE* f, ivec2* v)
{
	int tmp = fscanf(f, " (%d, %d)", &v->x, &v->y);
	return (tmp == 2);
}


typedef struct ivec3
{
	int x;
	int y;
	int z;
} ivec3;

inline ivec3 make_iv3(int x, int y, int z)
{
	ivec3 v = { x, y, z };
	return v;
}

inline void fprint_iv3(FILE* f, ivec3 v, const char* append)
{
	fprintf(f, "(%d, %d, %d)%s", v.x, v.y, v.z, append);
}

inline int fread_iv3(FILE* f, ivec3* v)
{
	int tmp = fscanf(f, " (%d, %d, %d)", &v->x, &v->y, &v->z);
	return (tmp == 3);
}



typedef struct ivec4
{
	int x;
	int y;
	int z;
	int w;
} ivec4;

inline ivec4 make_iv4(int x, int y, int z, int w)
{
	ivec4 v = { x, y, z, w };
	return v;
}

inline void fprint_iv4(FILE* f, ivec4 v, const char* append)
{
	fprintf(f, "(%d, %d, %d, %d)%s", v.x, v.y, v.z, v.w, append);
}

inline int fread_iv4(FILE* f, ivec4* v)
{
	int tmp = fscanf(f, " (%d, %d, %d, %d)", &v->x, &v->y, &v->z, &v->w);
	return (tmp == 4);
}



typedef struct uvec2
{
	unsigned int x;
	unsigned int y;
} uvec2;

inline uvec2 make_uv2(unsigned int x, unsigned int y)
{
	uvec2 v = { x, y };
	return v;
}

inline void fprint_uv2(FILE* f, uvec2 v, const char* append)
{
	fprintf(f, "(%u, %u)%s", v.x, v.y, append);
}

inline int fread_uv2(FILE* f, uvec2* v)
{
	int tmp = fscanf(f, " (%u, %u)", &v->x, &v->y);
	return (tmp == 2);
}


typedef struct uvec3
{
	unsigned int x;
	unsigned int y;
	unsigned int z;
} uvec3;

inline uvec3 make_uv3(unsigned int x, unsigned int y, unsigned int z)
{
	uvec3 v = { x, y, z };
	return v;
}

inline void fprint_uv3(FILE* f, uvec3 v, const char* append)
{
	fprintf(f, "(%u, %u, %u)%s", v.x, v.y, v.z, append);
}

inline int fread_uv3(FILE* f, uvec3* v)
{
	int tmp = fscanf(f, " (%u, %u, %u)", &v->x, &v->y, &v->z);
	return (tmp == 3);
}


typedef struct uvec4
{
	unsigned int x;
	unsigned int y;
	unsigned int z;
	unsigned int w;
} uvec4;

inline uvec4 make_uv4(unsigned int x, unsigned int y, unsigned int z, unsigned int w)
{
	uvec4 v = { x, y, z, w };
	return v;
}

inline void fprint_uv4(FILE* f, uvec4 v, const char* append)
{
	fprintf(f, "(%u, %u, %u, %u)%s", v.x, v.y, v.z, v.w, append);
}

inline int fread_uv4(FILE* f, uvec4* v)
{
	int tmp = fscanf(f, " (%u, %u, %u, %u)", &v->x, &v->y, &v->z, &v->w);
	return (tmp == 4);
}


typedef struct bvec2
{
	u8 x;
	u8 y;
} bvec2;

// TODO What to do here? param type?  enforce 0 or 1?
inline bvec2 make_bv2(int x, int y)
{
	bvec2 v = { !!x, !!y };
	return v;
}

inline void fprint_bv2(FILE* f, bvec2 v, const char* append)
{
	fprintf(f, "(%u, %u)%s", v.x, v.y, append);
}

// Should technically use SCNu8 macro not hhu
inline int fread_bv2(FILE* f, bvec2* v)
{
	int tmp = fscanf(f, " (%hhu, %hhu)", &v->x, &v->y);
	return (tmp == 2);
}


typedef struct bvec3
{
	u8 x;
	u8 y;
	u8 z;
} bvec3;

inline bvec3 make_bv3(int x, int y, int z)
{
	bvec3 v = { !!x, !!y, !!z };
	return v;
}

inline void fprint_bv3(FILE* f, bvec3 v, const char* append)
{
	fprintf(f, "(%u, %u, %u)%s", v.x, v.y, v.z, append);
}

inline int fread_bv3(FILE* f, bvec3* v)
{
	int tmp = fscanf(f, " (%hhu, %hhu, %hhu)", &v->x, &v->y, &v->z);
	return (tmp == 3);
}


typedef struct bvec4
{
	u8 x;
	u8 y;
	u8 z;
	u8 w;
} bvec4;

inline bvec4 make_bv4(int x, int y, int z, int w)
{
	bvec4 v = { !!x, !!y, !!z, !!w };
	return v;
}

inline void fprint_bv4(FILE* f, bvec4 v, const char* append)
{
	fprintf(f, "(%u, %u, %u, %u)%s", v.x, v.y, v.z, v.w, append);
}

inline int fread_bv4(FILE* f, bvec4* v)
{
	int tmp = fscanf(f, " (%hhu, %hhu, %hhu, %hhu)", &v->x, &v->y, &v->z, &v->w);
	return (tmp == 4);
}


inline vec2 v4_to_v2(vec4 a)
{
	vec2 v = { a.x, a.y };
	return v;
}

inline vec3 v4_to_v3(vec4 a)
{
	vec3 v = { a.x, a.y, a.z };
	return v;
}

inline vec2 v4_to_v2h(vec4 a)
{
	vec2 v = { a.x/a.w, a.y/a.w };
	return v;
}

inline vec3 v4_to_v3h(vec4 a)
{
	vec3 v = { a.x/a.w, a.y/a.w, a.z/a.w };
	return v;
}


/* matrices **************/

typedef float mat2[4];
typedef float mat3[9];
typedef float mat4[16];

#define IDENTITY_M2() { 1, 0, 0, 1 }
#define IDENTITY_M3() { 1, 0, 0, 0, 1, 0, 0, 0, 1 }
#define IDENTITY_M4() { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 }
#define SET_IDENTITY_M2(m) \
	do { \
	m[1] = m[2] = 0; \
	m[0] = m[3] = 1; \
	} while (0)

#define SET_IDENTITY_M3(m) \
	do { \
	m[1] = m[2] = m[3] = 0; \
	m[5] = m[6] = m[7] = 0; \
	m[0] = m[4] = m[8] = 1; \
	} while (0)

#define SET_IDENTITY_M4(m) \
	do { \
	m[1] = m[2] = m[3] = m[4] = 0; \
	m[6] = m[7] = m[8] = m[9] = 0; \
	m[11] = m[12] = m[13] = m[14] = 0; \
	m[0] = m[5] = m[10] = m[15] = 1; \
	} while (0)

#ifndef ROW_MAJOR
inline vec2 x_m2(mat2 m) {  return make_v2(m[0], m[2]); }
inline vec2 y_m2(mat2 m) {  return make_v2(m[1], m[3]); }
inline vec2 c1_m2(mat2 m) { return make_v2(m[0], m[1]); }
inline vec2 c2_m2(mat2 m) { return make_v2(m[2], m[3]); }

inline void setc1_m2(mat2 m, vec2 v) { m[0]=v.x, m[1]=v.y; }
inline void setc2_m2(mat2 m, vec2 v) { m[2]=v.x, m[3]=v.y; }

inline void setx_m2(mat2 m, vec2 v) { m[0]=v.x, m[2]=v.y; }
inline void sety_m2(mat2 m, vec2 v) { m[1]=v.x, m[3]=v.y; }
#else
inline vec2 x_m2(mat2 m) {  return make_v2(m[0], m[1]); }
inline vec2 y_m2(mat2 m) {  return make_v2(m[2], m[3]); }
inline vec2 c1_m2(mat2 m) { return make_v2(m[0], m[2]); }
inline vec2 c2_m2(mat2 m) { return make_v2(m[1], m[3]); }

inline void setc1_m2(mat2 m, vec2 v) { m[0]=v.x, m[2]=v.y; }
inline void setc2_m2(mat2 m, vec2 v) { m[1]=v.x, m[3]=v.y; }

inline void setx_m2(mat2 m, vec2 v) { m[0]=v.x, m[1]=v.y; }
inline void sety_m2(mat2 m, vec2 v) { m[2]=v.x, m[3]=v.y; }
#endif


#ifndef ROW_MAJOR
inline vec3 x_m3(mat3 m) {  return make_v3(m[0], m[3], m[6]); }
inline vec3 y_m3(mat3 m) {  return make_v3(m[1], m[4], m[7]); }
inline vec3 z_m3(mat3 m) {  return make_v3(m[2], m[5], m[8]); }
inline vec3 c1_m3(mat3 m) { return make_v3(m[0], m[1], m[2]); }
inline vec3 c2_m3(mat3 m) { return make_v3(m[3], m[4], m[5]); }
inline vec3 c3_m3(mat3 m) { return make_v3(m[6], m[7], m[8]); }

inline void setc1_m3(mat3 m, vec3 v) { m[0]=v.x, m[1]=v.y, m[2]=v.z; }
inline void setc2_m3(mat3 m, vec3 v) { m[3]=v.x, m[4]=v.y, m[5]=v.z; }
inline void setc3_m3(mat3 m, vec3 v) { m[6]=v.x, m[7]=v.y, m[8]=v.z; }

inline void setx_m3(mat3 m, vec3 v) { m[0]=v.x, m[3]=v.y, m[6]=v.z; }
inline void sety_m3(mat3 m, vec3 v) { m[1]=v.x, m[4]=v.y, m[7]=v.z; }
inline void setz_m3(mat3 m, vec3 v) { m[2]=v.x, m[5]=v.y, m[8]=v.z; }
#else
inline vec3 x_m3(mat3 m) {  return make_v3(m[0], m[1], m[2]); }
inline vec3 y_m3(mat3 m) {  return make_v3(m[3], m[4], m[5]); }
inline vec3 z_m3(mat3 m) {  return make_v3(m[6], m[7], m[8]); }
inline vec3 c1_m3(mat3 m) { return make_v3(m[0], m[3], m[6]); }
inline vec3 c2_m3(mat3 m) { return make_v3(m[1], m[4], m[7]); }
inline vec3 c3_m3(mat3 m) { return make_v3(m[2], m[5], m[8]); }

inline void setc1_m3(mat3 m, vec3 v) { m[0]=v.x, m[3]=v.y, m[6]=v.z; }
inline void setc2_m3(mat3 m, vec3 v) { m[1]=v.x, m[4]=v.y, m[7]=v.z; }
inline void setc3_m3(mat3 m, vec3 v) { m[2]=v.x, m[5]=v.y, m[8]=v.z; }

inline void setx_m3(mat3 m, vec3 v) { m[0]=v.x, m[1]=v.y, m[2]=v.z; }
inline void sety_m3(mat3 m, vec3 v) { m[3]=v.x, m[4]=v.y, m[5]=v.z; }
inline void setz_m3(mat3 m, vec3 v) { m[6]=v.x, m[7]=v.y, m[8]=v.z; }
#endif


#ifndef ROW_MAJOR
inline vec4 c1_m4(mat4 m) { return make_v4(m[ 0], m[ 1], m[ 2], m[ 3]); }
inline vec4 c2_m4(mat4 m) { return make_v4(m[ 4], m[ 5], m[ 6], m[ 7]); }
inline vec4 c3_m4(mat4 m) { return make_v4(m[ 8], m[ 9], m[10], m[11]); }
inline vec4 c4_m4(mat4 m) { return make_v4(m[12], m[13], m[14], m[15]); }

inline vec4 x_m4(mat4 m) { return make_v4(m[0], m[4], m[8], m[12]); }
inline vec4 y_m4(mat4 m) { return make_v4(m[1], m[5], m[9], m[13]); }
inline vec4 z_m4(mat4 m) { return make_v4(m[2], m[6], m[10], m[14]); }
inline vec4 w_m4(mat4 m) { return make_v4(m[3], m[7], m[11], m[15]); }

//sets 4th row to 0 0 0 1
inline void setc1_m4v3(mat4 m, vec3 v) { m[ 0]=v.x, m[ 1]=v.y, m[ 2]=v.z, m[ 3]=0; }
inline void setc2_m4v3(mat4 m, vec3 v) { m[ 4]=v.x, m[ 5]=v.y, m[ 6]=v.z, m[ 7]=0; }
inline void setc3_m4v3(mat4 m, vec3 v) { m[ 8]=v.x, m[ 9]=v.y, m[10]=v.z, m[11]=0; }
inline void setc4_m4v3(mat4 m, vec3 v) { m[12]=v.x, m[13]=v.y, m[14]=v.z, m[15]=1; }

inline void setc1_m4v4(mat4 m, vec4 v) { m[ 0]=v.x, m[ 1]=v.y, m[ 2]=v.z, m[ 3]=v.w; }
inline void setc2_m4v4(mat4 m, vec4 v) { m[ 4]=v.x, m[ 5]=v.y, m[ 6]=v.z, m[ 7]=v.w; }
inline void setc3_m4v4(mat4 m, vec4 v) { m[ 8]=v.x, m[ 9]=v.y, m[10]=v.z, m[11]=v.w; }
inline void setc4_m4v4(mat4 m, vec4 v) { m[12]=v.x, m[13]=v.y, m[14]=v.z, m[15]=v.w; }

//sets 4th column to 0 0 0 1
inline void setx_m4v3(mat4 m, vec3 v) { m[0]=v.x, m[4]=v.y, m[ 8]=v.z, m[12]=0; }
inline void sety_m4v3(mat4 m, vec3 v) { m[1]=v.x, m[5]=v.y, m[ 9]=v.z, m[13]=0; }
inline void setz_m4v3(mat4 m, vec3 v) { m[2]=v.x, m[6]=v.y, m[10]=v.z, m[14]=0; }
inline void setw_m4v3(mat4 m, vec3 v) { m[3]=v.x, m[7]=v.y, m[11]=v.z, m[15]=1; }

inline void setx_m4v4(mat4 m, vec4 v) { m[0]=v.x, m[4]=v.y, m[ 8]=v.z, m[12]=v.w; }
inline void sety_m4v4(mat4 m, vec4 v) { m[1]=v.x, m[5]=v.y, m[ 9]=v.z, m[13]=v.w; }
inline void setz_m4v4(mat4 m, vec4 v) { m[2]=v.x, m[6]=v.y, m[10]=v.z, m[14]=v.w; }
inline void setw_m4v4(mat4 m, vec4 v) { m[3]=v.x, m[7]=v.y, m[11]=v.z, m[15]=v.w; }
#else
inline vec4 c1_m4(mat4 m) { return make_v4(m[0], m[4], m[8], m[12]); }
inline vec4 c2_m4(mat4 m) { return make_v4(m[1], m[5], m[9], m[13]); }
inline vec4 c3_m4(mat4 m) { return make_v4(m[2], m[6], m[10], m[14]); }
inline vec4 c4_m4(mat4 m) { return make_v4(m[3], m[7], m[11], m[15]); }

inline vec4 x_m4(mat4 m) { return make_v4(m[0], m[1], m[2], m[3]); }
inline vec4 y_m4(mat4 m) { return make_v4(m[4], m[5], m[6], m[7]); }
inline vec4 z_m4(mat4 m) { return make_v4(m[8], m[9], m[10], m[11]); }
inline vec4 w_m4(mat4 m) { return make_v4(m[12], m[13], m[14], m[15]); }

//sets 4th row to 0 0 0 1
inline void setc1_m4v3(mat4 m, vec3 v) { m[0]=v.x, m[4]=v.y, m[8]=v.z, m[12]=0; }
inline void setc2_m4v3(mat4 m, vec3 v) { m[1]=v.x, m[5]=v.y, m[9]=v.z, m[13]=0; }
inline void setc3_m4v3(mat4 m, vec3 v) { m[2]=v.x, m[6]=v.y, m[10]=v.z, m[14]=0; }
inline void setc4_m4v3(mat4 m, vec3 v) { m[3]=v.x, m[7]=v.y, m[11]=v.z, m[15]=1; }

inline void setc1_m4v4(mat4 m, vec4 v) { m[0]=v.x, m[4]=v.y, m[8]=v.z, m[12]=v.w; }
inline void setc2_m4v4(mat4 m, vec4 v) { m[1]=v.x, m[5]=v.y, m[9]=v.z, m[13]=v.w; }
inline void setc3_m4v4(mat4 m, vec4 v) { m[2]=v.x, m[6]=v.y, m[10]=v.z, m[14]=v.w; }
inline void setc4_m4v4(mat4 m, vec4 v) { m[3]=v.x, m[7]=v.y, m[11]=v.z, m[15]=v.w; }

//sets 4th column to 0 0 0 1
inline void setx_m4v3(mat4 m, vec3 v) { m[0]=v.x, m[1]=v.y, m[2]=v.z, m[3]=0; }
inline void sety_m4v3(mat4 m, vec3 v) { m[4]=v.x, m[5]=v.y, m[6]=v.z, m[7]=0; }
inline void setz_m4v3(mat4 m, vec3 v) { m[8]=v.x, m[9]=v.y, m[10]=v.z, m[11]=0; }
inline void setw_m4v3(mat4 m, vec3 v) { m[12]=v.x, m[13]=v.y, m[14]=v.z, m[15]=1; }

inline void setx_m4v4(mat4 m, vec4 v) { m[0]=v.x, m[1]=v.y, m[2]=v.z, m[3]=v.w; }
inline void sety_m4v4(mat4 m, vec4 v) { m[4]=v.x, m[5]=v.y, m[6]=v.z, m[7]=v.w; }
inline void setz_m4v4(mat4 m, vec4 v) { m[8]=v.x, m[9]=v.y, m[10]=v.z, m[11]=v.w; }
inline void setw_m4v4(mat4 m, vec4 v) { m[12]=v.x, m[13]=v.y, m[14]=v.z, m[15]=v.w; }
#endif


inline void fprint_m2(FILE* f, mat2 m, const char* append)
{
#ifndef ROW_MAJOR
	fprintf(f, "[(%f, %f)\n (%f, %f)]%s",
	        m[0], m[2], m[1], m[3], append);
#else
	fprintf(f, "[(%f, %f)\n (%f, %f)]%s",
	        m[0], m[1], m[2], m[3], append);
#endif
}


inline void fprint_m3(FILE* f, mat3 m, const char* append)
{
#ifndef ROW_MAJOR
	fprintf(f, "[(%f, %f, %f)\n (%f, %f, %f)\n (%f, %f, %f)]%s",
	        m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8], append);
#else
	fprintf(f, "[(%f, %f, %f)\n (%f, %f, %f)\n (%f, %f, %f)]%s",
	        m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], append);
#endif
}

inline void fprint_m4(FILE* f, mat4 m, const char* append)
{
#ifndef ROW_MAJOR
	fprintf(f, "[(%f, %f, %f, %f)\n(%f, %f, %f, %f)\n(%f, %f, %f, %f)\n(%f, %f, %f, %f)]%s",
	        m[0], m[4], m[8], m[12], m[1], m[5], m[9], m[13], m[2], m[6], m[10], m[14],
	        m[3], m[7], m[11], m[15], append);
#else
	fprintf(f, "[(%f, %f, %f, %f)\n(%f, %f, %f, %f)\n(%f, %f, %f, %f)\n(%f, %f, %f, %f)]%s",
	        m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11],
	        m[12], m[13], m[14], m[15], append);
#endif
}

// macros?
inline void print_m2(mat2 m, const char* append)
{
	fprint_m2(stdout, m, append);
}

inline void print_m3(mat3 m, const char* append)
{
	fprint_m3(stdout, m, append);
}

inline void print_m4(mat4 m, const char* append)
{
	fprint_m4(stdout, m, append);
}

//TODO define macros for doing array version
vec2 mult_m2_v2(mat2 m, vec2 v);
vec3 mult_m3_v3(mat3 m, vec3 v);
vec4 mult_m4_v4(mat4 m, vec4 v);

void mult_m2_m2(mat2 c, mat2 a, mat2 b);

void mult_m3_m3(mat3 c, mat3 a, mat3 b);

void mult_m4_m4(mat4 c, mat4 a, mat4 b);

void load_rotation_m2(mat2 mat, float angle);
void load_rotation_m3(mat3 mat, vec3 v, float angle);
void load_rotation_m4(mat4 mat, vec3 vec, float angle);

//void invert_m4(mat4 mInverse, const mat4 m);

void make_perspective_m4(mat4 mat, float fFov, float aspect, float near, float far);
void make_pers_m4(mat4 mat, float z_near, float z_far);

void make_perspective_proj_m4(mat4 mat, float left, float right, float bottom, float top, float near, float far);

void make_orthographic_m4(mat4 mat, float left, float right, float bottom, float top, float near, float far);

void make_viewport_m4(mat4 mat, int x, int y, unsigned int width, unsigned int height, int opengl);

void lookAt(mat4 mat, vec3 eye, vec3 center, vec3 up);


///////////Matrix transformation functions
void scale_m3(mat3 m, float x, float y, float z);
void scale_m4(mat4 m, float x, float y, float z);

// Create a Translation matrix. Only 4x4 matrices have translation components
void translation_m4(mat4 m, float x, float y, float z);

// Extract a rotation matrix from a 4x4 matrix
// Extracts the rotation matrix (3x3) from a 4x4 matrix
void extract_rotation_m4(mat3 dst, mat4 src, int normalize);


// returns float [0,1)
inline float rsw_randf(void)
{
	return rand() / ((float)RAND_MAX + 1.0f);
}

inline float rsw_randf_range(float min, float max)
{
	return min + (max-min) * rsw_randf();
}

inline double rsw_map(double x, double a, double b, double c, double d)
{
	return (x-a)/(b-a) * (d-c) + c;
}

inline float rsw_mapf(float x, float a, float b, float c, float d)
{
	return (x-a)/(b-a) * (d-c) + c;
}


typedef struct Color
{
	u8 r;
	u8 g;
	u8 b;
	u8 a;
} Color;

/*
Color make_Color(void)
{
	r = g = b = 0;
	a = 255;
}
*/

inline Color make_Color(u8 red, u8 green, u8 blue, u8 alpha)
{
	Color c = { red, green, blue, alpha };
	return c;
}

inline void print_Color(Color c, const char* append)
{
	printf("(%d, %d, %d, %d)%s", c.r, c.g, c.b, c.a, append);
}

inline Color v4_to_Color(vec4 v)
{
	//assume all in the range of [0, 1]
	//NOTE(rswinkle): There are other ways of doing the conversion:
	//
	// round like HH: (u8)(v.x * 255.0f + 0.5f)
	// so 0 and 255 get half sized buckets, the rest get [(n-1).5, n.5)
	//
	// allocate equal sized buckets: (u8)(v.x * 256.0f - EPSILON) (where epsilon is eg 0.000001f)
	//
	// But as far as I can tell the spec does it this way
	Color c;
	c.r = v.x * 255.0f;
	c.g = v.y * 255.0f;
	c.b = v.z * 255.0f;
	c.a = v.w * 255.0f;
	return c;
}

inline vec4 Color_to_v4(Color c)
{
	vec4 v = { (float)c.r/255.0f, (float)c.g/255.0f, (float)c.b/255.0f, (float)c.a/255.0f };
	return v;
}

typedef struct Line
{
	float A, B, C;
} Line;

inline Line make_Line(float x1, float y1, float x2, float y2)
{
	Line l;
	l.A = y1 - y2;
	l.B = x2 - x1;
	l.C = x1*y2 - x2*y1;
	return l;
}

inline void normalize_line(Line* line)
{
	// TODO could enforce that n always points toward +y or +x...should I?
	vec2 n = { line->A, line->B };
	float len = len_v2(n);
	line->A /= len;
	line->B /= len;
	line->C /= len;
}

inline float line_func(Line* line, float x, float y)
{
	return line->A*x + line->B*y + line->C;
}
inline float line_findy(Line* line, float x)
{
	return -(line->A*x + line->C)/line->B;
}

inline float line_findx(Line* line, float y)
{
	return -(line->B*y + line->C)/line->A;
}

// return squared distance from c to line segment between a and b
inline float sq_dist_pt_segment2d(vec2 a, vec2 b, vec2 c)
{
	vec2 ab = sub_v2s(b, a);
	vec2 ac = sub_v2s(c, a);
	vec2 bc = sub_v2s(c, b);
	float e = dot_v2s(ac, ab);

	// cases where c projects outside ab
	if (e <= 0.0f) return dot_v2s(ac, ac);
	float f = dot_v2s(ab, ab);
	if (e >= f) return dot_v2s(bc, bc);

	// handle cases where c projects onto ab
	return dot_v2s(ac, ac) - e * e / f;
}

// return t and closest pt on segment ab to c
inline void closest_pt_pt_segment(vec2 c, vec2 a, vec2 b, float* t, vec2* d)
{
	vec2 ab = sub_v2s(b, a);

	// project c onto ab, compute t
	float t_ = dot_v2s(sub_v2s(c, a), ab) / dot_v2s(ab, ab);

	// clamp if outside segment
	if (t_ < 0.0f) t_ = 0.0f;
	if (t_ > 1.0f) t_ = 1.0f;

	// compute projected position
	*d = add_v2s(a, scale_v2(ab, t_));
	*t = t_;
}

inline float closest_pt_pt_segment_t(vec2 c, vec2 a, vec2 b)
{
	vec2 ab = sub_v2s(b, a);

	// project c onto ab, compute t
	float t = dot_v2s(sub_v2s(c, a), ab) / dot_v2s(ab, ab);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	return t;
}

typedef struct Plane
{
	vec3 n;	//normal points x on plane satisfy n dot x = d
	float d; //d = n dot p

} Plane;

/*
Plane(void) {}
Plane(vec3 a, vec3 b, vec3 c)	//ccw winding
{
	n = cross_product(b-a, c-a).norm();
	d = n * a;
}
*/

//int intersect_segment_plane(vec3 a, vec3 b, Plane p, float* t, vec3* q);


// TODO hmm would have to change mat3 and mat4 to proper
// structures to have operators return them since our
// current mat*mat functions take the output mat as a parameter


// For some reason g++ chokes on these operator overloads but they work just
// fine with clang++.  Commented till I figure out what's going on.
/*
#ifdef __cplusplus
inline vec2 operator*(vec2 v, float a) { return scale_v2(v, a); }
inline vec2 operator*(float a, vec2 v) { return scale_v2(v, a); }
inline vec3 operator*(vec3 v, float a) { return scale_v3(v, a); }
inline vec3 operator*(float a, vec3 v) { return scale_v3(v, a); }
inline vec4 operator*(vec4 v, float a) { return scale_v4(v, a); }
inline vec4 operator*(float a, vec4 v) { return scale_v4(v, a); }

inline vec2 operator+(vec2 v1, vec2 v2) { return add_v2s(v1, v2); }
inline vec3 operator+(vec3 v1, vec3 v2) { return add_v3s(v1, v2); }
inline vec4 operator+(vec4 v1, vec4 v2) { return add_v4s(v1, v2); }

inline vec2 operator-(vec2 v1, vec2 v2) { return sub_v2s(v1, v2); }
inline vec3 operator-(vec3 v1, vec3 v2) { return sub_v3s(v1, v2); }
inline vec4 operator-(vec4 v1, vec4 v2) { return sub_v4s(v1, v2); }

inline int operator==(vec2 v1, vec2 v2) { return equal_v2s(v1, v2); }
inline int operator==(vec3 v1, vec3 v2) { return equal_v3s(v1, v2); }
inline int operator==(vec4 v1, vec4 v2) { return equal_v4s(v1, v2); }

inline vec2 operator-(vec2 v) { return neg_v2(v); }
inline vec3 operator-(vec3 v) { return neg_v3(v); }
inline vec4 operator-(vec4 v) { return neg_v4(v); }

inline vec2 operator*(mat2 m, vec2 v) { return mult_m2_v2(m, v); }
inline vec3 operator*(mat3 m, vec3 v) { return mult_m3_v3(m, v); }
inline vec4 operator*(mat4 m, vec4 v) { return mult_m4_v4(m, v); }

#include <iostream>
static inline std::ostream& operator<<(std::ostream& stream, const vec2& a)
{
	return stream <<"("<<a.x<<", "<<a.y<<")";
}
static inline std::ostream& operator<<(std::ostream& stream, const vec3& a)
{
	return stream <<"("<<a.x<<", "<<a.y<<", "<<a.z<<")";
}

static inline std::ostream& operator<<(std::ostream& stream, const vec4& a)
{
	return stream <<"("<<a.x<<", "<<a.y<<", "<<a.z<<", "<<a.w<<")";
}

#endif
*/


// Built-in GLSL functions from Chapter 8 of the GLSLangSpec.3.30.pdf
// Some functionality is included elsewhere in crsw_math (especially
// the geometric functions) and texture lookup functions are in
// gl_glsl.c but this is for the rest of them.  May be moved eventually

// For functions that take 1 float input
#define PGL_VECTORIZE_VEC2(func) \
inline vec2 func##_v2(vec2 v) \
{ \
	return make_v2(func(v.x), func(v.y)); \
}
#define PGL_VECTORIZE_VEC3(func) \
inline vec3 func##_v3(vec3 v) \
{ \
	return make_v3(func(v.x), func(v.y), func(v.z)); \
}
#define PGL_VECTORIZE_VEC4(func) \
inline vec4 func##_v4(vec4 v) \
{ \
	return make_v4(func(v.x), func(v.y), func(v.z), func(v.w)); \
}

#define PGL_VECTORIZE_VEC(func) \
	PGL_VECTORIZE_VEC2(func) \
	PGL_VECTORIZE_VEC3(func) \
	PGL_VECTORIZE_VEC4(func)

#define PGL_STATIC_VECTORIZE_VEC(func) \
static PGL_VECTORIZE_VEC2(func) \
static PGL_VECTORIZE_VEC3(func) \
static PGL_VECTORIZE_VEC4(func)

// for functions that take 2 float inputs and return a float
#define PGL_VECTORIZE2_VEC2(func) \
inline vec2 func##_v2(vec2 a, vec2 b) \
{ \
	return make_v2(func(a.x, b.x), func(a.y, b.y)); \
}
#define PGL_VECTORIZE2_VEC3(func) \
inline vec3 func##_v3(vec3 a, vec3 b) \
{ \
	return make_v3(func(a.x, b.x), func(a.y, b.y), func(a.z, b.z)); \
}
#define PGL_VECTORIZE2_VEC4(func) \
inline vec4 func##_v4(vec4 a, vec4 b) \
{ \
	return make_v4(func(a.x, b.x), func(a.y, b.y), func(a.z, b.z), func(a.w, b.w)); \
}

#define PGL_VECTORIZE2_VEC(func) \
	PGL_VECTORIZE2_VEC2(func) \
	PGL_VECTORIZE2_VEC3(func) \
	PGL_VECTORIZE2_VEC4(func)

#define PGL_STATIC_VECTORIZE2_VEC(func) \
static PGL_VECTORIZE2_VEC2(func) \
static PGL_VECTORIZE2_VEC3(func) \
static PGL_VECTORIZE2_VEC4(func)

// For functions that take 2 float inputs and 1 float control
//  and return a float like mix
#define PGL_VECTORIZE2_1_VEC2(func) \
inline vec2 func##_v2(vec2 a, vec2 b, float c) \
{ \
	return make_v2(func(a.x, b.x, c), func(a.y, b.y, c)); \
}
#define PGL_VECTORIZE2_1_VEC3(func) \
inline vec3 func##_v3(vec3 a, vec3 b, float c) \
{ \
	return make_v3(func(a.x, b.x, c), func(a.y, b.y, c), func(a.z, b.z, c)); \
}
#define PGL_VECTORIZE2_1_VEC4(func) \
inline vec4 func##_v4(vec4 a, vec4 b, float c) \
{ \
	return make_v4(func(a.x, b.x, c), func(a.y, b.y, c), func(a.z, b.z, c), func(a.w, b.w, c)); \
}

#define PGL_VECTORIZE2_1_VEC(func) \
	PGL_VECTORIZE2_1_VEC2(func) \
	PGL_VECTORIZE2_1_VEC3(func) \
	PGL_VECTORIZE2_1_VEC4(func)

#define PGL_STATIC_VECTORIZE2_1_VEC(func) \
static PGL_VECTORIZE2_1_VEC2(func) \
static PGL_VECTORIZE2_1_VEC3(func) \
static PGL_VECTORIZE2_1_VEC4(func)

// for functions that take 1 input and 2 control floats
// and return a float like clamp
#define PGL_VECTORIZE_2_VEC2(func) \
inline vec2 func##_v2(vec2 v, float a, float b) \
{ \
	return make_v2(func(v.x, a, b), func(v.y, a, b)); \
}
#define PGL_VECTORIZE_2_VEC3(func) \
inline vec3 func##_v3(vec3 v, float a, float b) \
{ \
	return make_v3(func(v.x, a, b), func(v.y, a, b), func(v.z, a, b)); \
}
#define PGL_VECTORIZE_2_VEC4(func) \
inline vec4 func##_v4(vec4 v, float a, float b) \
{ \
	return make_v4(func(v.x, a, b), func(v.y, a, b), func(v.z, a, b), func(v.w, a, b)); \
}

#define PGL_VECTORIZE_2_VEC(func) \
	PGL_VECTORIZE_2_VEC2(func) \
	PGL_VECTORIZE_2_VEC3(func) \
	PGL_VECTORIZE_2_VEC4(func)

#define PGL_STATIC_VECTORIZE_2_VEC(func) \
static PGL_VECTORIZE_2_VEC2(func) \
static PGL_VECTORIZE_2_VEC3(func) \
static PGL_VECTORIZE_2_VEC4(func)

// hmm name VECTORIZEI_IVEC2?  suffix is return type?
#define PGL_VECTORIZE_IVEC2(func) \
inline ivec2 func##_iv2(ivec2 v) \
{ \
	return make_iv2(func(v.x), func(v.y)); \
}
#define PGL_VECTORIZE_IVEC3(func) \
inline ivec3 func##_iv3(ivec3 v) \
{ \
	return make_iv3(func(v.x), func(v.y), func(v.z)); \
}
#define PGL_VECTORIZE_IVEC4(func) \
inline ivec4 func##_iv4(ivec4 v) \
{ \
	return make_iv4(func(v.x), func(v.y), func(v.z), func(v.w)); \
}

#define PGL_VECTORIZE_IVEC(func) \
	PGL_VECTORIZE_IVEC2(func) \
	PGL_VECTORIZE_IVEC3(func) \
	PGL_VECTORIZE_IVEC4(func)

#define PGL_VECTORIZE_BVEC2(func) \
inline bvec2 func##_bv2(bvec2 v) \
{ \
	return make_bv2(func(v.x), func(v.y)); \
}
#define PGL_VECTORIZE_BVEC3(func) \
inline bvec3 func##_bv3(bvec3 v) \
{ \
	return make_bv3(func(v.x), func(v.y), func(v.z)); \
}
#define PGL_VECTORIZE_BVEC4(func) \
inline bvec4 func##_bv4(bvec4 v) \
{ \
	return make_bv4(func(v.x), func(v.y), func(v.z), func(v.w)); \
}

#define PGL_VECTORIZE_BVEC(func) \
	PGL_VECTORIZE_BVEC2(func) \
	PGL_VECTORIZE_BVEC3(func) \
	PGL_VECTORIZE_BVEC4(func)

#define PGL_STATIC_VECTORIZE_BVEC(func) \
static PGL_VECTORIZE_BVEC2(func) \
static PGL_VECTORIZE_BVEC3(func) \
static PGL_VECTORIZE_BVEC4(func)

// for functions that take 2 float inputs and return a bool
#define PGL_VECTORIZE2_BVEC2(func) \
inline bvec2 func##_v2(vec2 a, vec2 b) \
{ \
	return make_bv2(func(a.x, b.x), func(a.y, b.y)); \
}
#define PGL_VECTORIZE2_BVEC3(func) \
inline bvec3 func##_v3(vec3 a, vec3 b) \
{ \
	return make_bv3(func(a.x, b.x), func(a.y, b.y), func(a.z, b.z)); \
}
#define PGL_VECTORIZE2_BVEC4(func) \
inline bvec4 func##_v4(vec4 a, vec4 b) \
{ \
	return make_bv4(func(a.x, b.x), func(a.y, b.y), func(a.z, b.z), func(a.w, b.w)); \
}

#define PGL_VECTORIZE2_BVEC(func) \
	PGL_VECTORIZE2_BVEC2(func) \
	PGL_VECTORIZE2_BVEC3(func) \
	PGL_VECTORIZE2_BVEC4(func)

#define PGL_STATIC_VECTORIZE2_BVEC(func) \
static PGL_VECTORIZE2_BVEC2(func) \
static PGL_VECTORIZE2_BVEC3(func) \
static PGL_VECTORIZE2_BVEC4(func)



// 8.1 Angle and Trig Functions
static inline float radiansf(float degrees) { return DEG_TO_RAD(degrees); }
static inline float degreesf(float radians) { return RAD_TO_DEG(radians); }

static inline double radians(double degrees) { return DEG_TO_RAD(degrees); }
static inline double degrees(double radians) { return RAD_TO_DEG(radians); }

PGL_STATIC_VECTORIZE_VEC(radiansf)
PGL_STATIC_VECTORIZE_VEC(degreesf)
PGL_VECTORIZE_VEC(pgl_fast_sin)
PGL_VECTORIZE_VEC(pgl_fast_cos)
PGL_VECTORIZE_VEC(pgl_fast_tan)
PGL_VECTORIZE_VEC(asinf)
PGL_VECTORIZE_VEC(pgl_fast_acos)
PGL_VECTORIZE_VEC(pgl_fast_atan)
PGL_VECTORIZE2_VEC(atan2f)
PGL_VECTORIZE_VEC(sinhf)
PGL_VECTORIZE_VEC(coshf)
PGL_VECTORIZE_VEC(tanhf)
PGL_VECTORIZE_VEC(asinhf)
PGL_VECTORIZE_VEC(acoshf)
PGL_VECTORIZE_VEC(atanhf)

// 8.2 Exponential Functions

static inline float inversesqrtf(float x)
{
	return 1/pgl_fast_sqrt(x);
}

PGL_VECTORIZE2_VEC(powf)
PGL_VECTORIZE_VEC(expf)
PGL_VECTORIZE_VEC(exp2f)
PGL_VECTORIZE_VEC(logf)
PGL_VECTORIZE_VEC(log2f)
PGL_VECTORIZE_VEC(pgl_fast_sqrt)
PGL_STATIC_VECTORIZE_VEC(inversesqrtf)

// 8.3 Common Functions
//
static inline float signf(float x)
{
	if (x > 0.0f) return 1.0f;
	if (x < 0.0f) return -1.0f;
	return 0.0f;
}

static inline float fractf(float x) { return x - floorf(x); }

// GLSL mod() function, can't do modf for float because
// modf is a different standard C function for doubles
// TODO final name?
static inline float modulusf(float x, float y)
{
	return x - y * floorf(x/y);
}

static inline float minf(float x, float y)
{
	return (x < y) ? x : y;
}
static inline float maxf(float x, float y)
{
	return (x > y) ? x : y;
}

static inline float clamp_01(float f)
{
	if (f < 0.0f) return 0.0f;
	if (f > 1.0f) return 1.0f;
	return f;
}

static inline float clamp(float x, float minVal, float maxVal)
{
	if (x < minVal) return minVal;
	if (x > maxVal) return maxVal;
	return x;
}

static inline int clampi(int i, int min, int max)
{
	if (i < min) return min;
	if (i > max) return max;
	return i;
}

static inline float mixf(float x, float y, float a)
{
	return x*(1-a) + y*a;
}

PGL_VECTORIZE_IVEC(abs)
PGL_VECTORIZE_VEC(fabsf)
PGL_STATIC_VECTORIZE_VEC(signf)
PGL_VECTORIZE_VEC(floorf)
PGL_VECTORIZE_VEC(truncf)
PGL_VECTORIZE_VEC(roundf)

// assumes current rounding direction (fegetround/fesetround)
// is nearest in which case nearbyintf rounds to nearest even
#define roundEvenf nearbyintf
PGL_VECTORIZE_VEC(nearbyintf)

PGL_VECTORIZE_VEC(ceilf)
PGL_STATIC_VECTORIZE_VEC(fractf)

PGL_STATIC_VECTORIZE2_VEC(modulusf)
PGL_STATIC_VECTORIZE2_VEC(minf)
PGL_STATIC_VECTORIZE2_VEC(maxf)

PGL_STATIC_VECTORIZE_VEC(clamp_01)
PGL_STATIC_VECTORIZE_2_VEC(clamp)
PGL_STATIC_VECTORIZE2_1_VEC(mixf)

PGL_VECTORIZE_VEC(isnan)
PGL_VECTORIZE_VEC(isinf)


// 8.4 Geometric Functions
// Most of these are elsewhere in the the file
// TODO Where should these go?

static inline float distance_v2(vec2 a, vec2 b)
{
	return len_v2(sub_v2s(a, b));
}
static inline float distance_v3(vec3 a, vec3 b)
{
	return len_v3(sub_v3s(a, b));
}

static inline vec3 reflect_v3(vec3 i, vec3 n)
{
	return sub_v3s(i, scale_v3(n, 2 * dot_v3s(i, n)));
}

static inline float smoothstep(float edge0, float edge1, float x)
{
	float t = clamp_01((x-edge0)/(edge1-edge0));
	return t*t*(3 - 2*t);
}

// 8.5 Matrix Functions
// Again the ones that exist are currently elsewhere

// 8.6 Vector Relational functions

static inline u8 lessThan(float x, float y) { return x < y; }
static inline u8 lessThanEqual(float x, float y) { return x <= y; }
static inline u8 greaterThan(float x, float y) { return x > y; }
static inline u8 greaterThanEqual(float x, float y) { return x >= y; }
static inline u8 equal(float x, float y) { return x == y; }
static inline u8 notEqual(float x, float y) { return x != y; }

//TODO any, all, not

PGL_STATIC_VECTORIZE2_BVEC(lessThan)
PGL_STATIC_VECTORIZE2_BVEC(lessThanEqual)
PGL_STATIC_VECTORIZE2_BVEC(greaterThan)
PGL_STATIC_VECTORIZE2_BVEC(greaterThanEqual)
PGL_STATIC_VECTORIZE2_BVEC(equal)
PGL_STATIC_VECTORIZE2_BVEC(notEqual)

// 8.7 Texture Lookup Functions
// currently in gl_glsl.h/c

#endif



#include <stdint.h>

// References
// https://www.khronos.org/opengl/wiki/OpenGL_Type
// https://registry.khronos.org/EGL/api/KHR/khrplatform.h
// https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/main/xml/gl.xml
//
// NOTES:
// Non-negative is not the same as unsigned
// They use plain int for GLsizei not unsigned like you'd think hence all
// the GL_INVALID_VALUE errors when a GLsizei param is < 0
// Similarly, according to these links, GLsizeiptr is signed
//
// Also, there are some minor/rare contradictions in the links above. They use
// plain int for GLint and GLsizei and unsigned int for GLbitfield but the first
// link above insists all 3 must be 32-bits while the C standard only guarantees
// an int (signed or unsigned) is *at least* 16-bits.  Obviously 16 bit
// architectures are rare and it's probably impossible to run OpenGL on one for
// other reasons, but still, why not use an int32_t/khronos_int32_t in the
// official registry?

typedef uint8_t   GLboolean;
typedef char      GLchar;
typedef int8_t    GLbyte;
typedef uint8_t   GLubyte;
typedef int16_t   GLshort;
typedef uint16_t  GLushort;
typedef int32_t   GLint;
typedef uint32_t  GLuint;
typedef int64_t   GLint64;
typedef uint64_t  GLuint64;

typedef int32_t   GLsizei;

typedef uint32_t  GLenum;
typedef uint32_t  GLbitfield;

typedef intptr_t  GLintptr;
typedef intptr_t  GLsizeiptr;
typedef void      GLvoid;

typedef float     GLfloat;
typedef float     GLclampf;

// not used
typedef double    GLdouble;
typedef double    GLclampd;

#define PGL_UNUSED(var) (void)(var)

enum
{
	//gl error codes
	GL_NO_ERROR = 0,
	GL_INVALID_ENUM,
	GL_INVALID_VALUE,
	GL_INVALID_OPERATION,
	GL_INVALID_FRAMEBUFFER_OPERATION,
	GL_OUT_OF_MEMORY,

	//buffer types (only ARRAY_BUFFER and ELEMENT_ARRAY_BUFFER are currently used)
	GL_ARRAY_BUFFER,
	GL_COPY_READ_BUFFER,
	GL_COPY_WRITE_BUFFER,
	GL_ELEMENT_ARRAY_BUFFER,
	GL_PIXEL_PACK_BUFFER,
	GL_PIXEL_UNPACK_BUFFER,
	GL_TEXTURE_BUFFER,
	GL_TRANSFORM_FEEDBACK_BUFFER,
	GL_UNIFORM_BUFFER,
	GL_NUM_BUFFER_TYPES,

	// Framebuffer stuff (unused/supported yet)
	GL_FRAMEBUFFER,
	GL_DRAW_FRAMEBUFFER,
	GL_READ_FRAMEBUFFER,

	GL_COLOR_ATTACHMENT0,
	GL_COLOR_ATTACHMENT1,
	GL_COLOR_ATTACHMENT2,
	GL_COLOR_ATTACHMENT3,
	GL_COLOR_ATTACHMENT4,
	GL_COLOR_ATTACHMENT5,
	GL_COLOR_ATTACHMENT6,
	GL_COLOR_ATTACHMENT7,

	GL_DEPTH_ATTACHMENT,
	GL_STENCIL_ATTACHMENT,
	GL_DEPTH_STENCIL_ATTACHMENT,

	GL_RENDERBUFFER,

	//buffer use hints (not used currently)
	GL_STREAM_DRAW,
	GL_STREAM_READ,
	GL_STREAM_COPY,
	GL_STATIC_DRAW,
	GL_STATIC_READ,
	GL_STATIC_COPY,
	GL_DYNAMIC_DRAW,
	GL_DYNAMIC_READ,
	GL_DYNAMIC_COPY,

	// mapped buffer access
	GL_READ_ONLY,
	GL_WRITE_ONLY,
	GL_READ_WRITE,

	//polygon modes
	GL_POINT,
	GL_LINE,
	GL_FILL,

	//primitive types
	GL_POINTS,
	GL_LINES,
	GL_LINE_STRIP,
	GL_LINE_LOOP,
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_TRIANGLE_FAN,

	// unsupported primitives because I don't support the geometry shader
	GL_LINE_STRIP_ADJACENCY,
	GL_LINES_ADJACENCY,
	GL_TRIANGLES_ADJACENCY,
	GL_TRIANGLE_STRIP_ADJACENCY,

	//depth functions (and stencil funcs)
	GL_LESS,
	GL_LEQUAL,
	GL_GREATER,
	GL_GEQUAL,
	GL_EQUAL,
	GL_NOTEQUAL,
	GL_ALWAYS,
	GL_NEVER,

	//blend functions
	GL_ZERO,
	GL_ONE,
	GL_SRC_COLOR,
	GL_ONE_MINUS_SRC_COLOR,
	GL_DST_COLOR,
	GL_ONE_MINUS_DST_COLOR,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_DST_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_CONSTANT_COLOR,
	GL_ONE_MINUS_CONSTANT_COLOR,
	GL_CONSTANT_ALPHA,
	GL_ONE_MINUS_CONSTANT_ALPHA,
	GL_SRC_ALPHA_SATURATE,
	NUM_BLEND_FUNCS,
	GL_SRC1_COLOR,
	GL_ONE_MINUS_SRC1_COLOR,
	GL_SRC1_ALPHA,
	GL_ONE_MINUS_SRC1_ALPHA,
	//NUM_BLEND_FUNCS

	//blend equations
	GL_FUNC_ADD,
	GL_FUNC_SUBTRACT,
	GL_FUNC_REVERSE_SUBTRACT,
	GL_MIN,
	GL_MAX,
	NUM_BLEND_EQUATIONS,

	//texture types
	GL_TEXTURE_UNBOUND,
	GL_TEXTURE_1D,
	GL_TEXTURE_2D,
	GL_TEXTURE_3D,
	GL_TEXTURE_1D_ARRAY,
	GL_TEXTURE_2D_ARRAY,
	GL_TEXTURE_RECTANGLE,
	GL_TEXTURE_CUBE_MAP,

	// not needed (just use uniforms (or globals), that's the beauty of
	// software rendering, everything is normal/unified RAM. Also the fact
	// that this is used for both textures and buffers breaks my convenient
	// enum -> bound array index scheme so it would be a pain anyway
	//GL_TEXTURE_BUFFER,

	GL_NUM_TEXTURE_TYPES,
	GL_TEXTURE_CUBE_MAP_POSITIVE_X,
	GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
	GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
	GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
	GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
	GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,

	//texture parameters i
	GL_TEXTURE_BASE_LEVEL,
	GL_TEXTURE_BORDER_COLOR, // doesn't actually do anything
	GL_TEXTURE_COMPARE_FUNC,
	GL_TEXTURE_COMPARE_MODE,
	GL_TEXTURE_LOD_BIAS,
	GL_TEXTURE_MIN_FILTER,
	GL_TEXTURE_MAG_FILTER,
	GL_TEXTURE_MIN_LOD,
	GL_TEXTURE_MAX_LOD,
	GL_TEXTURE_MAX_LEVEL,
	GL_TEXTURE_SWIZZLE_R,
	GL_TEXTURE_SWIZZLE_G,
	GL_TEXTURE_SWIZZLE_B,
	GL_TEXTURE_SWIZZLE_A,
	GL_TEXTURE_SWIZZLE_RGBA,
	GL_TEXTURE_WRAP_S,
	GL_TEXTURE_WRAP_T,
	GL_TEXTURE_WRAP_R,

	//texture parameter values
	// CLAMP_TO_BORDER is an alias to CLAMP_TO_EDGE by default
	// enable it by defining PGL_ENABLE_CLAMP_TO_BORDER
	GL_REPEAT,
	GL_CLAMP_TO_EDGE,
	GL_CLAMP_TO_BORDER,
	GL_MIRRORED_REPEAT,
	GL_NEAREST,
	GL_LINEAR,
	GL_NEAREST_MIPMAP_NEAREST,
	GL_NEAREST_MIPMAP_LINEAR,
	GL_LINEAR_MIPMAP_NEAREST,
	GL_LINEAR_MIPMAP_LINEAR,

	//texture/depth/stencil formats including some from GLES and custom
	PGL_ONE_ALPHA, // Like GL_ALPHA except uses 1's for rgb not 0's

	// From OpenGL ES
	GL_ALPHA, // Fills 0's in for rgb
	GL_LUMINANCE, // used for rgb, fills 1 for alpha
	GL_LUMINANCE_ALPHA, // lum used for rgb

	GL_RED,
	GL_RG,
	GL_RGB,
	GL_BGR,
	GL_RGBA,
	GL_BGRA,
	GL_COMPRESSED_RED,
	GL_COMPRESSED_RG,
	GL_COMPRESSED_RGB,
	GL_COMPRESSED_RGBA,
	//lots more go here but not important

	// None of these are used currently just to help porting
	GL_DEPTH_COMPONENT16,
	GL_DEPTH_COMPONENT24,
	GL_DEPTH_COMPONENT32,
	GL_DEPTH_COMPONENT32F, // PGL uses a float depth buffer

	GL_DEPTH24_STENCIL8,
	GL_DEPTH32F_STENCIL8,  // <- we do this

	GL_STENCIL_INDEX1,
	GL_STENCIL_INDEX4,
	GL_STENCIL_INDEX8,   // this
	GL_STENCIL_INDEX16,

	
	//PixelStore parameters
	GL_UNPACK_ALIGNMENT,
	GL_PACK_ALIGNMENT,

	// Texture units (not used but eases porting)
	// but I'm not doing 80 or bothering with GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS
	GL_TEXTURE0,
	GL_TEXTURE1,
	GL_TEXTURE2,
	GL_TEXTURE3,
	GL_TEXTURE4,
	GL_TEXTURE5,
	GL_TEXTURE6,
	GL_TEXTURE7,
	
	//implemented glEnable options
	GL_CULL_FACE,
	GL_DEPTH_TEST,
	GL_DEPTH_CLAMP,
	GL_LINE_SMOOTH,  // TODO correctly
	GL_BLEND,
	GL_COLOR_LOGIC_OP,
	GL_POLYGON_OFFSET_POINT,
	GL_POLYGON_OFFSET_LINE,
	GL_POLYGON_OFFSET_FILL,
	GL_SCISSOR_TEST,
	GL_STENCIL_TEST,

	//provoking vertex
	GL_FIRST_VERTEX_CONVENTION,
	GL_LAST_VERTEX_CONVENTION,

	//point sprite stuff
	GL_POINT_SPRITE_COORD_ORIGIN,
	GL_UPPER_LEFT,
	GL_LOWER_LEFT,

	//front face determination/culling
	GL_FRONT,
	GL_BACK,
	GL_FRONT_AND_BACK,
	GL_CCW,
	GL_CW,

	// glLogicOp logic ops
	GL_CLEAR,
	GL_SET,
	GL_COPY,
	GL_COPY_INVERTED,
	GL_NOOP,
	GL_AND,
	GL_NAND,
	GL_OR,
	GL_NOR,
	GL_XOR,
	GL_EQUIV,
	GL_AND_REVERSE,
	GL_AND_INVERTED,
	GL_OR_REVERSE,
	GL_OR_INVERTED,
	GL_INVERT,

	// glStencilOp
	GL_KEEP,
	//GL_ZERO, already defined in blend functions aggh
	GL_REPLACE,
	GL_INCR,
	GL_INCR_WRAP,
	GL_DECR,
	GL_DECR_WRAP,
	//GL_INVERT,   // already defined in LogicOps

	//data types
	GL_UNSIGNED_BYTE,
	GL_BYTE,
	GL_UNSIGNED_SHORT,
	GL_SHORT,
	GL_UNSIGNED_INT,
	GL_INT,
	GL_FLOAT,
	GL_DOUBLE,

	GL_BITMAP,  // TODO what is this for?

	//glGetString info
	GL_VENDOR,
	GL_RENDERER,
	GL_VERSION,
	GL_SHADING_LANGUAGE_VERSION,

	// glGet enums
	GL_POLYGON_OFFSET_FACTOR,
	GL_POLYGON_OFFSET_UNITS,
	GL_POINT_SIZE,

	GL_LINE_WIDTH,
	GL_ALIASED_LINE_WIDTH_RANGE,
	GL_SMOOTH_LINE_WIDTH_RANGE,
	GL_SMOOTH_LINE_WIDTH_GRANULARITY,

	GL_DEPTH_CLEAR_VALUE,
	GL_DEPTH_RANGE,
	GL_STENCIL_WRITE_MASK,
	GL_STENCIL_REF,
	GL_STENCIL_VALUE_MASK,
	GL_STENCIL_FUNC,
	GL_STENCIL_FAIL,
	GL_STENCIL_PASS_DEPTH_FAIL,
	GL_STENCIL_PASS_DEPTH_PASS,

	GL_STENCIL_BACK_WRITE_MASK,
	GL_STENCIL_BACK_REF,
	GL_STENCIL_BACK_VALUE_MASK,
	GL_STENCIL_BACK_FUNC,
	GL_STENCIL_BACK_FAIL,
	GL_STENCIL_BACK_PASS_DEPTH_FAIL,
	GL_STENCIL_BACK_PASS_DEPTH_PASS,

	GL_LOGIC_OP_MODE,
	GL_BLEND_SRC_RGB,
	GL_BLEND_SRC_ALPHA,
	GL_BLEND_DST_RGB,
	GL_BLEND_DST_ALPHA,

	GL_BLEND_EQUATION_RGB,
	GL_BLEND_EQUATION_ALPHA,

	GL_CULL_FACE_MODE,
	GL_FRONT_FACE,
	GL_DEPTH_FUNC,
	//GL_POINT_SPRITE_COORD_ORIGIN,
	GL_PROVOKING_VERTEX,

	GL_POLYGON_MODE,

	GL_MAJOR_VERSION,
	GL_MINOR_VERSION,

	GL_TEXTURE_BINDING_1D,
	GL_TEXTURE_BINDING_1D_ARRAY,
	GL_TEXTURE_BINDING_2D,
	GL_TEXTURE_BINDING_2D_ARRAY,

	// Not supported
	GL_TEXTURE_BINDING_2D_MULTISAMPLE,
	GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY,

	GL_TEXTURE_BINDING_3D,
	GL_TEXTURE_BINDING_BUFFER,
	GL_TEXTURE_BINDING_CUBE_MAP,
	GL_TEXTURE_BINDING_RECTANGLE,

	GL_ARRAY_BUFFER_BINDING,
	GL_ELEMENT_ARRAY_BUFFER_BINDING,
	GL_VERTEX_ARRAY_BINDING,
	GL_CURRENT_PROGRAM,

	GL_VIEWPORT,
	GL_SCISSOR_BOX,

	GL_MAX_TEXTURE_BUFFER_SIZE,
	GL_MAX_TEXTURE_IMAGE_UNITS,
	GL_MAX_TEXTURE_LOD_BIAS,
	GL_MAX_TEXTURE_SIZE,
	GL_MAX_3D_TEXTURE_SIZE,
	GL_MAX_ARRAY_TEXTURE_LAYERS,

	// glDebugOutput
	GL_DEBUG_OUTPUT,

	GL_DEBUG_SOURCE_API,
	GL_DEBUG_SOURCE_SHADER_COMPILER,
	GL_DEBUG_SOURCE_WINDOW_SYSTEM,
	GL_DEBUG_SOURCE_THIRD_PARTY,
	GL_DEBUG_SOURCE_APPLICATION,
	GL_DEBUG_SOURCE_OTHER,

	GL_DEBUG_TYPE_ERROR,
	GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR,
	GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR,
	GL_DEBUG_TYPE_PERFORMANCE,
	GL_DEBUG_TYPE_PORTABILITY,
	GL_DEBUG_TYPE_MARKER,
	GL_DEBUG_TYPE_PUSH_GROUP,
	GL_DEBUG_TYPE_POP_GROUP,
	GL_DEBUG_TYPE_OTHER,

	GL_DEBUG_SEVERITY_HIGH,
	GL_DEBUG_SEVERITY_MEDIUM,
	GL_DEBUG_SEVERITY_LOW,
	GL_DEBUG_SEVERITY_NOTIFICATION,

	GL_MAX_DEBUG_MESSAGE_LENGTH,

	//shader types etc. not used, just here for compatibility add what you
	//need so you can use your OpenGL code with PortableGL with minimal changes
	GL_COMPUTE_SHADER,
	GL_VERTEX_SHADER,
	GL_TESS_CONTROL_SHADER,
	GL_TESS_EVALUATION_SHADER,
	GL_GEOMETRY_SHADER,
	GL_FRAGMENT_SHADER,

	GL_INFO_LOG_LENGTH,
	GL_COMPILE_STATUS,
	GL_LINK_STATUS,

	// buffer clearing selections are a mask so can't have overlap
	// choosing arbitrary bits higher than all other constants in enum
	GL_COLOR_BUFFER_BIT = 1 << 10,
	GL_DEPTH_BUFFER_BIT = 1 << 11,
	GL_STENCIL_BUFFER_BIT = 1 << 12
};

#define GL_FALSE 0
#define GL_TRUE 1

#define GL_STENCIL_BITS 8

// Just GL_STENCIL_BITS of 1's, not an official GL enum/value
//#define PGL_STENCIL_MASK ((1 << GL_STENCIL_BITS)-1)
#define PGL_STENCIL_MASK 0xFF

#define GL_MAX_VERTEX_ATTRIBS 8
#define GL_MAX_VERTEX_OUTPUT_COMPONENTS (4*GL_MAX_VERTEX_ATTRIBS)

// Function to set max vertices before init_glContext()
// Must be called before any glContext initialization
#define PGL_TINY_MAX_VERTICES  5000
#define PGL_SMALL_MAX_VERTICES 50000
#define PGL_MED_MAX_VERTICES   100000
#define PGL_BIG_MAX_VERTICES   500000
#define PGL_MAX_MAX_VERTICES   1000000
#define PGL_DEF_MAX_VERTICES   PGL_MED_MAX_VERTICES
void pgl_set_max_vertices(int max_vertices);

// Mostly arbitrarily chosen, some match my AMD/Mesa output, some not really used
#define GL_MAX_DRAW_BUFFERS 4
#define GL_MAX_COLOR_ATTACHMENTS 4

#define PGL_MAX_ALIASED_WIDTH 2048.0f
#define PGL_MAX_TEXTURE_SIZE 16384
#define PGL_MAX_3D_TEXTURE_SIZE 8192
#define PGL_MAX_ARRAY_TEXTURE_LAYERS 8192
#define PGL_MAX_DEBUG_MESSAGE_LENGTH 256

// TODO for now I only support smooth AA lines width 1, so granularity is meaningless
#define PGL_MAX_SMOOTH_WIDTH 1.0f
#define PGL_SMOOTH_GRANULARITY 1.0f

enum { PGL_SMOOTH, PGL_FLAT, PGL_NOPERSPECTIVE };

#define PGL_SMOOTH2 PGL_SMOOTH, PGL_SMOOTH
#define PGL_SMOOTH3 PGL_SMOOTH2, PGL_SMOOTH
#define PGL_SMOOTH4 PGL_SMOOTH3, PGL_SMOOTH

#define PGL_FLAT2 PGL_FLAT, PGL_FLAT
#define PGL_FLAT3 PGL_FLAT2, PGL_FLAT
#define PGL_FLAT4 PGL_FLAT3, PGL_FLAT

#define PGL_NOPERSPECTIVE2 PGL_NOPERSPECTIVE, PGL_NOPERSPECTIVE
#define PGL_NOPERSPECTIVE3 PGL_NOPERSPECTIVE2, PGL_NOPERSPECTIVE
#define PGL_NOPERSPECTIVE4 PGL_NOPERSPECTIVE3, PGL_NOPERSPECTIVE

//TODO NOT USED YET
typedef struct PerVertex {
	vec4 gl_Position;
	float gl_PointSize;
	float gl_ClipDistance[6];
} PerVertex;

// TODO separate structs for vertex and fragment shader builtins?
// input vs output?
typedef struct Shader_Builtins
{
	// vertex inputs
	GLint gl_InstanceID;
	GLint gl_BaseInstance; // 4.6 feature

	// vertex outputs
	vec4 gl_Position;
	//float gl_PointSize;
	//float gl_ClipDistance[6]

	// fragment inputs
	vec4 gl_FragCoord;
	vec2 gl_PointCoord;
	GLboolean gl_FrontFacing;  // struct packing fail I know

	// fragment outputs
	vec4 gl_FragColor;
	//vec4 gl_FragData[GL_MAX_DRAW_BUFFERS];
	float gl_FragDepth;
	GLboolean discard;

} Shader_Builtins;

// TODO GLfloat* and GLvoid*?
typedef void (*vert_func)(float* vs_output, vec4* vertex_attribs, Shader_Builtins* builtins, void* uniforms);
typedef void (*frag_func)(float* fs_input, Shader_Builtins* builtins, void* uniforms);

typedef void (*GLDEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam);

typedef struct glProgram
{
	vert_func vertex_shader;
	frag_func fragment_shader;
	void* uniform;
	GLsizei vs_output_size;
	GLenum interpolation[GL_MAX_VERTEX_OUTPUT_COMPONENTS];

	GLboolean fragdepth_or_discard;

	GLboolean deleted;

} glProgram;

typedef struct glBuffer
{
	/*
	GLenum usage;
	GLenum access;
	GLint access_flags;
	void* map_pointer;
	GLsizei map_offset;
	GLsizei map_length;
	*/

	GLsizei size;
	GLenum type;
	u8* data;

	GLboolean deleted;

	// true if the user uses one of the pgl data extension functions that
	// doesn't copy the data.
	// If true, PGL does not free it when deleting the buffer
	GLboolean user_owned;
} glBuffer;

typedef struct glVertex_Attrib
{
	GLint size;      // number of components 1-4
	GLenum type;     // GL_FLOAT, default
	GLsizei stride;  //
	GLsizeiptr offset;  //
	GLboolean normalized;
	GLuint buf;
	GLboolean enabled;
	GLuint divisor;
} glVertex_Attrib;

typedef struct glVertex_Array
{
	glVertex_Attrib vertex_attribs[GL_MAX_VERTEX_ATTRIBS];

	GLuint element_buffer;
	GLboolean deleted;

} glVertex_Array;

typedef struct glTexture
{
	GLsizei w;
	GLsizei h;
	GLsizei d;

	//GLint base_level;  // Not used
#ifdef PGL_ENABLE_CLAMP_TO_BORDER
	vec4 border_color;
#endif

	GLenum mag_filter;
	GLenum min_filter;
	GLenum wrap_s;
	GLenum wrap_t;
	GLenum wrap_r;

	// TODO?
	//GLenum datatype; // only support GL_UNSIGNED_BYTE so not worth having yet
	GLenum format; // GL_RED, GL_RG, GL_RGB/BGR, GL_RGBA/BGRA
	
	GLenum type; // GL_TEXTURE_UNBOUND, GL_TEXTURE_2D etc.

	GLboolean deleted;

	// TODO same meaning as in glBuffer
	GLboolean user_owned;

	u8* data;
} glTexture;

typedef struct glVertex
{
	vec4 clip_space;
	vec4 screen_space;
	int clip_code;
	int edge_flag;
	float* vs_out;
} glVertex;

typedef struct glFramebuffer
{
	u8* buf;
	u8* lastrow; //better or worse than + h-1 every pixel draw?
	GLsizei w;
	GLsizei h;
} glFramebuffer;

typedef struct Vertex_Shader_output
{
	GLsizei size;
	GLenum* interpolation;

	// TODO Should this be a vector?  or just a pointer?
	// All I currently use is the constructor, reserve and free...
	// I could remove the rest of the cvector_float functions to save on bloat
	// but still easily add back functions as needed...
	//
	// or like comment in init_glContext says just allocate to the max size and be done
	float* output_buf;
} Vertex_Shader_output;


typedef void (*draw_triangle_func)(glVertex* v0, glVertex* v1, glVertex* v2, unsigned int provoke);



#ifndef CVEC_SIZE_T
#include <stdlib.h>
#define CVEC_SIZE_T size_t
#endif

#ifndef CVEC_SZ
#define CVEC_SZ
typedef CVEC_SIZE_T cvec_sz;
#endif


/** Data structure for glVertex_Array vector. */
typedef struct cvector_glVertex_Array
{
	glVertex_Array* a;           /**< Array. */
	cvec_sz size;       /**< Current size (amount you use when manipulating array directly). */
	cvec_sz capacity;   /**< Allocated size of array; always >= size. */
} cvector_glVertex_Array;



extern cvec_sz CVEC_glVertex_Array_SZ;

int cvec_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz size, cvec_sz capacity);
int cvec_init_glVertex_Array(cvector_glVertex_Array* vec, glVertex_Array* vals, cvec_sz num);

cvector_glVertex_Array* cvec_glVertex_Array_heap(cvec_sz size, cvec_sz capacity);
cvector_glVertex_Array* cvec_init_glVertex_Array_heap(glVertex_Array* vals, cvec_sz num);
int cvec_copyc_glVertex_Array(void* dest, void* src);
int cvec_copy_glVertex_Array(cvector_glVertex_Array* dest, cvector_glVertex_Array* src);

int cvec_push_glVertex_Array(cvector_glVertex_Array* vec, glVertex_Array a);
glVertex_Array cvec_pop_glVertex_Array(cvector_glVertex_Array* vec);

int cvec_extend_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz num);
int cvec_insert_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz i, glVertex_Array a);
int cvec_insert_array_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz i, glVertex_Array* a, cvec_sz num);
glVertex_Array cvec_replace_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz i, glVertex_Array a);
void cvec_erase_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz start, cvec_sz end);
int cvec_reserve_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz size);
#define cvec_shrink_to_fit_glVertex_Array(vec) cvec_set_cap_glVertex_Array((vec), (vec)->size)
int cvec_set_cap_glVertex_Array(cvector_glVertex_Array* vec, cvec_sz size);
void cvec_set_val_sz_glVertex_Array(cvector_glVertex_Array* vec, glVertex_Array val);
void cvec_set_val_cap_glVertex_Array(cvector_glVertex_Array* vec, glVertex_Array val);

glVertex_Array* cvec_back_glVertex_Array(cvector_glVertex_Array* vec);

void cvec_clear_glVertex_Array(cvector_glVertex_Array* vec);
void cvec_free_glVertex_Array_heap(void* vec);
void cvec_free_glVertex_Array(void* vec);



/** Data structure for glBuffer vector. */
typedef struct cvector_glBuffer
{
	glBuffer* a;           /**< Array. */
	cvec_sz size;       /**< Current size (amount you use when manipulating array directly). */
	cvec_sz capacity;   /**< Allocated size of array; always >= size. */
} cvector_glBuffer;



extern cvec_sz CVEC_glBuffer_SZ;

int cvec_glBuffer(cvector_glBuffer* vec, cvec_sz size, cvec_sz capacity);
int cvec_init_glBuffer(cvector_glBuffer* vec, glBuffer* vals, cvec_sz num);

cvector_glBuffer* cvec_glBuffer_heap(cvec_sz size, cvec_sz capacity);
cvector_glBuffer* cvec_init_glBuffer_heap(glBuffer* vals, cvec_sz num);
int cvec_copyc_glBuffer(void* dest, void* src);
int cvec_copy_glBuffer(cvector_glBuffer* dest, cvector_glBuffer* src);

int cvec_push_glBuffer(cvector_glBuffer* vec, glBuffer a);
glBuffer cvec_pop_glBuffer(cvector_glBuffer* vec);

int cvec_extend_glBuffer(cvector_glBuffer* vec, cvec_sz num);
int cvec_insert_glBuffer(cvector_glBuffer* vec, cvec_sz i, glBuffer a);
int cvec_insert_array_glBuffer(cvector_glBuffer* vec, cvec_sz i, glBuffer* a, cvec_sz num);
glBuffer cvec_replace_glBuffer(cvector_glBuffer* vec, cvec_sz i, glBuffer a);
void cvec_erase_glBuffer(cvector_glBuffer* vec, cvec_sz start, cvec_sz end);
int cvec_reserve_glBuffer(cvector_glBuffer* vec, cvec_sz size);
#define cvec_shrink_to_fit_glBuffer(vec) cvec_set_cap_glBuffer((vec), (vec)->size)
int cvec_set_cap_glBuffer(cvector_glBuffer* vec, cvec_sz size);
void cvec_set_val_sz_glBuffer(cvector_glBuffer* vec, glBuffer val);
void cvec_set_val_cap_glBuffer(cvector_glBuffer* vec, glBuffer val);

glBuffer* cvec_back_glBuffer(cvector_glBuffer* vec);

void cvec_clear_glBuffer(cvector_glBuffer* vec);
void cvec_free_glBuffer_heap(void* vec);
void cvec_free_glBuffer(void* vec);



/** Data structure for glTexture vector. */
typedef struct cvector_glTexture
{
	glTexture* a;           /**< Array. */
	cvec_sz size;       /**< Current size (amount you use when manipulating array directly). */
	cvec_sz capacity;   /**< Allocated size of array; always >= size. */
} cvector_glTexture;



extern cvec_sz CVEC_glTexture_SZ;

int cvec_glTexture(cvector_glTexture* vec, cvec_sz size, cvec_sz capacity);
int cvec_init_glTexture(cvector_glTexture* vec, glTexture* vals, cvec_sz num);

cvector_glTexture* cvec_glTexture_heap(cvec_sz size, cvec_sz capacity);
cvector_glTexture* cvec_init_glTexture_heap(glTexture* vals, cvec_sz num);
int cvec_copyc_glTexture(void* dest, void* src);
int cvec_copy_glTexture(cvector_glTexture* dest, cvector_glTexture* src);

int cvec_push_glTexture(cvector_glTexture* vec, glTexture a);
glTexture cvec_pop_glTexture(cvector_glTexture* vec);

int cvec_extend_glTexture(cvector_glTexture* vec, cvec_sz num);
int cvec_insert_glTexture(cvector_glTexture* vec, cvec_sz i, glTexture a);
int cvec_insert_array_glTexture(cvector_glTexture* vec, cvec_sz i, glTexture* a, cvec_sz num);
glTexture cvec_replace_glTexture(cvector_glTexture* vec, cvec_sz i, glTexture a);
void cvec_erase_glTexture(cvector_glTexture* vec, cvec_sz start, cvec_sz end);
int cvec_reserve_glTexture(cvector_glTexture* vec, cvec_sz size);
#define cvec_shrink_to_fit_glTexture(vec) cvec_set_cap_glTexture((vec), (vec)->size)
int cvec_set_cap_glTexture(cvector_glTexture* vec, cvec_sz size);
void cvec_set_val_sz_glTexture(cvector_glTexture* vec, glTexture val);
void cvec_set_val_cap_glTexture(cvector_glTexture* vec, glTexture val);

glTexture* cvec_back_glTexture(cvector_glTexture* vec);

void cvec_clear_glTexture(cvector_glTexture* vec);
void cvec_free_glTexture_heap(void* vec);
void cvec_free_glTexture(void* vec);



/** Data structure for glProgram vector. */
typedef struct cvector_glProgram
{
	glProgram* a;           /**< Array. */
	cvec_sz size;       /**< Current size (amount you use when manipulating array directly). */
	cvec_sz capacity;   /**< Allocated size of array; always >= size. */
} cvector_glProgram;



extern cvec_sz CVEC_glProgram_SZ;

int cvec_glProgram(cvector_glProgram* vec, cvec_sz size, cvec_sz capacity);
int cvec_init_glProgram(cvector_glProgram* vec, glProgram* vals, cvec_sz num);

cvector_glProgram* cvec_glProgram_heap(cvec_sz size, cvec_sz capacity);
cvector_glProgram* cvec_init_glProgram_heap(glProgram* vals, cvec_sz num);
int cvec_copyc_glProgram(void* dest, void* src);
int cvec_copy_glProgram(cvector_glProgram* dest, cvector_glProgram* src);

int cvec_push_glProgram(cvector_glProgram* vec, glProgram a);
glProgram cvec_pop_glProgram(cvector_glProgram* vec);

int cvec_extend_glProgram(cvector_glProgram* vec, cvec_sz num);
int cvec_insert_glProgram(cvector_glProgram* vec, cvec_sz i, glProgram a);
int cvec_insert_array_glProgram(cvector_glProgram* vec, cvec_sz i, glProgram* a, cvec_sz num);
glProgram cvec_replace_glProgram(cvector_glProgram* vec, cvec_sz i, glProgram a);
void cvec_erase_glProgram(cvector_glProgram* vec, cvec_sz start, cvec_sz end);
int cvec_reserve_glProgram(cvector_glProgram* vec, cvec_sz size);
#define cvec_shrink_to_fit_glProgram(vec) cvec_set_cap_glProgram((vec), (vec)->size)
int cvec_set_cap_glProgram(cvector_glProgram* vec, cvec_sz size);
void cvec_set_val_sz_glProgram(cvector_glProgram* vec, glProgram val);
void cvec_set_val_cap_glProgram(cvector_glProgram* vec, glProgram val);

glProgram* cvec_back_glProgram(cvector_glProgram* vec);

void cvec_clear_glProgram(cvector_glProgram* vec);
void cvec_free_glProgram_heap(void* vec);
void cvec_free_glProgram(void* vec);



/** Data structure for glVertex vector. */
typedef struct cvector_glVertex
{
	glVertex* a;           /**< Array. */
	cvec_sz size;       /**< Current size (amount you use when manipulating array directly). */
	cvec_sz capacity;   /**< Allocated size of array; always >= size. */
} cvector_glVertex;



extern cvec_sz CVEC_glVertex_SZ;

int cvec_glVertex(cvector_glVertex* vec, cvec_sz size, cvec_sz capacity);
int cvec_init_glVertex(cvector_glVertex* vec, glVertex* vals, cvec_sz num);

cvector_glVertex* cvec_glVertex_heap(cvec_sz size, cvec_sz capacity);
cvector_glVertex* cvec_init_glVertex_heap(glVertex* vals, cvec_sz num);
int cvec_copyc_glVertex(void* dest, void* src);
int cvec_copy_glVertex(cvector_glVertex* dest, cvector_glVertex* src);

int cvec_push_glVertex(cvector_glVertex* vec, glVertex a);
glVertex cvec_pop_glVertex(cvector_glVertex* vec);

int cvec_extend_glVertex(cvector_glVertex* vec, cvec_sz num);
int cvec_insert_glVertex(cvector_glVertex* vec, cvec_sz i, glVertex a);
int cvec_insert_array_glVertex(cvector_glVertex* vec, cvec_sz i, glVertex* a, cvec_sz num);
glVertex cvec_replace_glVertex(cvector_glVertex* vec, cvec_sz i, glVertex a);
void cvec_erase_glVertex(cvector_glVertex* vec, cvec_sz start, cvec_sz end);
int cvec_reserve_glVertex(cvector_glVertex* vec, cvec_sz size);
#define cvec_shrink_to_fit_glVertex(vec) cvec_set_cap_glVertex((vec), (vec)->size)
int cvec_set_cap_glVertex(cvector_glVertex* vec, cvec_sz size);
void cvec_set_val_sz_glVertex(cvector_glVertex* vec, glVertex val);
void cvec_set_val_cap_glVertex(cvector_glVertex* vec, glVertex val);

glVertex* cvec_back_glVertex(cvector_glVertex* vec);

void cvec_clear_glVertex(cvector_glVertex* vec);
void cvec_free_glVertex_heap(void* vec);
void cvec_free_glVertex(void* vec);


typedef struct glContext
{
	mat4 vp_mat;

	// viewport control TODO not currently used internally
	GLint xmin, ymin;
	GLsizei width, height;

	// Always on scissoring (ie screenspace/guardband clipping)
	GLint lx, ly, ux, uy;

	cvector_glVertex_Array vertex_arrays;
	cvector_glBuffer buffers;
	cvector_glTexture textures;
	cvector_glProgram programs;

	// default 0 textures, have to exist per target
	glTexture default_textures[GL_NUM_TEXTURE_TYPES-GL_TEXTURE_UNBOUND-1];

	GLuint cur_vertex_array;
	GLuint bound_buffers[GL_NUM_BUFFER_TYPES-GL_ARRAY_BUFFER];
	GLuint bound_textures[GL_NUM_TEXTURE_TYPES-GL_TEXTURE_UNBOUND-1];
	GLuint cur_texture2D;
	GLuint cur_program;

	GLenum error;
	GLDEBUGPROC dbg_callback;
	GLchar dbg_msg_buf[PGL_MAX_DEBUG_MESSAGE_LENGTH];
	void* dbg_userparam;
	GLboolean dbg_output;

	// TODO make some or all of these locals, measure performance
	// impact. Would be necessary in the long term if I ever
	// parallelize more
	vec4 vertex_attribs_vs[GL_MAX_VERTEX_ATTRIBS];
	Shader_Builtins builtins;
	Vertex_Shader_output vs_output;
	float fs_input[GL_MAX_VERTEX_OUTPUT_COMPONENTS];

	GLboolean depth_test;
	GLboolean line_smooth;
	GLboolean cull_face;
	GLboolean fragdepth_or_discard;
	GLboolean depth_clamp;
	GLboolean depth_mask;
	GLboolean blend;
	GLboolean logic_ops;
	GLboolean poly_offset_pt;
	GLboolean poly_offset_line;
	GLboolean poly_offset_fill;
	GLboolean scissor_test;

	pix_t color_mask;

#ifndef PGL_NO_STENCIL
	GLboolean stencil_test;
	GLuint stencil_writemask;
	GLuint stencil_writemask_back;
	GLint stencil_ref;
	GLint stencil_ref_back;
	GLuint stencil_valuemask;
	GLuint stencil_valuemask_back;
	GLenum stencil_func;
	GLenum stencil_func_back;
	GLenum stencil_sfail;
	GLenum stencil_dpfail;
	GLenum stencil_dppass;
	GLenum stencil_sfail_back;
	GLenum stencil_dpfail_back;
	GLenum stencil_dppass_back;

	GLint clear_stencil;
	glFramebuffer stencil_buf;
#endif

	GLenum logic_func;
	GLenum blend_sRGB;
	GLenum blend_sA;
	GLenum blend_dRGB;
	GLenum blend_dA;
	GLenum blend_eqRGB;
	GLenum blend_eqA;
	GLenum cull_mode;
	GLenum front_face;
	GLenum poly_mode_front;
	GLenum poly_mode_back;
	GLenum depth_func;
	GLenum point_spr_origin;
	GLenum provoking_vert;

	GLfloat poly_factor;
	GLfloat poly_units;

	GLint scissor_lx;
	GLint scissor_ly;
	GLsizei scissor_w;
	GLsizei scissor_h;

	GLint unpack_alignment;
	GLint pack_alignment;

	pix_t clear_color;
	vec4 blend_color;
	GLfloat point_size;
	GLfloat line_width;
	GLfloat clear_depth;
	//GLuint clear_depth;
	GLfloat depth_range_near;
	GLfloat depth_range_far;

	draw_triangle_func draw_triangle_front;
	draw_triangle_func draw_triangle_back;

	// I don't think it's actualy worth ifdef'ing all the depth buffer
	// stuff for PGL_NO_DEPTH_NO_STENCIL. Arguably it wasn't worth it
	// for PGL_NO_STENCIL either but I can always add it later
	glFramebuffer zbuf;
	glFramebuffer back_buffer;

	int user_alloced_backbuf;

	cvector_glVertex glverts;
} glContext;




/*************************************
 *  GLSL(ish) functions
 *************************************/

// Some duplication with crsw_math.h because
// we use these internally and the user can exclude
// those functions (with the official glsl names) to
// avoid clashes
//float clampf_01(float f);
//float clampf(float f, float min, float max);
//int clampi(int i, int min, int max);

//shader texture functions
PGLDEF vec4 texture1D(GLuint tex, float x);
PGLDEF vec4 texture2D(GLuint tex, float x, float y);
PGLDEF vec4 texture3D(GLuint tex, float x, float y, float z);
PGLDEF vec4 texture2DArray(GLuint tex, float x, float y, int z);
PGLDEF vec4 texture_rect(GLuint tex, float x, float y);
PGLDEF vec4 texture_cubemap(GLuint texture, float x, float y, float z);

PGLDEF vec4 texelFetch1D(GLuint tex, int x, int lod);
PGLDEF vec4 texelFetch2D(GLuint tex, int x, int y, int lod);
PGLDEF vec4 texelFetch3D(GLuint tex, int x, int y, int z, int lod);

typedef struct pgl_uniforms
{
	mat4 mvp_mat;
	mat4 mv_mat;
	mat4 p_mat;
	mat3 normal_mat;
	vec4 color;

	GLuint tex0;
	vec3 light_pos;
} pgl_uniforms;

typedef struct pgl_prog_info
{
	vert_func vs;
	frag_func fs;
	int vs_out_sz;
	GLenum interp[GL_MAX_VERTEX_OUTPUT_COMPONENTS];
	GLboolean uses_fragdepth_or_discard;
} pgl_prog_info;

enum {
	PGL_ATTR_VERT,
	PGL_ATTR_COLOR,
	PGL_ATTR_NORMAL,
	PGL_ATTR_TEXCOORD0,
	PGL_ATTR_TEXCOORD1
};

enum {
	PGL_SHADER_IDENTITY,
	PGL_SHADER_FLAT,
	PGL_SHADER_SHADED,
	PGL_SHADER_DFLT_LIGHT,
	PGL_SHADER_POINT_LIGHT_DIFF,
	PGL_SHADER_TEX_REPLACE,
	PGL_SHADER_TEX_MODULATE,
	PGL_SHADER_TEX_POINT_LIGHT_DIFF,
	PGL_SHADER_TEX_RECT_REPLACE,
	PGL_NUM_SHADERS
};

PGLDEF void pgl_init_std_shaders(GLuint programs[PGL_NUM_SHADERS]);


// TODO leave these non gl* functions here?  prefix with pgl?
PGLDEF GLboolean init_glContext(glContext* c, pix_t** back_buffer, GLsizei width, GLsizei height);
PGLDEF void free_glContext(glContext* context);
PGLDEF void set_glContext(glContext* context);

PGLDEF GLboolean pglResizeFramebuffer(GLsizei width, GLsizei height);

PGLDEF void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);

PGLDEF void glDebugMessageCallback(GLDEBUGPROC callback, void* userParam);

PGLDEF GLubyte* glGetString(GLenum name);
PGLDEF GLenum glGetError(void);
PGLDEF void glGetBooleanv(GLenum pname, GLboolean* data);
PGLDEF void glGetFloatv(GLenum pname, GLfloat* data);
PGLDEF void glGetIntegerv(GLenum pname, GLint* data);
PGLDEF GLboolean glIsEnabled(GLenum cap);
PGLDEF GLboolean glIsProgram(GLuint program);

PGLDEF void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
PGLDEF void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
PGLDEF void glClearDepthf(GLfloat depth);
PGLDEF void glClearDepth(GLdouble depth);
PGLDEF void glDepthFunc(GLenum func);
PGLDEF void glDepthRangef(GLfloat nearVal, GLfloat farVal);
PGLDEF void glDepthRange(GLdouble nearVal, GLdouble farVal);
PGLDEF void glDepthMask(GLboolean flag);
PGLDEF void glBlendFunc(GLenum sfactor, GLenum dfactor);
PGLDEF void glBlendEquation(GLenum mode);
PGLDEF void glBlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha, GLenum dstAlpha);
PGLDEF void glBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha);
PGLDEF void glBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
PGLDEF void glClear(GLbitfield mask);
PGLDEF void glProvokingVertex(GLenum provokeMode);
PGLDEF void glEnable(GLenum cap);
PGLDEF void glDisable(GLenum cap);
PGLDEF void glCullFace(GLenum mode);
PGLDEF void glFrontFace(GLenum mode);
PGLDEF void glPolygonMode(GLenum face, GLenum mode);
PGLDEF void glPointSize(GLfloat size);
PGLDEF void glPointParameteri(GLenum pname, GLint param);
PGLDEF void glLineWidth(GLfloat width);
PGLDEF void glLogicOp(GLenum opcode);
PGLDEF void glPolygonOffset(GLfloat factor, GLfloat units);
PGLDEF void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
#ifndef PGL_NO_STENCIL
PGLDEF void glStencilFunc(GLenum func, GLint ref, GLuint mask);
PGLDEF void glStencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
PGLDEF void glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass);
PGLDEF void glStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass);
PGLDEF void glClearStencil(GLint s);
PGLDEF void glStencilMask(GLuint mask);
PGLDEF void glStencilMaskSeparate(GLenum face, GLuint mask);
#endif

// textures
PGLDEF void glGenTextures(GLsizei n, GLuint* textures);
PGLDEF void glDeleteTextures(GLsizei n, const GLuint* textures);
PGLDEF void glBindTexture(GLenum target, GLuint texture);

PGLDEF void glTexParameteri(GLenum target, GLenum pname, GLint param);
PGLDEF void glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params);
PGLDEF void glTexParameteriv(GLenum target, GLenum pname, const GLint* params);
PGLDEF void glTextureParameteri(GLuint texture, GLenum pname, GLint param);
PGLDEF void glTextureParameterfv(GLuint texture, GLenum pname, const GLfloat* params);
PGLDEF void glTextureParameteriv(GLuint texture, GLenum pname, const GLint* params);

PGLDEF void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params);
PGLDEF void glGetTexParameteriv(GLenum target, GLenum pname, GLint* params);
PGLDEF void glGetTexParameterIiv(GLenum target, GLenum pname, GLint* params);
PGLDEF void glGetTexParameterIuiv(GLenum target, GLenum pname, GLuint* params);
PGLDEF void glGetTextureParameterfv(GLuint texture, GLenum pname, GLfloat* params);
PGLDEF void glGetTextureParameteriv(GLuint texture, GLenum pname, GLint* params);
PGLDEF void glGetTextureParameterIiv(GLuint texture, GLenum pname, GLint* params);
PGLDEF void glGetTextureParameterIuiv(GLuint texture, GLenum pname, GLuint* params);

PGLDEF void glPixelStorei(GLenum pname, GLint param);
PGLDEF void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid* data);
PGLDEF void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* data);
PGLDEF void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid* data);

PGLDEF void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid* data);
PGLDEF void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid* data);
PGLDEF void glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLenum type, const GLvoid* data);


PGLDEF void glGenVertexArrays(GLsizei n, GLuint* arrays);
PGLDEF void glDeleteVertexArrays(GLsizei n, const GLuint* arrays);
PGLDEF void glBindVertexArray(GLuint array);
PGLDEF void glGenBuffers(GLsizei n, GLuint* buffers);
PGLDEF void glDeleteBuffers(GLsizei n, const GLuint* buffers);
PGLDEF void glBindBuffer(GLenum target, GLuint buffer);
PGLDEF void glBufferData(GLenum target, GLsizeiptr size, const GLvoid* data, GLenum usage);
PGLDEF void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid* data);
PGLDEF void* glMapBuffer(GLenum target, GLenum access);
PGLDEF void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid* pointer);
PGLDEF void glVertexAttribDivisor(GLuint index, GLuint divisor);
PGLDEF void glEnableVertexAttribArray(GLuint index);
PGLDEF void glDisableVertexAttribArray(GLuint index);
PGLDEF void glDrawArrays(GLenum mode, GLint first, GLsizei count);
PGLDEF void glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei drawcount);
PGLDEF void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices);
PGLDEF void glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type, const GLvoid* const* indices, GLsizei drawcount);
PGLDEF void glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei primcount);
PGLDEF void glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei primcount, GLuint baseinstance);
PGLDEF void glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices, GLsizei primcount);
PGLDEF void glDrawElementsInstancedBaseInstance(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices, GLsizei primcount, GLuint baseinstance);

//DSA functions (from OpenGL 4.5+)
#define glCreateBuffers(n, buffers) glGenBuffers(n, buffers)
PGLDEF void glNamedBufferData(GLuint buffer, GLsizeiptr size, const GLvoid* data, GLenum usage);
PGLDEF void glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const GLvoid* data);
PGLDEF void* glMapNamedBuffer(GLuint buffer, GLenum access);
PGLDEF void glCreateTextures(GLenum target, GLsizei n, GLuint* textures);

PGLDEF void glEnableVertexArrayAttrib(GLuint vaobj, GLuint index);
PGLDEF void glDisableVertexArrayAttrib(GLuint vaobj, GLuint index);


//shaders
PGLDEF GLuint pglCreateProgram(vert_func vertex_shader, frag_func fragment_shader, GLsizei n, GLenum* interpolation, GLboolean fragdepth_or_discard);
PGLDEF void glDeleteProgram(GLuint program);
PGLDEF void glUseProgram(GLuint program);

// These are here, not in pgl_ext.h/c because they take the place of standard OpenGL
// functions glUniform*() and glProgramUniform*()
PGLDEF void pglSetUniform(void* uniform);
PGLDEF void pglSetProgramUniform(GLuint program, void* uniform);



#ifndef PGL_EXCLUDE_STUBS

// Stubs to let real OpenGL libs compile with minimal modifications/ifdefs
// add what you need
//
PGLDEF const GLubyte* glGetStringi(GLenum name, GLuint index);

PGLDEF void glColorMaski(GLuint buf, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);

PGLDEF void glGenerateMipmap(GLenum target);
PGLDEF void glActiveTexture(GLenum texture);

PGLDEF void glTexParameterf(GLenum target, GLenum pname, GLfloat param);

PGLDEF void glTextureParameterf(GLuint texture, GLenum pname, GLfloat param);

// TODO what the heck are these?
PGLDEF void glTexParameterliv(GLenum target, GLenum pname, const GLint* params);
PGLDEF void glTexParameterluiv(GLenum target, GLenum pname, const GLuint* params);

PGLDEF void glTextureParameterliv(GLuint texture, GLenum pname, const GLint* params);
PGLDEF void glTextureParameterluiv(GLuint texture, GLenum pname, const GLuint* params);

PGLDEF void glCompressedTexImage1D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLint border, GLsizei imageSize, const GLvoid* data);
PGLDEF void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid* data);
PGLDEF void glCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLsizei imageSize, const GLvoid* data);

PGLDEF void glTexBuffer(GLenum target, GLenum internalformat, GLuint buffer);
PGLDEF void glTextureBuffer(GLuint texture, GLenum internalformat, GLuint buffer);

PGLDEF void glGetDoublev(GLenum pname, GLdouble* params);
PGLDEF void glGetInteger64v(GLenum pname, GLint64* params);

// Draw buffers
PGLDEF void glDrawBuffers(GLsizei n, const GLenum* bufs);
PGLDEF void glNamedFramebufferDrawBuffers(GLuint framebuffer, GLsizei n, const GLenum* bufs);

// Framebuffers/Renderbuffers
PGLDEF void glGenFramebuffers(GLsizei n, GLuint* ids);
PGLDEF void glBindFramebuffer(GLenum target, GLuint framebuffer);
PGLDEF void glDeleteFramebuffers(GLsizei n, GLuint* framebuffers);
PGLDEF void glFramebufferTexture(GLenum target, GLenum attachment, GLuint texture, GLint level);

PGLDEF void glFramebufferTexture1D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
PGLDEF void glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
PGLDEF void glFramebufferTexture3D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint layer);
PGLDEF GLboolean glIsFramebuffer(GLuint framebuffer);

PGLDEF void glFramebufferTextureLayer(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer);
PGLDEF void glNamedFramebufferTextureLayer(GLuint framebuffer, GLenum attachment, GLuint texture, GLint level, GLint layer);

PGLDEF void glReadBuffer(GLenum mode);
PGLDEF void glNamedFramebufferReadBuffer(GLuint framebuffer, GLenum mode);

PGLDEF void glBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
PGLDEF void glBlitNamedFramebuffer(GLuint readFramebuffer, GLuint drawFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);

PGLDEF void glGenRenderbuffers(GLsizei n, GLuint* renderbuffers);
PGLDEF void glBindRenderbuffer(GLenum target, GLuint renderbuffer);
PGLDEF void glDeleteRenderbuffers(GLsizei n, const GLuint* renderbuffers);
PGLDEF void glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
GLboolean glIsRenderbuffer(GLuint renderbuffer);
PGLDEF void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
GLenum glCheckFramebufferStatus(GLenum target);

PGLDEF void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
PGLDEF void glNamedRenderbufferStorageMultisample(GLuint renderbuffer, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);

PGLDEF void glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint* value);
PGLDEF void glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint* value);
PGLDEF void glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value);
PGLDEF void glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil);
PGLDEF void glClearNamedFramebufferiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLint* value);
PGLDEF void glClearNamedFramebufferuiv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLuint* value);
PGLDEF void glClearNamedFramebufferfv(GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat* value);
PGLDEF void glClearNamedFramebufferfi(GLuint framebuffer, GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil);


PGLDEF void glGetProgramiv(GLuint program, GLenum pname, GLint* params);
PGLDEF void glGetProgramInfoLog(GLuint program, GLsizei maxLength, GLsizei* length, GLchar* infoLog);
PGLDEF void glAttachShader(GLuint program, GLuint shader);
PGLDEF void glCompileShader(GLuint shader);
PGLDEF void glGetShaderInfoLog(GLuint shader, GLsizei maxLength, GLsizei* length, GLchar* infoLog);

// use pglCreateProgram()
PGLDEF GLuint glCreateProgram(void);

PGLDEF void glLinkProgram(GLuint program);
PGLDEF void glShaderSource(GLuint shader, GLsizei count, const GLchar** string, const GLint* length);
PGLDEF void glGetShaderiv(GLuint shader, GLenum pname, GLint* params);
PGLDEF GLuint glCreateShader(GLenum shaderType);
PGLDEF void glDeleteShader(GLuint shader);
PGLDEF void glDetachShader(GLuint program, GLuint shader);

PGLDEF GLint glGetUniformLocation(GLuint program, const GLchar* name);
PGLDEF GLint glGetAttribLocation(GLuint program, const GLchar* name);

PGLDEF GLboolean glUnmapBuffer(GLenum target);
PGLDEF GLboolean glUnmapNamedBuffer(GLuint buffer);

PGLDEF void glUniform1f(GLint location, GLfloat v0);
PGLDEF void glUniform2f(GLint location, GLfloat v0, GLfloat v1);
PGLDEF void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
PGLDEF void glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);

PGLDEF void glUniform1i(GLint location, GLint v0);
PGLDEF void glUniform2i(GLint location, GLint v0, GLint v1);
PGLDEF void glUniform3i(GLint location, GLint v0, GLint v1, GLint v2);
PGLDEF void glUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);

PGLDEF void glUniform1ui(GLint location, GLuint v0);
PGLDEF void glUniform2ui(GLint location, GLuint v0, GLuint v1);
PGLDEF void glUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2);
PGLDEF void glUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3);

PGLDEF void glUniform1fv(GLint location, GLsizei count, const GLfloat* value);
PGLDEF void glUniform2fv(GLint location, GLsizei count, const GLfloat* value);
PGLDEF void glUniform3fv(GLint location, GLsizei count, const GLfloat* value);
PGLDEF void glUniform4fv(GLint location, GLsizei count, const GLfloat* value);

PGLDEF void glUniform1iv(GLint location, GLsizei count, const GLint* value);
PGLDEF void glUniform2iv(GLint location, GLsizei count, const GLint* value);
PGLDEF void glUniform3iv(GLint location, GLsizei count, const GLint* value);
PGLDEF void glUniform4iv(GLint location, GLsizei count, const GLint* value);

PGLDEF void glUniform1uiv(GLint location, GLsizei count, const GLuint* value);
PGLDEF void glUniform2uiv(GLint location, GLsizei count, const GLuint* value);
PGLDEF void glUniform3uiv(GLint location, GLsizei count, const GLuint* value);
PGLDEF void glUniform4uiv(GLint location, GLsizei count, const GLuint* value);

PGLDEF void glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
PGLDEF void glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
PGLDEF void glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
PGLDEF void glUniformMatrix2x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
PGLDEF void glUniformMatrix3x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
PGLDEF void glUniformMatrix2x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
PGLDEF void glUniformMatrix4x2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
PGLDEF void glUniformMatrix3x4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
PGLDEF void glUniformMatrix4x3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);

#endif


// Modeled after SDL for RenderGeometry
// Color c like SDL or vec4 c?
typedef struct pgl_vertex
{
	vec2 pos;
	Color color;
	vec2 tex_coord;
} pgl_vertex;


// TODO use ints like SDL or keep floats?
typedef struct pgl_fill_data
{
	vec2 dst;
	Color c;
} pgl_fill_data;

typedef struct pgl_copy_data
{
	vec2 src;
	vec2 dst;
	Color c;
} pgl_copy_data;

PGLDEF void pglClearScreen(void);

//This isn't possible in regular OpenGL, changing the interpolation of vs output of
//an existing shader.  You'd have to switch between 2 almost identical shaders.
PGLDEF void pglSetInterp(GLsizei n, GLenum* interpolation);

#define pglVertexAttribPointer(index, size, type, normalized, stride, offset) \
glVertexAttribPointer(index, size, type, normalized, stride, (void*)(offset))


PGLDEF GLuint pglCreateFragProgram(frag_func fragment_shader, GLboolean fragdepth_or_discard);

//TODO
//pglDrawRect(x, y, w, h)
//pglDrawPoint(x, y)
PGLDEF void pglDrawFrame(void);
PGLDEF void pglDrawFrame2(frag_func frag_shader, void* uniforms);

// TODO should these be called pglMapped* since that's what they do?  I don't think so, since it's too different from actual spec for mapped buffers
PGLDEF void pglBufferData(GLenum target, GLsizei size, const GLvoid* data, GLenum usage);
PGLDEF void pglTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid* data);

PGLDEF void pglTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* data);

PGLDEF void pglTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid* data);
PGLDEF void pglTextureImage1D(GLuint texture, GLint level, GLint internalformat, GLsizei width, GLint border, GLenum format, GLenum type, const GLvoid* data);

PGLDEF void pglTextureImage2D(GLuint texture, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid* data);

PGLDEF void pglTextureImage3D(GLuint texture, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type, const GLvoid* data);

// I could make these return the data?
PGLDEF void pglGetBufferData(GLuint buffer, GLvoid** data);
PGLDEF void pglGetTextureData(GLuint texture, GLvoid** data);

GLvoid* pglGetBackBuffer(void);
PGLDEF void pglSetBackBuffer(GLvoid* backbuf, GLsizei w, GLsizei h, GLboolean user_owned);
PGLDEF void pglSetTexBackBuffer(GLuint texture);


PGLDEF u8* convert_format_to_packed_rgba(u8* output, u8* input, int w, int h, int pitch, GLenum format);
PGLDEF u8* convert_grayscale_to_rgba(u8* input, int size, u32 bg_rgba, u32 text_rgba);

PGLDEF int setup_default_textures(void);

PGLDEF void put_pixel(Color color, int x, int y);
PGLDEF void put_pixel_blend(vec4 src, int x, int y);

//Should I have it take a glFramebuffer as paramater?
PGLDEF void put_line(Color the_color, float x1, float y1, float x2, float y2);
PGLDEF void put_wide_line_simple(Color the_color, float width, float x1, float y1, float x2, float y2);
PGLDEF void put_wide_line(Color color1, Color color2, float width, float x1, float y1, float x2, float y2);

PGLDEF void put_triangle(Color c1, Color c2, Color c3, vec2 p1, vec2 p2, vec2 p3);
PGLDEF void put_triangle_tex(int tex, vec2 uv1, vec2 uv2, vec2 uv3, vec2 p1, vec2 p2, vec2 p3);
PGLDEF void pgl_draw_geometry_raw(int tex, const float* xy, int xy_stride, const Color* color, int color_stride, const float* uv, int uv_stride, int n_verts, const void* indices, int n_indices, int sz_indices);

PGLDEF void put_aa_line(vec4 c, float x1, float y1, float x2, float y2);
PGLDEF void put_aa_line_interp(vec4 c1, vec4 c2, float x1, float y1, float x2, float y2);


#ifdef __cplusplus
}
#endif

// end GL_H
#endif
