// spectra main — launches with empty welcome screen (no figure).
// Use the menu bar to create figures, load CSV, etc.

#include <spectra/app.hpp>

#if __has_include(<spectra/version.hpp>)
    #include <spectra/version.hpp>
#endif

#include <cstring>
#include <iostream>

int main(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0)
        {
#ifdef SPECTRA_VERSION_STRING
            std::cout << "spectra " << SPECTRA_VERSION_STRING << "\n";
#else
            std::cout << "spectra (version unknown)\n";
#endif
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
        {
            std::cout << "Usage: spectra [OPTIONS]\n"
                      << "\n"
                      << "GPU-accelerated scientific plotting application.\n"
                      << "\n"
                      << "Options:\n"
                      << "  --version, -v    Print version and exit\n"
                      << "  --help, -h       Show this help\n";
            return 0;
        }
    }

    spectra::App app;
    // No figures created → build_empty_ui() renders the welcome screen
    app.run();
    return 0;
}
