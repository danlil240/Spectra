// spectra main — launches with empty welcome screen (no figure).
// Use the menu bar to create figures, load CSV, etc.

#include <spectra/app.hpp>

int main()
{
    spectra::App app;
    // No figures created → build_empty_ui() renders the welcome screen
    app.run();
    return 0;
}
