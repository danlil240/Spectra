// Runtime robustness tests that need no GPU: export request validation,
// README easy-API snippet compilation, IPC socket directory / path handling.

#include <gtest/gtest.h>

#include <spectra/easy.hpp>
#include <spectra/figure.hpp>

#include "env_guard.hpp"
#include "ipc/transport.hpp"

#include <chrono>
#include <filesystem>
#include <string>

#ifndef _WIN32
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace fs = std::filesystem;
using spectra::test::EnvGuard;

namespace
{

fs::path make_temp_dir(const std::string& tag)
{
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path   dir =
        fs::temp_directory_path() / ("spectra_robust_" + tag + "_" + std::to_string(stamp));
    fs::create_directories(dir);
    return dir;
}

// ── README "5-Second Demo" snippet ──────────────────────────────────────────
// Compiled (never executed: calling it would create the global App and need
// a GPU) to guarantee the documented braced-list form stays valid C++20.
[[maybe_unused]] void readme_first_example()
{
    spectra::plot({0.f, 1.f, 2.f, 3.f, 4.f}, {0.f, 1.f, 0.5f, 1.5f, 2.f}, "c-o");
    spectra::title("Hello Spectra");
    // spectra::show();  -- intentionally not called

    // Other braced-list entry points.
    spectra::scatter({1.f, 2.f}, {3.f, 4.f});
    spectra::bar({1.f, 2.f}, {3.f, 4.f});
    spectra::histogram({1.f, 2.f, 2.f, 3.f}, 3);
    spectra::plot3({0.f, 1.f}, {0.f, 1.f}, {0.f, 1.f});
    spectra::scatter3({0.f, 1.f}, {0.f, 1.f}, {0.f, 1.f});

    // Existing span-based call sites must remain unambiguous.
    std::vector<float> x{0.f, 1.f}, y{1.f, 2.f};
    spectra::plot(x, y);
    spectra::plot(x, y, "r--");
    spectra::scatter(x, y);
    spectra::histogram(x);
    [[maybe_unused]] bool queued = spectra::save_png("out.png");
}

}   // namespace

// The same calls as constraints, so a regression fails at compile time with a
// clear message even if the function above were optimized away.
static_assert(requires { spectra::plot({0.f, 1.f}, {0.f, 1.f}, "c-o"); });
static_assert(requires { spectra::plot({0.f, 1.f}, {0.f, 1.f}); });
static_assert(requires { spectra::scatter({0.f, 1.f}, {0.f, 1.f}); });

// ── Figure export validation ────────────────────────────────────────────────

TEST(FigureExport, SavePngRejectsMissingDirectory)
{
    spectra::Figure fig;
    const auto      bogus = fs::temp_directory_path() / "spectra_no_such_dir_xyz" / "a" / "b.png";
    EXPECT_FALSE(fig.save_png(bogus.string()));
    EXPECT_TRUE(fig.export_req_.png_path.empty());
    EXPECT_FALSE(fig.save_png(bogus.string(), 640, 480));
    EXPECT_TRUE(fig.export_req_.png_path.empty());
}

TEST(FigureExport, SavePngRejectsEmptyPathAndDirectory)
{
    spectra::Figure fig;
    EXPECT_FALSE(fig.save_png(""));
    EXPECT_FALSE(fig.save_png(fs::temp_directory_path().string()));
    EXPECT_TRUE(fig.export_req_.png_path.empty());
}

TEST(FigureExport, SaveSvgRejectsMissingDirectory)
{
    spectra::Figure fig;
    EXPECT_FALSE(fig.save_svg("/nonexistent_spectra_dir/plot.svg"));
    EXPECT_TRUE(fig.export_req_.svg_path.empty());
}

#ifndef _WIN32
TEST(FigureExport, SavePngRejectsUnwritableDirectory)
{
    if (::geteuid() == 0)
        GTEST_SKIP() << "root bypasses directory permissions";
    auto dir = make_temp_dir("ro");
    fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec);
    spectra::Figure fig;
    EXPECT_FALSE(fig.save_png((dir / "x.png").string()));
    fs::permissions(dir, fs::perms::owner_all);
    fs::remove_all(dir);
}
#endif

TEST(FigureExport, SaveAcceptsWritableDirectory)
{
    auto            dir = make_temp_dir("ok");
    spectra::Figure fig;
    const auto      png = (dir / "x.png").string();
    const auto      svg = (dir / "x.svg").string();
    EXPECT_TRUE(fig.save_png(png, 320, 200));
    EXPECT_EQ(fig.export_req_.png_path, png);
    EXPECT_EQ(fig.export_req_.png_width, 320u);
    EXPECT_TRUE(fig.save_svg(svg));
    EXPECT_EQ(fig.export_req_.svg_path, svg);
    fs::remove_all(dir);
}

// ── IPC runtime directory / socket paths ────────────────────────────────────

TEST(IpcPaths, RuntimeDirPrefersXdgRuntimeDir)
{
    EnvGuard xdg("XDG_RUNTIME_DIR");
    xdg.set("/run/user/12345");
    EXPECT_EQ(spectra::ipc::runtime_dir(), "/run/user/12345");
    EXPECT_EQ(spectra::ipc::default_socket_path().rfind("/run/user/12345/spectra-", 0), 0u);
}

TEST(IpcPaths, RuntimeDirFallsBackToTempDirectory)
{
    EnvGuard xdg("XDG_RUNTIME_DIR");
    xdg.unset();
    std::string expected = fs::temp_directory_path().string();
    while (expected.size() > 1 && (expected.back() == '/' || expected.back() == '\\'))
        expected.pop_back();
    EXPECT_EQ(spectra::ipc::runtime_dir(), expected);
}

#ifndef _WIN32
TEST(IpcPaths, ServerRejectsOverlongSocketPath)
{
    const size_t max_len = spectra::ipc::max_socket_path_length();
    ASSERT_GT(max_len, 0u);
    std::string path = fs::temp_directory_path().string() + "/" + std::string(max_len, 'x');
    spectra::ipc::Server server;
    EXPECT_FALSE(server.listen(path));
    EXPECT_EQ(spectra::ipc::Client::connect(path), nullptr);
}

TEST(IpcPaths, SocketIsOwnerOnly)
{
    auto dir = make_temp_dir("sock");
    // Keep the path short (sun_path is ~104-108 bytes).
    std::string path = (dir / "s.sock").string();
    if (path.size() > spectra::ipc::max_socket_path_length())
        GTEST_SKIP() << "temp dir path too long for a Unix socket";

    spectra::ipc::Server server;
    ASSERT_TRUE(server.listen(path));
    struct stat st
    {
    };
    ASSERT_EQ(::lstat(path.c_str(), &st), 0);
    EXPECT_TRUE(S_ISSOCK(st.st_mode));
    EXPECT_EQ(st.st_mode & 077, 0u) << std::oct << st.st_mode;
    server.close();
    fs::remove_all(dir);
}
#endif
