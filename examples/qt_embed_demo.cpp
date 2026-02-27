// qt_embed_demo.cpp — Minimal Qt6 example embedding a Spectra plot in a QWidget.
//
// Build (requires Qt6):
//   cmake -DSPECTRA_BUILD_EXAMPLES=ON -DCMAKE_PREFIX_PATH=/path/to/Qt6 ..
//   make qt_embed_demo
//
// Usage:
//   ./qt_embed_demo
//
// This demonstrates the simplest integration path: Spectra renders to a CPU
// buffer, which is blitted into a QImage and painted by QPainter.
// For zero-copy rendering, use EmbedSurface::render_to_image() with
// QVulkanWindow instead.

#ifdef SPECTRA_HAS_QT6

#include <QApplication>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWidget>

#include <spectra/embed.hpp>

#include <cmath>
#include <vector>

class SpectraWidget : public QWidget
{
   public:
    explicit SpectraWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumSize(400, 300);
        resize(800, 600);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);

        // Create the embed surface
        spectra::EmbedConfig cfg;
        cfg.width  = static_cast<uint32_t>(width());
        cfg.height = static_cast<uint32_t>(height());
        surface_   = std::make_unique<spectra::EmbedSurface>(cfg);

        // Create a figure with some data
        auto& fig = surface_->figure();
        auto& ax  = fig.subplot(1, 1, 1);

        // Generate sin/cos data
        const int N = 200;
        std::vector<float> x(N), y_sin(N), y_cos(N);
        for (int i = 0; i < N; i++)
        {
            x[i]     = static_cast<float>(i) * 0.1f;
            y_sin[i] = std::sin(x[i]);
            y_cos[i] = std::cos(x[i]);
        }

        ax.line(x, y_sin).label("sin(x)");
        ax.line(x, y_cos).label("cos(x)");

        // Set up a redraw callback so Spectra can request repaints
        surface_->set_redraw_callback([this]() { update(); });

        // Allocate pixel buffer
        pixels_.resize(static_cast<size_t>(width()) * height() * 4);

        // Timer for animation updates (60 FPS)
        auto* timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]() {
            surface_->update(1.0f / 60.0f);
            renderAndUpdate();
        });
        timer->start(16);   // ~60 FPS
    }

   protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        if (pixels_.empty())
            return;

        QImage img(pixels_.data(), width(), height(), width() * 4, QImage::Format_RGBA8888);
        QPainter painter(this);
        painter.drawImage(0, 0, img);
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        auto w = static_cast<uint32_t>(width());
        auto h = static_cast<uint32_t>(height());
        if (surface_->resize(w, h))
        {
            pixels_.resize(static_cast<size_t>(w) * h * 4);
            renderAndUpdate();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        auto pos = event->position();
        surface_->inject_mouse_move(static_cast<float>(pos.x()), static_cast<float>(pos.y()));
        renderAndUpdate();
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        auto pos = event->position();
        int  btn = qtButtonToSpectra(event->button());
        int  mod = qtModsToSpectra(event->modifiers());
        surface_->inject_mouse_button(btn, spectra::embed::ACTION_PRESS, mod,
                                      static_cast<float>(pos.x()), static_cast<float>(pos.y()));
        renderAndUpdate();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        auto pos = event->position();
        int  btn = qtButtonToSpectra(event->button());
        int  mod = qtModsToSpectra(event->modifiers());
        surface_->inject_mouse_button(btn, spectra::embed::ACTION_RELEASE, mod,
                                      static_cast<float>(pos.x()), static_cast<float>(pos.y()));
        renderAndUpdate();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        auto  pos = event->position();
        float dy  = static_cast<float>(event->angleDelta().y()) / 120.0f;
        float dx  = static_cast<float>(event->angleDelta().x()) / 120.0f;
        surface_->inject_scroll(dx, dy, static_cast<float>(pos.x()),
                                static_cast<float>(pos.y()));
        renderAndUpdate();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        int key = qtKeyToSpectra(event->key());
        int mod = qtModsToSpectra(event->modifiers());
        surface_->inject_key(key, spectra::embed::ACTION_PRESS, mod);
        renderAndUpdate();
    }

    void keyReleaseEvent(QKeyEvent* event) override
    {
        int key = qtKeyToSpectra(event->key());
        int mod = qtModsToSpectra(event->modifiers());
        surface_->inject_key(key, spectra::embed::ACTION_RELEASE, mod);
    }

   private:
    void renderAndUpdate()
    {
        if (surface_->render_to_buffer(pixels_.data()))
        {
            update();   // Schedule a QPaintEvent
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
        // Qt key codes for letters match ASCII (and GLFW)
        if (qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z)
            return qt_key;   // 65-90
        if (qt_key >= Qt::Key_0 && qt_key <= Qt::Key_9)
            return qt_key;   // 48-57

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

    std::unique_ptr<spectra::EmbedSurface> surface_;
    std::vector<uint8_t>                   pixels_;
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    SpectraWidget widget;
    widget.setWindowTitle("Spectra — Qt Embed Demo");
    widget.show();
    return app.exec();
}

#else

#include <iostream>

int main()
{
    std::cout << "This example requires Qt6. Build with:\n"
              << "  cmake -DSPECTRA_HAS_QT6=ON -DCMAKE_PREFIX_PATH=/path/to/Qt6 ..\n";
    return 0;
}

#endif   // SPECTRA_HAS_QT6
