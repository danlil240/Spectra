// qt_embed_demo.cpp — Qt6 example embedding a Spectra plot via Vulkan canvas.
//
// Build (requires Qt6):
//   cmake -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON \
//         -DSPECTRA_BUILD_EXAMPLES=ON -DCMAKE_PREFIX_PATH=/path/to/Qt6 ..
//   make qt_embed_demo
//
// Usage:
//   ./qt_embed_demo
//
// This demonstrates first-class Vulkan integration: Spectra creates a real
// VkSurface + swapchain on a QWindow and renders directly — no CPU readback,
// no QImage blitting.  Input events are forwarded through Spectra's
// InputHandler for pan/zoom/select.

#ifdef SPECTRA_HAS_QT6

#include <QApplication>
#include <QMainWindow>
#include <QMouseEvent>
#include <QTimer>
#include <QVBoxLayout>
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

    void startFrameTimer()
    {
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &SpectraVulkanWindow::renderFrame);
        timer_->start(16);   // ~60 FPS
    }

   protected:
    void exposeEvent(QExposeEvent* /*event*/) override
    {
        if (!isExposed())
            return;

        if (!attached_ && runtime_)
        {
            // First expose: create surface + swapchain.
            auto dpr = devicePixelRatio();
            auto w   = static_cast<uint32_t>(width() * dpr);
            auto h   = static_cast<uint32_t>(height() * dpr);
            if (runtime_->attach_window(this, w, h))
            {
                attached_    = true;
                last_dpr_    = dpr;
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
                runtime_->mark_swapchain_dirty();
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
        runtime_->mark_swapchain_dirty();
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

        if (runtime_->begin_frame())
        {
            runtime_->render_figure(*figure_);
            runtime_->end_frame();
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

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // 1. Initialize Spectra Qt runtime (Vulkan backend + renderer)
    spectra::adapters::qt::QtRuntime runtime;
    if (!runtime.init())
    {
        qFatal("Failed to initialize Spectra Vulkan runtime");
        return 1;
    }

    // 2. Create a figure with some data
    spectra::Figure figure;
    auto& ax = figure.subplot(1, 1, 1);

    const int              N = 200;
    std::vector<float> x(N), y_sin(N), y_cos(N);
    for (int i = 0; i < N; i++)
    {
        x[i]     = static_cast<float>(i) * 0.1f;
        y_sin[i] = std::sin(x[i]);
        y_cos[i] = std::cos(x[i]);
    }

    ax.line(x, y_sin).label("sin(x)");
    ax.line(x, y_cos).label("cos(x)");
    ax.auto_fit();

    // 3. Create an InputHandler for pan/zoom/select
    spectra::InputHandler input;
    input.set_figure(&figure);
    if (!figure.axes().empty() && figure.axes()[0])
    {
        input.set_active_axes(figure.axes()[0].get());
    }

    // 4. Create the Vulkan QWindow
    SpectraVulkanWindow vulkan_window;
    vulkan_window.setRuntime(&runtime);
    vulkan_window.setFigure(&figure);
    vulkan_window.setInputHandler(&input);

    // Bind QVulkanInstance before showing the window
    vulkan_window.setVulkanInstance(runtime.vulkan_instance());

    // 5. Embed in a QMainWindow via QWidget::createWindowContainer
    QMainWindow main_window;
    main_window.setWindowTitle("Spectra — Qt Vulkan Embed Demo");
    main_window.resize(800, 600);

    QWidget* container = QWidget::createWindowContainer(&vulkan_window, &main_window);
    container->setMinimumSize(400, 300);
    container->setFocusPolicy(Qt::StrongFocus);
    main_window.setCentralWidget(container);

    main_window.show();

    // 6. Start the frame timer after the window is visible
    vulkan_window.startFrameTimer();

    int result = app.exec();

    // 7. Clean shutdown — runtime dtor handles Vulkan cleanup
    runtime.shutdown();

    return result;
}

#else

#include <iostream>

int main()
{
    std::cout << "This example requires Qt6. Build with:\n"
              << "  cmake -DSPECTRA_USE_QT=ON -DSPECTRA_BUILD_QT_EXAMPLE=ON "
                 "-DSPECTRA_BUILD_EXAMPLES=ON -DCMAKE_PREFIX_PATH=/path/to/Qt6 ..\n";
    return 0;
}

#endif   // SPECTRA_HAS_QT6
