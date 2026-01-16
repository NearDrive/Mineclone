#include <glad/glad.h>
#include <stdio.h>

int GLAD_GL_VERSION_4_5 = 0;

PFNGLGETSTRINGPROC glad_glGetString = NULL;
PFNGLCLEARCOLORPROC glad_glClearColor = NULL;
PFNGLCLEARPROC glad_glClear = NULL;
PFNGLENABLEPROC glad_glEnable = NULL;
PFNGLDISABLEPROC glad_glDisable = NULL;
PFNGLCULLFACEPROC glad_glCullFace = NULL;
PFNGLFRONTFACEPROC glad_glFrontFace = NULL;
PFNGLVIEWPORTPROC glad_glViewport = NULL;
PFNGLCREATESHADERPROC glad_glCreateShader = NULL;
PFNGLSHADERSOURCEPROC glad_glShaderSource = NULL;
PFNGLCOMPILESHADERPROC glad_glCompileShader = NULL;
PFNGLGETSHADERIVPROC glad_glGetShaderiv = NULL;
PFNGLGETSHADERINFOLOGPROC glad_glGetShaderInfoLog = NULL;
PFNGLCREATEPROGRAMPROC glad_glCreateProgram = NULL;
PFNGLATTACHSHADERPROC glad_glAttachShader = NULL;
PFNGLLINKPROGRAMPROC glad_glLinkProgram = NULL;
PFNGLGETPROGRAMIVPROC glad_glGetProgramiv = NULL;
PFNGLGETPROGRAMINFOLOGPROC glad_glGetProgramInfoLog = NULL;
PFNGLDELETESHADERPROC glad_glDeleteShader = NULL;
PFNGLUSEPROGRAMPROC glad_glUseProgram = NULL;
PFNGLDELETEPROGRAMPROC glad_glDeleteProgram = NULL;
PFNGLGENVERTEXARRAYSPROC glad_glGenVertexArrays = NULL;
PFNGLBINDVERTEXARRAYPROC glad_glBindVertexArray = NULL;
PFNGLGENBUFFERSPROC glad_glGenBuffers = NULL;
PFNGLBINDBUFFERPROC glad_glBindBuffer = NULL;
PFNGLBUFFERDATAPROC glad_glBufferData = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC glad_glEnableVertexAttribArray = NULL;
PFNGLVERTEXATTRIBPOINTERPROC glad_glVertexAttribPointer = NULL;
PFNGLGETUNIFORMLOCATIONPROC glad_glGetUniformLocation = NULL;
PFNGLUNIFORMMATRIX4FVPROC glad_glUniformMatrix4fv = NULL;
PFNGLDRAWARRAYSPROC glad_glDrawArrays = NULL;
PFNGLDELETEVERTEXARRAYSPROC glad_glDeleteVertexArrays = NULL;
PFNGLDELETEBUFFERSPROC glad_glDeleteBuffers = NULL;
PFNGLGETINTEGERVPROC glad_glGetIntegerv = NULL;
PFNGLDEBUGMESSAGECALLBACKPROC glad_glDebugMessageCallback = NULL;
PFNGLDEBUGMESSAGECONTROLPROC glad_glDebugMessageControl = NULL;

static void *glad_get_proc(GLADloadproc load, const char *name) {
    return (void *)load(name);
}

int gladLoadGLLoader(GLADloadproc load) {
    if (!load) {
        return 0;
    }

    glad_glGetString = (PFNGLGETSTRINGPROC)glad_get_proc(load, "glGetString");
    glad_glClearColor = (PFNGLCLEARCOLORPROC)glad_get_proc(load, "glClearColor");
    glad_glClear = (PFNGLCLEARPROC)glad_get_proc(load, "glClear");
    glad_glEnable = (PFNGLENABLEPROC)glad_get_proc(load, "glEnable");
    glad_glDisable = (PFNGLDISABLEPROC)glad_get_proc(load, "glDisable");
    glad_glCullFace = (PFNGLCULLFACEPROC)glad_get_proc(load, "glCullFace");
    glad_glFrontFace = (PFNGLFRONTFACEPROC)glad_get_proc(load, "glFrontFace");
    glad_glViewport = (PFNGLVIEWPORTPROC)glad_get_proc(load, "glViewport");
    glad_glCreateShader = (PFNGLCREATESHADERPROC)glad_get_proc(load, "glCreateShader");
    glad_glShaderSource = (PFNGLSHADERSOURCEPROC)glad_get_proc(load, "glShaderSource");
    glad_glCompileShader = (PFNGLCOMPILESHADERPROC)glad_get_proc(load, "glCompileShader");
    glad_glGetShaderiv = (PFNGLGETSHADERIVPROC)glad_get_proc(load, "glGetShaderiv");
    glad_glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)glad_get_proc(load, "glGetShaderInfoLog");
    glad_glCreateProgram = (PFNGLCREATEPROGRAMPROC)glad_get_proc(load, "glCreateProgram");
    glad_glAttachShader = (PFNGLATTACHSHADERPROC)glad_get_proc(load, "glAttachShader");
    glad_glLinkProgram = (PFNGLLINKPROGRAMPROC)glad_get_proc(load, "glLinkProgram");
    glad_glGetProgramiv = (PFNGLGETPROGRAMIVPROC)glad_get_proc(load, "glGetProgramiv");
    glad_glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)glad_get_proc(load, "glGetProgramInfoLog");
    glad_glDeleteShader = (PFNGLDELETESHADERPROC)glad_get_proc(load, "glDeleteShader");
    glad_glUseProgram = (PFNGLUSEPROGRAMPROC)glad_get_proc(load, "glUseProgram");
    glad_glDeleteProgram = (PFNGLDELETEPROGRAMPROC)glad_get_proc(load, "glDeleteProgram");
    glad_glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)glad_get_proc(load, "glGenVertexArrays");
    glad_glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)glad_get_proc(load, "glBindVertexArray");
    glad_glGenBuffers = (PFNGLGENBUFFERSPROC)glad_get_proc(load, "glGenBuffers");
    glad_glBindBuffer = (PFNGLBINDBUFFERPROC)glad_get_proc(load, "glBindBuffer");
    glad_glBufferData = (PFNGLBUFFERDATAPROC)glad_get_proc(load, "glBufferData");
    glad_glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)glad_get_proc(load, "glEnableVertexAttribArray");
    glad_glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)glad_get_proc(load, "glVertexAttribPointer");
    glad_glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)glad_get_proc(load, "glGetUniformLocation");
    glad_glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC)glad_get_proc(load, "glUniformMatrix4fv");
    glad_glDrawArrays = (PFNGLDRAWARRAYSPROC)glad_get_proc(load, "glDrawArrays");
    glad_glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)glad_get_proc(load, "glDeleteVertexArrays");
    glad_glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)glad_get_proc(load, "glDeleteBuffers");
    glad_glGetIntegerv = (PFNGLGETINTEGERVPROC)glad_get_proc(load, "glGetIntegerv");
    glad_glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC)glad_get_proc(load, "glDebugMessageCallback");
    glad_glDebugMessageControl = (PFNGLDEBUGMESSAGECONTROLPROC)glad_get_proc(load, "glDebugMessageControl");

    if (!glad_glGetString || !glad_glClearColor || !glad_glClear || !glad_glEnable || !glad_glCullFace ||
        !glad_glFrontFace || !glad_glViewport || !glad_glCreateShader || !glad_glShaderSource ||
        !glad_glCompileShader || !glad_glGetShaderiv || !glad_glGetShaderInfoLog || !glad_glCreateProgram ||
        !glad_glAttachShader || !glad_glLinkProgram || !glad_glGetProgramiv || !glad_glGetProgramInfoLog ||
        !glad_glDeleteShader || !glad_glUseProgram || !glad_glDeleteProgram || !glad_glGenVertexArrays ||
        !glad_glBindVertexArray || !glad_glGenBuffers || !glad_glBindBuffer || !glad_glBufferData ||
        !glad_glEnableVertexAttribArray || !glad_glVertexAttribPointer || !glad_glGetUniformLocation ||
        !glad_glUniformMatrix4fv || !glad_glDrawArrays || !glad_glDeleteVertexArrays || !glad_glDeleteBuffers ||
        !glad_glGetIntegerv) {
        fprintf(stderr, "[glad] Failed to load one or more OpenGL functions.\n");
        return 0;
    }

    GLAD_GL_VERSION_4_5 = 1;
    return 1;
}
