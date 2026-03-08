#include <spectra/export.hpp>

#include "io/ffmpeg_command.hpp"

// Windows does not expose POSIX popen/pclose; use the MSVC equivalents.
#if defined(_WIN32)
    #define popen  _popen
    #define pclose _pclose
#endif

#ifdef SPECTRA_USE_FFMPEG

    #include <cstdio>
    #include <stdexcept>

namespace spectra
{

VideoExporter::VideoExporter(const Config& config) : config_(config)
{
    std::string error;
    if (!detail::ensure_ffmpeg_output_parent(config_.output_path, &error))
    {
        throw std::runtime_error(error);
    }

    const detail::FfmpegCommandConfig ffmpeg_config{
        .output_path = config_.output_path,
        .width       = config_.width,
        .height      = config_.height,
        .fps         = config_.fps,
        .codec       = config_.codec,
        .pix_fmt     = config_.pix_fmt,
    };

    const std::string cmd = detail::build_ffmpeg_command(ffmpeg_config);
    pipe_                 = popen(cmd.c_str(), "w");
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

}   // namespace spectra

#endif   // SPECTRA_USE_FFMPEG
