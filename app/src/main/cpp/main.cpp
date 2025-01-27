#include <mutex>

#include <android/log.h>
#include <imgui_impl_android.h>
#include <imgui_impl_vulkan.h>
#include <jni.h>
#include <spdlog/sinks/android_sink.h>
#include <spdlog/sinks/base_sink.h>
#include <vulkan/vulkan.h>
#include <fstream>
#include <game-activity/GameActivity.cpp>
#include <game-activity/native_app_glue/android_native_app_glue.c>
#include <game-text-input/gametextinput.cpp>
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


    class ImGuiContextRaii {

    public:
        ImGuiContextRaii() {
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO &io = ImGui::GetIO();
            (void)io;
        }

        ~ImGuiContextRaii() { ImGui::DestroyContext(); }
    } g_imgui_ctxt_raii;


    class MotionInputManager {

    public:
        void notify(
            const GameActivityMotionEvent &e, mirinae::IEngine &engine
        ) {
            const auto action = e.action & AMOTION_EVENT_ACTION_MASK;

            switch (action) {
                case AMOTION_EVENT_ACTION_POINTER_DOWN:
                    this->activate_pointer(get_point_idx(e), e, engine);
                    break;
                case AMOTION_EVENT_ACTION_POINTER_UP:
                    this->deactivate_pointer(get_point_idx(e), e, engine);
                    break;
                case AMOTION_EVENT_ACTION_DOWN:
                    this->activate_pointer(0, e, engine);
                    break;
                case AMOTION_EVENT_ACTION_UP:
                    this->deactivate_pointer(0, e, engine);
                    break;
                case AMOTION_EVENT_ACTION_MOVE:
                    this->update_movements(e, engine);
                    break;
                default:
                    SPDLOG_WARN(
                        "Unhandled motion input action: {}",
                        static_cast<int>(action)
                    );
                    break;
            }
        }

    private:
        struct Pointer {
            bool notify_pos(float x, float y) {
                bool changed = false;

                if (last_x_ != x) {
                    last_x_ = x;
                    changed = true;
                }
                if (last_y_ != y) {
                    last_y_ = y;
                    changed = true;
                }

                return changed;
            }

            [[nodiscard]]
            mirinae::touch::Event make_event() const {
                mirinae::touch::Event out;
                out.xpos_ = last_x_;
                out.ypos_ = last_y_;
                return out;
            }

            float last_x_ = 0;
            float last_y_ = 0;
            bool active_ = false;
        };

        static int get_point_idx(const GameActivityMotionEvent &e) {
            auto point_idx = e.action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK;
            point_idx = point_idx >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
            return point_idx;
        }

        static float get_axis_x(const GameActivityPointerAxes &axes) {
            return GameActivityPointerAxes_getX(&axes);
        }

        static float get_axis_y(const GameActivityPointerAxes &axes) {
            return GameActivityPointerAxes_getY(&axes);
        }

        Pointer &pointer_at(size_t index) {
            if (index >= pointers_.size())
                pointers_.resize(index + 1);
            return pointers_[index];
        }

        void activate_pointer(
            size_t i, const GameActivityMotionEvent &e, mirinae::IEngine &engine
        ) {
            auto &axes = e.pointers[i];
            auto &p = this->pointer_at(i);
            p.last_x_ = get_axis_x(axes);
            p.last_y_ = get_axis_y(axes);

            if (!p.active_) {
                p.active_ = true;

                auto &io = ImGui::GetIO();
                io.AddMousePosEvent(p.last_x_, p.last_y_);
                io.AddMouseButtonEvent(0, true);
                if (!io.WantCaptureMouse) {
                    auto event = p.make_event();
                    event.index_ = i;
                    event.action_ = mirinae::touch::ActionType::down;
                    engine.on_touch_event(event);
                }
            }
        }

        void deactivate_pointer(
            size_t i, const GameActivityMotionEvent &e, mirinae::IEngine &engine
        ) {
            auto &axes = e.pointers[i];
            auto &p = this->pointer_at(i);
            p.last_x_ = get_axis_x(axes);
            p.last_y_ = get_axis_y(axes);

            if (p.active_) {
                p.active_ = false;

                auto &io = ImGui::GetIO();
                io.AddMousePosEvent(p.last_x_, p.last_y_);
                io.AddMouseButtonEvent(0, false);
                if (!io.WantCaptureMouse) {
                    auto event = p.make_event();
                    event.action_ = mirinae::touch::ActionType::up;
                    event.index_ = i;
                    engine.on_touch_event(event);
                }
            }
        }

        void update_movements(
            const GameActivityMotionEvent &e, mirinae::IEngine &engine
        ) {
            const auto p_count = std::min<size_t>(
                e.pointerCount, pointers_.size()
            );

            for (size_t i = 0; i < p_count; ++i) {
                auto &p = pointers_[i];
                auto &axes = e.pointers[i];
                if (p.notify_pos(get_axis_x(axes), get_axis_y(axes))) {
                    auto &io = ImGui::GetIO();
                    io.AddMousePosEvent(p.last_x_, p.last_y_);
                    if (!io.WantCaptureMouse) {
                        auto event = p.make_event();
                        event.action_ = mirinae::touch::ActionType::move;
                        event.index_ = i;
                        engine.on_touch_event(event);
                    }
                }
            }
        }

        std::vector<Pointer> pointers_;
    };


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

            ImGui_ImplAndroid_Init(app.window);

            create_info_.init_width_ = 100;
            create_info_.init_height_ = 100;
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
            create_info_.imgui_new_frame_ = []() {
                ImGui_ImplAndroid_NewFrame();
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

        void handle_inputs(android_app &app) {
            auto ib = android_app_swap_input_buffers(&app);
            if (nullptr == ib)
                return;

            for (uint64_t i = 0; i < ib->motionEventsCount; ++i) {
                motion_inputs_.notify(ib->motionEvents[i], *engine_);
            }

            android_app_clear_motion_events(ib);
        }

    private:
        mirinae::EngineCreateInfo create_info_;
        std::unique_ptr<mirinae::IEngine> engine_;
        ::MotionInputManager motion_inputs_;
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

}  // namespace


extern "C" {

/*!
 * This the main entry point for a native activity
 */
void android_main(struct android_app *pApp) {
    // Register an event handler for Android events
    pApp->onAppCmd = handle_cmd;
    android_app_set_motion_event_filter(pApp, nullptr);

    auto &app = *pApp;

    while (true) {
        {
            int events;
            android_poll_source *pSource;
            const auto poll_res = ALooper_pollOnce(
                0, nullptr, &events, (void **)&pSource
            );
            if (pSource)
                pSource->process(pApp, pSource);
        }

        if (app.destroyRequested) {
            delete get_userdata_as<::CombinedEngine>(app);
            app.userData = nullptr;
            return;
        }

        if (auto engine = get_userdata_as<CombinedEngine>(pApp)) {
            if (!engine->is_ongoing()) {
                delete get_userdata_as<::CombinedEngine>(app);
                app.userData = nullptr;
                return;
            }

            engine->handle_inputs(app);
            engine->do_frame();
        }
    }
}
}
