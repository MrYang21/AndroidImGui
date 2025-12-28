#include <cstdint>
#include <thread>
#include <chrono>
#include <atomic>
#include <android/log.h>
#include <unistd.h>

#include "../Header/Header.hpp"
#include "../Header/ANwCreator.hpp"
#include "../Render/AImGui.hpp"

static ANativeActivity *g_Activity = nullptr;
static android::AImGui *g_ImGui = nullptr;

static std::atomic<bool> g_EverCreated{false};

static std::atomic<bool> g_WindowReady{false};

enum : int
{
    ACT_NONE = 0,
    ACT_CREATE = 1,
    ACT_DESTROY = 2
};
static std::atomic<int> g_PendingAction{ACT_NONE};

static void (*g_OrigOnNativeWindowCreated)(ANativeActivity *, ANativeWindow *) = nullptr;
static void (*g_OrigOnNativeWindowDestroyed)(ANativeActivity *, ANativeWindow *) = nullptr;

static inline void SignalCreateOnly(ANativeActivity *activity)
{
    g_Activity = activity;
    g_WindowReady.store(true, std::memory_order_release);
    g_PendingAction.store(ACT_CREATE, std::memory_order_release);
}

static void MyOnNativeWindowCreated(ANativeActivity *activity, ANativeWindow *window)
{
    __android_log_print(ANDROID_LOG_INFO, "IMGUI", "WindowCreated %p", window);

    g_EverCreated.store(true, std::memory_order_release);

    if (g_OrigOnNativeWindowCreated)
        g_OrigOnNativeWindowCreated(activity, window);

    SignalCreateOnly(activity);
}

static void MyOnNativeWindowDestroyed(ANativeActivity *activity, ANativeWindow *window)
{
    __android_log_print(ANDROID_LOG_INFO, "IMGUI", "WindowDestroyed %p", window);

    g_WindowReady.store(false, std::memory_order_release);

    g_PendingAction.store(ACT_DESTROY, std::memory_order_release);

    if (g_OrigOnNativeWindowDestroyed)
        g_OrigOnNativeWindowDestroyed(activity, window);
}

static void RenderLoop()
{
    bool state = true, showDemoWindow = false, showAnotherWindow = false;

    while (state)
    {
        int act = g_PendingAction.exchange(ACT_NONE, std::memory_order_acq_rel);

        if (act == ACT_DESTROY)
        {
            if (g_ImGui)
            {
                g_ImGui->Destroy();
                delete g_ImGui;
                g_ImGui = nullptr;
            }
        }
        else if (act == ACT_CREATE)
        {
            if (g_ImGui)
            {
                g_ImGui->Destroy();
                delete g_ImGui;
                g_ImGui = nullptr;
            }

            if (g_Activity && g_WindowReady.load(std::memory_order_acquire))
            {
                g_ImGui = new android::AImGui({g_Activity, true});
            }
        }

        if (g_WindowReady.load(std::memory_order_acquire) && g_ImGui)
        {
            g_ImGui->BeginFrame();

            if (showDemoWindow)
                ImGui::ShowDemoWindow(&showDemoWindow);

            {
                static float f = 0.0f;
                static int counter = 0;

                ImGui::Begin("Hello, world!", &state);
                ImGui::Text("This is some useful text.");
                ImGui::Checkbox("Demo Window", &showDemoWindow);
                ImGui::Checkbox("Another Window", &showAnotherWindow);

                ImGui::SliderFloat("float", &f, 0.0f, 1.0f);

                if (ImGui::Button("Button"))
                    counter++;
                ImGui::SameLine();
                ImGui::Text("counter = %d", counter);

                ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                            1000.0f / ImGui::GetIO().Framerate,
                            ImGui::GetIO().Framerate);
                ImGui::End();
            }

            if (showAnotherWindow)
            {
                ImGui::Begin("Another Window", &showAnotherWindow);
                ImGui::Text("Hello from another window!");
                if (ImGui::Button("Close Me"))
                    showAnotherWindow = false;
                ImGui::End();
            }

            g_ImGui->EndFrame();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    if (g_ImGui)
    {
        g_ImGui->Destroy();
        delete g_ImGui;
        g_ImGui = nullptr;
    }
}

// -------------------- 入口 --------------------
__attribute__((constructor)) static void MyStart()
{
    std::thread([] {
        while (Data.libUE4 <= 0)
        {
            Data.libUE4 = Memory::FindModuleBase("libUE4.so");
            if (Data.libUE4 > 0)
                break;
            sleep(1);
        }

        while (true)
        {
            uintptr_t p0, p1, p2, p3;

            p0 = Data.libUE4 + 0xE4D1EF8;
            if (!p0) goto sleep_1;

            p1 = *(uintptr_t *)p0;
            if (!p1) goto sleep_1;

            p2 = *(uintptr_t *)(p1 + 0x268);
            if (!p2) goto sleep_1;

            p3 = *(uintptr_t *)(p2 + 0x40);
            if (!p3) goto sleep_1;

            g_Activity = *(ANativeActivity **)(p3 + 0x18);
            if (!g_Activity) goto sleep_1;

            g_OrigOnNativeWindowCreated   = g_Activity->callbacks->onNativeWindowCreated;
            g_OrigOnNativeWindowDestroyed = g_Activity->callbacks->onNativeWindowDestroyed;

            g_Activity->callbacks->onNativeWindowCreated   = MyOnNativeWindowCreated;
            g_Activity->callbacks->onNativeWindowDestroyed = MyOnNativeWindowDestroyed;

            __android_log_print(ANDROID_LOG_INFO, "IMGUI","Callbacks installed. activity=%p", g_Activity);

            for (int i = 0; i < 200 && !g_EverCreated.load(std::memory_order_acquire); ++i)
                usleep(10000); // 10ms

            if (!g_EverCreated.load(std::memory_order_acquire))
            {
                __android_log_print(ANDROID_LOG_INFO, "IMGUI","Missed first WindowCreated, force SignalCreateOnly()");
                SignalCreateOnly(g_Activity);
            }

            break;

        sleep_1:
            usleep(1000); // 1ms
        }

        RenderLoop();
    }).detach();
}

