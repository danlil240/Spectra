#pragma once

// Coverage raster for DenseLineMode::Density.
//
// Drawing hundreds of translucent segments per pixel into an 8-bit colour
// target rounds every blend to 1/255, so the colour stalls about 0.5 / alpha
// levels short of the line colour, at a point that depends on the driver's
// blend rounding.  Instead the renderer counts, per pixel, how many segments
// cover it (fractional at the anti-aliased ends) and draws one texture whose
// alpha is 1 - (1 - a)^count: a single blend per pixel, the same on every GPU.

#include <cstdint>
#include <vector>

namespace spectra::data
{

class LineDensityGrid
{
   public:
    // Clear to an empty cols x rows grid.  Pixel (c, r) covers x in [c, c + 1)
    // and y in [r, r + 1), with row 0 at the bottom.
    void reset(uint32_t cols, uint32_t rows);

    // Add one segment (pixel coordinates, any direction) drawn `half_width`
    // pixels to each side.  Each pixel column the segment crosses gets the
    // segment's y extent within the column, widened by half_width, with
    // fractional coverage at both ends.  Parts outside the grid are clipped;
    // segments with a non-finite end are skipped.
    void add_segment(double x0, double y0, double x1, double y1, double half_width);

    // Coverage count of a pixel (sum over segments of the covered fraction).
    float coverage(uint32_t col, uint32_t row) const;

    // Straight-alpha RGBA8, row 0 (bottom) first: every pixel is (r, g, b) with
    // alpha 1 - (1 - alpha)^coverage.  r, g, b, alpha in [0, 1].
    void to_rgba(float r, float g, float b, float alpha, std::vector<uint8_t>& out) const;

    uint32_t cols() const { return cols_; }
    uint32_t rows() const { return rows_; }

   private:
    void add_span(uint32_t col, double lo, double hi);

    uint32_t cols_ = 0;
    uint32_t rows_ = 0;
    // Per column, rows_ + 1 entries of a difference array: the coverage of
    // row r is the prefix sum of entries 0..r.
    std::vector<float> diff_;
};

}   // namespace spectra::data
