// empty_launch_csv.cpp — Launch Spectra with an empty figure ready for data.
// Use Data -> Load from CSV to browse, pick columns, and plot.

#include <spectra/app.hpp>

int main()
{
    spectra::App app;
    auto& fig = app.figure();
    fig.subplot(1, 1, 1);   // Empty axes — ready for CSV data
    app.run();
    return 0;
}
