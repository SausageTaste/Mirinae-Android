#include <mutex>

#include <android/log.h>
#include <jni.h>
#include <game-activity/GameActivity.cpp>
#include <game-activity/native_app_glue/android_native_app_glue.c>
#include <game-text-input/gametextinput.cpp>

#include <spdlog/sinks/android_sink.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>
#include <fstream>
#include <mirinae/engine.hpp>

#include "filesys.hpp"


namespace {

    std::shared_ptr<spdlog::logger> g_android_logger;


    class CombinedEngine {

    public:
        explicit CombinedEngine(android_app *const state) {
            // Logger
            if (!g_android_logger) {
                g_android_logger = spdlog::android_logger_mt(
                    "android", "Mirinae"
                );
                spdlog::set_default_logger(g_android_logger);
                spdlog::set_level(spdlog::level::debug);
            }

            create_info_.filesys_ = std::make_shared<dal::Filesystem>();
            create_info_.filesys_->add_subsys(
                mirinapp::create_filesubsys_android_asset(
                    state->activity->assetManager
                )
            );
            create_info_.filesys_->add_subsys(dal::create_filesubsys_std(
                "", ::std::filesystem::u8path(state->activity->externalDataPath)
            ));

            create_info_.instance_extensions_ = std::vector<std::string>{
                "VK_KHR_surface",
                "VK_KHR_android_surface",
            };
            create_info_.surface_creator_ = [state](void *instance
                                            ) -> uint64_t {
                VkAndroidSurfaceCreateInfoKHR create_info{
                    .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
                    .pNext = nullptr,
                    .flags = 0,
                    .window = state->window,
                };

                VkSurfaceKHR surface = VK_NULL_HANDLE;
                const auto create_result = vkCreateAndroidSurfaceKHR(
                    reinterpret_cast<VkInstance>(instance),
                    &create_info,
                    nullptr,
                    &surface
                );

                return *reinterpret_cast<uint64_t *>(&surface);
            };

            engine_ = mirinae::create_engine(std::move(create_info_));
        }

        void do_frame() { engine_->do_frame(); }

        [[nodiscard]]
        bool is_ongoing() const {
            if (nullptr == engine_)
                return false;
            if (!engine_->is_ongoing())
                return false;

            return true;
        }

        void on_resize(uint32_t w, uint32_t h) {
            engine_->notify_window_resize(w, h);
        }

    private:
        mirinae::EngineCreateInfo create_info_;
        std::unique_ptr<mirinae::IEngine> engine_;
    };

}  // namespace


void on_content_rect_changed(android_app *const state) {
    if (nullptr == state->userData)
        return;

    auto &engine = *reinterpret_cast<::CombinedEngine *>(state->userData);
    const auto width = state->contentRect.right - state->contentRect.left;
    const auto height = state->contentRect.bottom - state->contentRect.top;

    engine.on_resize(width, height);
}


extern "C" {

void handle_cmd(android_app *pApp, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            spdlog::info("APP_CMD_INIT_WINDOW");
            if (pApp->userData) {
                auto engine = reinterpret_cast<::CombinedEngine *>(
                    pApp->userData
                );
                delete engine;
            }
            pApp->userData = new ::CombinedEngine(pApp);
            break;
        case APP_CMD_CONTENT_RECT_CHANGED:
            spdlog::info("APP_CMD_CONTENT_RECT_CHANGED");
            on_content_rect_changed(pApp);
            break;
        case APP_CMD_TERM_WINDOW:
            spdlog::info("APP_CMD_TERM_WINDOW");
            if (pApp->userData) {
                auto engine = reinterpret_cast<::CombinedEngine *>(
                    pApp->userData
                );
                delete engine;
            }
            pApp->userData = nullptr;
            break;
        default:
            spdlog::info("APP_CMD ({}): unhandled", static_cast<int>(cmd));
            break;
    }
}

/*!
 * Enable the motion events you want to handle; not handled events are
 * passed back to OS for further processing. For this example case,
 * only pointer and joystick devices are enabled.
 *
 * @param motionEvent the newly arrived GameActivityMotionEvent.
 * @return true if the event is from a pointer or joystick device,
 *         false for all other input devices.
 */
bool motion_event_filter_func(const GameActivityMotionEvent *motionEvent) {
    auto sourceClass = motionEvent->source & AINPUT_SOURCE_CLASS_MASK;
    return (
        sourceClass == AINPUT_SOURCE_CLASS_POINTER ||
        sourceClass == AINPUT_SOURCE_CLASS_JOYSTICK
    );
}

/*!
 * This the main entry point for a native activity
 */
void android_main(struct android_app *pApp) {
    // Register an event handler for Android events
    pApp->onAppCmd = handle_cmd;

    // Set input event filters (set it to NULL if the app wants to process all
    // inputs). Note that for key inputs, this example uses the default
    // default_key_filter() implemented in android_native_app_glue.c.
    android_app_set_motion_event_filter(pApp, motion_event_filter_func);

    do {
        int events;
        android_poll_source *pSource;
        const auto poll_res = ALooper_pollOnce(
            0, nullptr, &events, (void **)&pSource
        );
        if (pSource)
            pSource->process(pApp, pSource);

        if (pApp->userData) {
            auto engine = reinterpret_cast<::CombinedEngine *>(pApp->userData);
            if (!engine->is_ongoing())
                break;
            engine->do_frame();
        }
    } while (!pApp->destroyRequested);
}
}
