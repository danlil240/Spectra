#pragma once

#include <cstdint>
#include <span>
#include <string>

namespace plotix {

class ImageExporter {
public:
    static bool write_png(const std::string& path,
                          const uint8_t* rgba_data,
                          uint32_t width,
                          uint32_t height);
};

#ifdef PLOTIX_USE_FFMPEG
class VideoExporter {
public:
    struct Config {
        std::string output_path;
        uint32_t    width    = 1280;
        uint32_t    height   = 720;
        float       fps      = 60.0f;
        std::string codec    = "libx264";
        std::string pix_fmt  = "yuv420p";
    };

    explicit VideoExporter(const Config& config);
    ~VideoExporter();

    VideoExporter(const VideoExporter&) = delete;
    VideoExporter& operator=(const VideoExporter&) = delete;

    bool write_frame(const uint8_t* rgba_data);
    void finish();

    bool is_open() const { return pipe_ != nullptr; }

private:
    Config config_;
    FILE*  pipe_ = nullptr;
};
#endif // PLOTIX_USE_FFMPEG

} // namespace plotix
