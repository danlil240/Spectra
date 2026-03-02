// qt_embed_demo.cpp — Qt6 example embedding a Spectra plot via Vulkan canvas.
//
// Build (requires Qt6):
//   cmake -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON \
//         -DSPECTRA_BUILD_EXAMPLES=ON -DCMAKE_PREFIX_PATH=/path/to/Qt6 ..
//   make qt_embed_demo
//
// Usage:
//   ./qt_embed_demo [--multi|--single]
//
// This demonstrates first-class Vulkan integration: Spectra creates a real
// VkSurface + swapchain on a QWindow and renders directly — no CPU readback,
// no QImage blitting.  Input events are forwarded through Spectra's
// InputHandler for pan/zoom/select.

#ifdef SPECTRA_HAS_QT6

#include <QApplication>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPlatformSurfaceEvent>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <spectra/axes.hpp>
#include <spectra/embed.hpp>
#include <spectra/figure.hpp>
#include <spectra/series.hpp>

#include "adapters/qt/qt_runtime.hpp"
#include "ui/input/input.hpp"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

// ─── SpectraVulkanWindow ─────────────────────────────────────────────────────
// A QWindow with VulkanSurface type that drives the Spectra render loop.

class SpectraVulkanWindow : public QWindow
{
   public:
    explicit SpectraVulkanWindow(QWindow* parent = nullptr) : QWindow(parent)
    {
        setSurfaceType(QSurface::VulkanSurface);
        setMinimumSize(QSize(400, 300));
        resize(800, 600);
    }

    void setRuntime(spectra::adapters::qt::QtRuntime* rt) { runtime_ = rt; }
    void setFigure(spectra::Figure* fig) { figure_ = fig; }
    void setInputHandler(spectra::InputHandler* ih) { input_ = ih; }
    bool isAttached() const { return attached_; }

    bool ensureAttached()
    {
        if (attached_)
            return true;
        if (!runtime_ || !isExposed())
            return false;

        auto dpr = devicePixelRatio();
        auto w   = static_cast<uint32_t>(width() * dpr);
        auto h   = static_cast<uint32_t>(height() * dpr);
        if (w == 0 || h == 0)
            return false;

        if (!runtime_->attach_window(this, w, h))
            return false;

        attached_ = true;
        last_dpr_ = dpr;
        return true;
    }

    void forceDetach()
    {
        if (runtime_ && attached_)
        {
            runtime_->detach_window(this);
        }
        attached_ = false;
    }

    void startFrameTimer()
    {
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &SpectraVulkanWindow::renderFrame);
        timer_->start(16);   // ~60 FPS
    }

   protected:
    bool event(QEvent* event) override
    {
        if (event && event->type() == QEvent::PlatformSurface && runtime_)
        {
            auto* platform_event = static_cast<QPlatformSurfaceEvent*>(event);
            if (platform_event->surfaceEventType()
                == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed)
            {
                forceDetach();
            }
            else if (platform_event->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated)
            {
                (void)ensureAttached();
            }
        }
        return QWindow::event(event);
    }

    void exposeEvent(QExposeEvent* /*event*/) override
    {
        if (!isExposed())
            return;

        if (!attached_ && runtime_)
        {
            // First expose: create surface + swapchain.
            if (ensureAttached())
            {
                renderFrame();
            }
            return;
        }

        if (attached_ && runtime_)
        {
            // Re-expose after hide (minimize→restore, monitor move, etc.)
            // Check for DPR change (moved to a different-scale monitor).
            auto dpr = devicePixelRatio();
            if (dpr != last_dpr_)
            {
                last_dpr_ = dpr;
                runtime_->mark_swapchain_dirty(this);
            }

            // Ensure a frame is rendered promptly after becoming visible again.
            renderFrame();
        }
    }

    void resizeEvent(QResizeEvent* /*event*/) override
    {
        if (!attached_ || !runtime_)
            return;

        // Set dirty flag — actual swapchain recreation is deferred to
        // begin_frame() at the next frame boundary.
        runtime_->mark_swapchain_dirty(this);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!input_)
            return;
        auto pos = event->position();
        auto dpr = devicePixelRatio();
        input_->on_mouse_move(pos.x() * dpr, pos.y() * dpr);
        requestUpdate();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (!input_)
            return;
        auto pos = event->position();
        auto dpr = devicePixelRatio();
        int  btn = qtButtonToSpectra(event->button());
        int  mod = qtModsToSpectra(event->modifiers());
        input_->on_mouse_button(btn, spectra::embed::ACTION_PRESS, mod,
                                pos.x() * dpr, pos.y() * dpr);
        requestUpdate();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (!input_)
            return;
        auto pos = event->position();
        auto dpr = devicePixelRatio();
        int  btn = qtButtonToSpectra(event->button());
        int  mod = qtModsToSpectra(event->modifiers());
        input_->on_mouse_button(btn, spectra::embed::ACTION_RELEASE, mod,
                                pos.x() * dpr, pos.y() * dpr);
        requestUpdate();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (!input_)
            return;
        auto  pos = event->position();
        auto  dpr = devicePixelRatio();
        float dy  = static_cast<float>(event->angleDelta().y()) / 120.0f;
        float dx  = static_cast<float>(event->angleDelta().x()) / 120.0f;
        input_->on_scroll(dx, dy, pos.x() * dpr, pos.y() * dpr);
        requestUpdate();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (!input_)
            return;
        int key = qtKeyToSpectra(event->key());
        int mod = qtModsToSpectra(event->modifiers());
        input_->on_key(key, spectra::embed::ACTION_PRESS, mod);
        requestUpdate();
    }

    void keyReleaseEvent(QKeyEvent* event) override
    {
        if (!input_)
            return;
        int key = qtKeyToSpectra(event->key());
        int mod = qtModsToSpectra(event->modifiers());
        input_->on_key(key, spectra::embed::ACTION_RELEASE, mod);
    }

   private:
    void renderFrame()
    {
        if (!attached_ || !runtime_ || !figure_)
            return;

        // Visibility guard: skip rendering when window is not exposed
        // (hidden, minimized, or zero-size).  Prevents swapchain storms
        // and wasted GPU work.
        if (!isExposed())
            return;

        auto dpr = devicePixelRatio();
        auto w   = static_cast<uint32_t>(width() * dpr);
        auto h   = static_cast<uint32_t>(height() * dpr);
        if (w == 0 || h == 0)
            return;

        if (runtime_->begin_frame(this))
        {
            runtime_->render_figure(this, *figure_);
            runtime_->end_frame(this);
        }
    }

    static int qtButtonToSpectra(Qt::MouseButton btn)
    {
        switch (btn)
        {
            case Qt::LeftButton: return spectra::embed::MOUSE_BUTTON_LEFT;
            case Qt::RightButton: return spectra::embed::MOUSE_BUTTON_RIGHT;
            case Qt::MiddleButton: return spectra::embed::MOUSE_BUTTON_MIDDLE;
            default: return 0;
        }
    }

    static int qtModsToSpectra(Qt::KeyboardModifiers mods)
    {
        int result = 0;
        if (mods & Qt::ShiftModifier) result |= spectra::embed::MOD_SHIFT;
        if (mods & Qt::ControlModifier) result |= spectra::embed::MOD_CONTROL;
        if (mods & Qt::AltModifier) result |= spectra::embed::MOD_ALT;
        if (mods & Qt::MetaModifier) result |= spectra::embed::MOD_SUPER;
        return result;
    }

    static int qtKeyToSpectra(int qt_key)
    {
        if (qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z)
            return qt_key;
        if (qt_key >= Qt::Key_0 && qt_key <= Qt::Key_9)
            return qt_key;

        switch (qt_key)
        {
            case Qt::Key_Escape: return spectra::embed::KEY_ESCAPE;
            case Qt::Key_Return:
            case Qt::Key_Enter: return spectra::embed::KEY_ENTER;
            case Qt::Key_Tab: return spectra::embed::KEY_TAB;
            case Qt::Key_Backspace: return spectra::embed::KEY_BACKSPACE;
            case Qt::Key_Delete: return spectra::embed::KEY_DELETE;
            case Qt::Key_Right: return spectra::embed::KEY_RIGHT;
            case Qt::Key_Left: return spectra::embed::KEY_LEFT;
            case Qt::Key_Down: return spectra::embed::KEY_DOWN;
            case Qt::Key_Up: return spectra::embed::KEY_UP;
            case Qt::Key_Home: return spectra::embed::KEY_HOME;
            case Qt::Key_End: return spectra::embed::KEY_END;
            case Qt::Key_Space: return spectra::embed::KEY_SPACE;
            default: return 0;
        }
    }

    spectra::adapters::qt::QtRuntime* runtime_  = nullptr;
    spectra::Figure*                  figure_   = nullptr;
    spectra::InputHandler*            input_    = nullptr;
    QTimer*                           timer_    = nullptr;
    bool                              attached_ = false;
    qreal                             last_dpr_ = 1.0;
};

// ─── main ────────────────────────────────────────────────────────────────────

#ifdef SPECTRA_QT_FORCE_MULTI_CANVAS
static constexpr bool k_default_multi_canvas = true;
#else
static constexpr bool k_default_multi_canvas = false;
#endif

static void populate_demo_figure(spectra::Figure& fig, float phase)
{
    auto& ax = fig.subplot(1, 1, 1);

    const int              N = 200;
    std::vector<float> x(N), y_sin(N), y_cos(N);
    for (int i = 0; i < N; i++)
    {
        x[i]     = static_cast<float>(i) * 0.1f;
        y_sin[i] = std::sin(x[i] + phase);
        y_cos[i] = std::cos(x[i] + phase);
    }

    ax.line(x, y_sin).label("sin(x)");
    ax.line(x, y_cos).label("cos(x)");
    ax.auto_fit();
}

static void setup_input_handler(spectra::InputHandler& input, spectra::Figure& figure)
{
    input.set_figure(&figure);
    if (!figure.axes().empty() && figure.axes()[0])
    {
        input.set_active_axes(figure.axes()[0].get());
    }
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    bool multi_canvas = k_default_multi_canvas;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--multi" || arg == "-m")
        {
            multi_canvas = true;
        }
        else if (arg == "--single" || arg == "-s")
        {
            multi_canvas = false;
        }
    }

    // 1. Initialize Spectra Qt runtime (Vulkan backend + renderer)
    spectra::adapters::qt::QtRuntime runtime;
    if (!runtime.init())
    {
        qFatal("Failed to initialize Spectra Vulkan runtime");
        return 1;
    }

    QMainWindow main_window;
    main_window.setWindowTitle(multi_canvas ? "Spectra — Qt Vulkan Embed Demo (Multi Canvas)"
                                            : "Spectra — Qt Vulkan Embed Demo");
    main_window.resize(800, 600);

    // Keep demo objects alive for the entire event loop.
    spectra::Figure       figure_a;
    spectra::InputHandler input_a;
    SpectraVulkanWindow   window_a;

    spectra::Figure       figure_b;
    spectra::InputHandler input_b;
    SpectraVulkanWindow   window_b;

    // 2. Create demo figures
    populate_demo_figure(figure_a, 0.0f);
    setup_input_handler(input_a, figure_a);

    window_a.setRuntime(&runtime);
    window_a.setFigure(&figure_a);
    window_a.setInputHandler(&input_a);
    window_a.setVulkanInstance(runtime.vulkan_instance());

    if (!multi_canvas)
    {
        QWidget* container = QWidget::createWindowContainer(&window_a, &main_window);
        container->setMinimumSize(400, 300);
        container->setFocusPolicy(Qt::StrongFocus);
        main_window.setCentralWidget(container);
    }
    else
    {
        populate_demo_figure(figure_b, 0.75f);
        setup_input_handler(input_b, figure_b);

        window_b.setRuntime(&runtime);
        window_b.setFigure(&figure_b);
        window_b.setInputHandler(&input_b);
        window_b.setVulkanInstance(runtime.vulkan_instance());

        auto* splitter = new QSplitter(Qt::Horizontal, &main_window);

        QWidget* left_container = QWidget::createWindowContainer(&window_a, splitter);
        left_container->setMinimumSize(300, 250);
        left_container->setFocusPolicy(Qt::StrongFocus);

        QWidget* right_container = QWidget::createWindowContainer(&window_b, splitter);
        right_container->setMinimumSize(300, 250);
        right_container->setFocusPolicy(Qt::StrongFocus);

        splitter->addWidget(left_container);
        splitter->addWidget(right_container);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
        main_window.setCentralWidget(splitter);

        // Multi-canvas lifecycle test hook:
        // Ctrl+D toggles right canvas detach/reattach without restarting app.
        auto* toggle_right_attach =
            new QShortcut(QKeySequence(QStringLiteral("Ctrl+D")), &main_window);
        QObject::connect(toggle_right_attach, &QShortcut::activated, &main_window, [&window_b]() {
            if (window_b.isAttached())
            {
                window_b.forceDetach();
            }
            else
            {
                (void)window_b.ensureAttached();
            }
        });
    }

    main_window.show();

    // 3. Start frame timers after windows are visible
    window_a.startFrameTimer();
    if (multi_canvas)
    {
        window_b.startFrameTimer();
    }

    int result = app.exec();

    // 4. Clean shutdown — runtime dtor also handles Vulkan cleanup
    runtime.shutdown();

    return result;
}

#else

#include <iostream>

int main()
{
    std::cout << "This example requires Qt6. Build with:\n"
              << "  cmake -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON "
                 "-DSPECTRA_BUILD_EXAMPLES=ON -DCMAKE_PREFIX_PATH=/path/to/Qt6 ..\n"
              << "Run with --multi for two canvases, or --single for one.\n";
    return 0;
}

#endif   // SPECTRA_HAS_QT6
