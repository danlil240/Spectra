#include <spectra/easy.hpp>
#include <vector>

int main()
{
    spectra::figure(800, 600);

    std::vector<float> x = {1, 2, 3, 4, 5};
    std::vector<float> y = {2, 4, 1, 5, 3};
    spectra::plot(x, y, "b-").label("Test Line");
    spectra::legend();

    // Run the application - this will display the new modern menubar
    spectra::show();

    return 0;
}
