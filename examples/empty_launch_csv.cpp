// empty_launch_csv.cpp — Launch Spectra with no figures.
// Use File -> New Figure to create a figure.
// Use Data -> Load from CSV to browse, pick columns, and plot.

#include <spectra/app.hpp>

int main()
{
    spectra::App app;
    // No figures created — app launches with empty canvas.
    // User can create figures via File -> New Figure
    // and load data via Data -> Load from CSV.
    app.run();
    return 0;
}
