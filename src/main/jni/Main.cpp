#include <list>
#include <vector>
#include <string.h>
#include <cstring>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <thread>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include "Includes/Logger.h"
#include "Includes/obfuscate.h"
#include "Includes/Utils.h"
#include "KittyMemory/MemoryPatch.h"
#include "Menu.h"
#include "Zygisk/zygisk.h"
#include "ImGui/imgui.h"
#include "ImGui/backends/imgui_impl_android.h"
#include "ImGui/backends/imgui_impl_opengl3.h"
#include "Dobby/dobby.h"

using zygisk::Api;
using zygisk::AppSpecializeArgs;
using zygisk::ServerSpecializeArgs;

// Target package here
#define targetPackageName OBFUSCATE("com.mycompany.myapp")

//Target lib here
#define targetLibName OBFUSCATE("libil2cpp.so")

struct {
    int width, height, screenWidth, screenHeight;
    bool setup;
} egl;

void (*old_input)(void *event, void *exAb, void *exAc);
void hook_input(void *event, void *exAb, void *exAc) {
    old_input(event, exAb, exAc);
    ImGui_ImplAndroid_HandleTouchEvent((AInputEvent *) event, {(float) egl.screenWidth / (float) egl.width, (float) egl.screenHeight / (float) egl.height});
    return;
}

int (*old_getWidth)(ANativeWindow* window);
int hook_getWidth(ANativeWindow* window) {
    egl.screenWidth = old_getWidth(window);
    return old_getWidth(window);
}

int (*old_getHeight)(ANativeWindow* window);
int hook_getHeight(ANativeWindow* window) {
    egl.screenHeight = old_getHeight(window);
    return old_getHeight(window);
}

EGLBoolean (*old_eglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
EGLBoolean hook_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    eglQuerySurface(dpy, surface, EGL_WIDTH, &egl.width);
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &egl.height);
    if (!egl.setup) {
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(egl.width, egl.height);
        ImGui_ImplOpenGL3_Init();
        ImGui_ImplAndroid_Init(NULL);
        ImGui::StyleColorsDark();
        ImFontConfig font_cfg;
        font_cfg.SizePixels = 22.0f;
        io.Fonts->AddFontDefault(&font_cfg);
        ImGui::GetStyle().ScaleAllSizes(3.0f);
        egl.setup = true;
    }
    ImGuiIO &io = ImGui::GetIO();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(egl.width, egl.height);
    ImGui::NewFrame();
    ImGui::ShowDemoWindow();
    ImGui::EndFrame();
    ImGui::Render();
    glViewport(0, 0, io.DisplaySize.x, io.DisplaySize.y);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return old_eglSwapBuffers(dpy, surface);
}

// we will run our hacks in a new thread so our while loop doesn't block process main thread
void *hack_start(const char *) {
    LOGI(OBFUSCATE("pthread created"));

    do {
        sleep(1);
    } while (dlopen(OBFUSCATE("libandroid.so"), 4) == NULL);

    do {
        sleep(1);
    } while (dlopen(OBFUSCATE("libinput.so"), 4) == NULL);

    do {
        sleep(1);
    } while (dlopen(OBFUSCATE("libEGL.so"), 4) == NULL);

    DobbyHook((void *) dlsym(dlopen(OBFUSCATE("libandroid.so"), 4), OBFUSCATE("ANativeWindow_getWidth")), (void *) hook_getWidth, (void **) &old_getWidth);
    DobbyHook((void *) dlsym(dlopen(OBFUSCATE("libandroid.so"), 4), OBFUSCATE("ANativeWindow_getHeight")), (void *) hook_getHeight, (void **) &old_getHeight);
    DobbyHook((void *) dlsym(dlopen(OBFUSCATE("libinput.so"), 4), OBFUSCATE("_ZN7android13InputConsumer21initializeMotionEventEPNS_11MotionEventEPKNS_12InputMessageE")), (void *) hook_input, (void **) &old_input);
    DobbyHook((void *) dlsym(dlopen(OBFUSCATE("libEGL.so"), 4), OBFUSCATE("eglSwapBuffers")), (void *) hook_eglSwapBuffers, (void **) &old_eglSwapBuffers);

    //Check if target lib is loaded
    do {
        sleep(1);
    } while (!isLibraryLoaded(targetLibName));

    LOGI(OBFUSCATE("%s has been loaded"), (const char *) targetLibName);

    return NULL;
}

class ImGuiModMenu : public zygisk::ModuleBase {
public:
    void onLoad(Api *api, JNIEnv *env) override {
        this->api = api;
        this->env = env;
    }

    void preAppSpecialize(AppSpecializeArgs *args) override {
        auto package_name = env->GetStringUTFChars(args->nice_name, nullptr);
        auto app_data_dir = env->GetStringUTFChars(args->app_data_dir, nullptr);
        preSpecialize(package_name, app_data_dir);
        env->ReleaseStringUTFChars(args->nice_name, package_name);
        env->ReleaseStringUTFChars(args->app_data_dir, app_data_dir);
    }

    void postAppSpecialize(const AppSpecializeArgs *) override {
        if (enable_hack) {
            std::thread hack_thread(hack_start, game_data_dir);
            hack_thread.detach();
        }
    }

private:
    Api *api;
    JNIEnv *env;
    bool enable_hack;
    char *game_data_dir;

    void preSpecialize(const char *package_name, const char *app_data_dir) {
        if (strcmp(package_name, targetPackageName) == 0) {
            enable_hack = true;
            game_data_dir = new char[strlen(app_data_dir) + 1];
            strcpy(game_data_dir, app_data_dir);
        }
    }
};

REGISTER_ZYGISK_MODULE(ImGuiModMenu)
