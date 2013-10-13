#include <glib.h>
#include <iostream>
#include <cstring>
#include <cassert>

#include <WebKit2/WKContext.h>
#include <WebKit2/WKPage.h>
#include <WebKit2/WKString.h>
#include <WebKit2/WKType.h>
#include <WebKit2/WKURL.h>
#include <WebKit2/WKView.h>

#include  <X11/Xlib.h>
#include  <X11/Xatom.h>
#include  <X11/Xutil.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#define X11
struct state {
    uint32_t screen_width;
    uint32_t screen_height;

    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
};

struct state g_state;
static bool scheduleUpdate = false;

static EGLNativeDisplayType display;
static EGLNativeWindowType window;

static const int kWidth = 1280;
static const int kHeight = 720;

#ifdef X11
static void createBasicObjects()
{
    display = XOpenDisplay(0);
    assert(display != 0);

    Window root = DefaultRootWindow(display);
    XSetWindowAttributes swa;
    memset(&swa, 0, sizeof(swa));
    Window win  = XCreateWindow(display, root, 0, 0, kWidth, kHeight, 0, CopyFromParent,
                                InputOutput, CopyFromParent, CWEventMask, &swa);
    XMapWindow(display, win);
    XStoreName(display, win, "EGLFS");
    window = win;
}
#else
static void createBasicObjects()
{
    assert(!"not reachable");
}
#endif

static void ogl_init(struct state *state)
{
    EGLBoolean result;
    EGLint num_config;

    static const EGLint attribute_list[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_NONE
    };

    createBasicObjects();

    EGLConfig config;

    // We need call eglMakeCurrent beforehand to workaround a bug on rPi.
    // https://github.com/raspberrypi/firmware/issues/99
    eglMakeCurrent(0, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    state->display = eglGetDisplay( display ? display : EGL_DEFAULT_DISPLAY);
    //state->display = eglGetDisplay( (EGLNativeDisplayType) x_display );
    assert(state->display != EGL_NO_DISPLAY);

    result = eglInitialize(state->display, NULL, NULL);
    assert(result != EGL_FALSE);

    /* Get an appropriate EGL frame buffer config. */
    result = eglChooseConfig(state->display, attribute_list, &config, 1, &num_config);
    assert(result != EGL_FALSE);

    result = eglBindAPI(EGL_OPENGL_ES_API);
    assert(EGL_FALSE != result);

    static const EGLint context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, context_attributes);
    assert(state->context != EGL_NO_CONTEXT);

    state->screen_width = kWidth; state->screen_height = kHeight;

    state->surface = eglCreateWindowSurface(state->display, config, window, NULL);
    assert(state->surface != EGL_NO_SURFACE);

    result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
    assert(result != EGL_FALSE);
    assert(glGetError() == 0);
}

static void ogl_exit(struct state *state)
{
    eglMakeCurrent(state->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(state->display, state->surface);
    eglDestroyContext(state->display, state->context);
    eglTerminate(state->display);
}
static int scheduledDisplayUpdate(void * data)
{
    WKViewRef webView = static_cast<WKViewRef>(data);
    scheduleUpdate = false;
    eglMakeCurrent(g_state.display,g_state.surface, g_state.surface, g_state.context);
    glClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    WKViewPaintToCurrentGLContext(webView);
    eglSwapBuffers(g_state.display, g_state.surface);
    return 0;
}

static void viewNeedsDisplay(WKViewRef webView, WKRect, const void*)
{
    if(scheduleUpdate)
        return;
    scheduleUpdate = true;
    g_timeout_add(0, scheduledDisplayUpdate, (gpointer)webView);
}

static void didReceiveTitleForFrame(WKPageRef page, WKStringRef title, WKFrameRef, WKTypeRef, const void*)
{
    char buffer[256];
    size_t size = WKStringGetUTF8CString(title, buffer, sizeof(buffer) - 1);
    buffer[size] = 0;
    std::cout << "Title: " << buffer << "\n";
}

int main(int argc, char* argv[])
{
    const char* url = argc == 2 ? argv[1] : "http://www.google.com";

    //ProfilerStart("/home/pi/Sample.prof");

    //bcm_host_init();

    memset(&g_state, 0, sizeof(struct state));
    ogl_init(&g_state);

    GMainLoop* mainLoop = g_main_loop_new(0, false);
    WKContextRef context = WKContextCreateWithInjectedBundlePath(WKStringCreateWithUTF8CString(SAMPLE_INJECTEDBUNDLE_DIR "libSampleInjectedBundle.so"));
    WKViewRef webView = WKViewCreate(context, NULL);
    WKPageRef page = WKViewGetPage(webView);

    WKViewClient viewClient;
    memset(&viewClient, 0, sizeof(WKViewClient));
    viewClient.version = kWKViewClientCurrentVersion;
    viewClient.viewNeedsDisplay = viewNeedsDisplay;
    WKViewSetViewClient(webView, &viewClient);

    WKViewInitialize(webView);

    WKPageLoaderClient loaderClient;
    memset(&loaderClient, 0, sizeof(loaderClient));
    loaderClient.version = kWKPageLoaderClientCurrentVersion;
    loaderClient.didReceiveTitleForFrame = didReceiveTitleForFrame;
    WKPageSetPageLoaderClient(page, &loaderClient);

    WKViewSetSize(webView, WKSizeMake(g_state.screen_width, g_state.screen_height));
    WKPageLoadURL(page, WKURLCreateWithUTF8CString(url));

    //ProfilerFlush();
    //ProfilerStop();

    g_main_loop_run(mainLoop);

    WKRelease(webView);
    WKRelease(context);
    g_main_loop_unref(mainLoop);

    ogl_exit(&g_state);

    return 0;
}
