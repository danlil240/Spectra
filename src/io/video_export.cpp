#include <spectra/export.hpp>

#ifdef SPECTRA_USE_FFMPEG

    #include <cstdio>
    #include <sstream>
    #include <stdexcept>

namespace spectra
{

VideoExporter::VideoExporter(const Config& config) : config_(config)
{
    // Build ffmpeg command line:
    //   ffmpeg -y -f rawvideo -vcodec rawvideo -pix_fmt rgba
    //          -s WxH -r FPS -i - -c:v CODEC -pix_fmt PIX_FMT OUTPUT
    std::ostringstream cmd;
    cmd << "ffmpeg -y"
        << " -f rawvideo"
        << " -vcodec rawvideo"
        << " -pix_fmt rgba"
        << " -s " << config_.width << "x" << config_.height << " -r "
        << static_cast<int>(config_.fps) << " -i -"
        << " -c:v " << config_.codec << " -pix_fmt " << config_.pix_fmt << " "
        << config_.output_path << " 2>/dev/null";

    pipe_ = popen(cmd.str().c_str(), "w");
}

VideoExporter::~VideoExporter()
{
    finish();
}

bool VideoExporter::write_frame(const uint8_t* rgba_data)
{
    if (!pipe_ || !rgba_data)
    {
        return false;
    }

    size_t frame_bytes =
        static_cast<size_t>(config_.width) * static_cast<size_t>(config_.height) * 4;
    size_t written = std::fwrite(rgba_data, 1, frame_bytes, pipe_);
    return written == frame_bytes;
}

void VideoExporter::finish()
{
    if (pipe_)
    {
        pclose(pipe_);
        pipe_ = nullptr;
    }
}

}  // namespace spectra

#endif  // SPECTRA_USE_FFMPEG
