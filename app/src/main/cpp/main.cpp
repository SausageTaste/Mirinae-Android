#include <mutex>

#include <android/log.h>
#include <jni.h>
#include <game-activity/GameActivity.cpp>
#include <game-activity/native_app_glue/android_native_app_glue.c>
#include <game-text-input/gametextinput.cpp>

#include <spdlog/sinks/android_sink.h>
#include <spdlog/sinks/base_sink.h>
#include <vulkan/vulkan.h>
#include <fstream>
#include <mirinae/engine.hpp>
#include <mirinae/lightweight/include_spdlog.hpp>

#include "filesys.hpp"

#define GET_ENGINE(app)                                     \
    auto p_engine = get_userdata_as<::CombinedEngine>(app); \
    if (nullptr == p_engine)                                \
        return;                                             \
    auto &engine = *p_engine;

namespace {

    std::shared_ptr<spdlog::logger> g_android_logger;


    template <typename T>
    T *get_userdata_as(android_app *app) {
        if (nullptr == app)
            return nullptr;
        else
            return reinterpret_cast<T *>(app->userData);
    }

    template <typename T>
    T *get_userdata_as(android_app &app) {
        return ::get_userdata_as<T>(&app);
    }


    class CombinedEngine {

    public:
        explicit CombinedEngine(android_app &app) {
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
                    app.activity->assetManager
                )
            );
            create_info_.filesys_->add_subsys(dal::create_filesubsys_std(
                "", ::std::filesystem::u8path(app.activity->externalDataPath)
            ));

            create_info_.instance_extensions_ = std::vector<std::string>{
                "VK_KHR_surface",
                "VK_KHR_android_surface",
            };
            create_info_.surface_creator_ = [&app](void *instance) -> uint64_t {
                VkAndroidSurfaceCreateInfoKHR create_info{
                    .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
                    .pNext = nullptr,
                    .flags = 0,
                    .window = app.window,
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


    void handle_cmd_init_window(android_app &app) {
        SPDLOG_DEBUG("APP_CMD_INIT_WINDOW");
        delete get_userdata_as<::CombinedEngine>(app);
        app.userData = new ::CombinedEngine(app);
    }

    void handle_cmd_term_window(android_app &app) {
        SPDLOG_DEBUG("APP_CMD_TERM_WINDOW");
        delete get_userdata_as<::CombinedEngine>(app);
        app.userData = nullptr;
    }

    void handle_cmd_rect_changed(android_app &app) {
        SPDLOG_DEBUG("APP_CMD_CONTENT_RECT_CHANGED");
        GET_ENGINE(app);

        const auto width = app.contentRect.right - app.contentRect.left;
        const auto height = app.contentRect.bottom - app.contentRect.top;

        engine.on_resize(width, height);
    }

    void handle_cmd(android_app *app, int32_t cmd) {
        if (nullptr == app)
            return;

        switch (cmd) {
            case APP_CMD_INIT_WINDOW:
                return ::handle_cmd_init_window(*app);
            case APP_CMD_TERM_WINDOW:
                ::handle_cmd_term_window(*app);
            case APP_CMD_CONTENT_RECT_CHANGED:
                ::handle_cmd_rect_changed(*app);
            default:
                SPDLOG_WARN("Unhandled APP cmd: {}", static_cast<int>(cmd));
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

}  // namespace


extern "C" {

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

        if (auto engine = get_userdata_as<CombinedEngine>(pApp)) {
            if (!engine->is_ongoing())
                break;
            engine->do_frame();
        }
    } while (!pApp->destroyRequested);
}
}
