#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <spectra/series3d.hpp>

namespace spectra
{

namespace
{

void assign_coord(std::vector<float>& out, std::span<const float> in, std::atomic<bool>& dirty)
{
    out.assign(in.begin(), in.end());
    dirty.store(true, std::memory_order_release);
}

vec3 compute_centroid_xyz(const std::vector<float>& x,
                          const std::vector<float>& y,
                          const std::vector<float>& z)
{
    const size_t n = std::min({x.size(), y.size(), z.size()});
    if (n == 0)
        return {0.0f, 0.0f, 0.0f};

    vec3 sum{0.0f, 0.0f, 0.0f};
    for (size_t i = 0; i < n; ++i)
    {
        sum.x += x[i];
        sum.y += y[i];
        sum.z += z[i];
    }
    return sum / static_cast<float>(n);
}

void get_bounds_xyz(const std::vector<float>& x,
                    const std::vector<float>& y,
                    const std::vector<float>& z,
                    vec3&                     min_out,
                    vec3&                     max_out)
{
    const size_t n = std::min({x.size(), y.size(), z.size()});
    if (n == 0)
    {
        min_out = max_out = {0.0f, 0.0f, 0.0f};
        return;
    }

    min_out = {x[0], y[0], z[0]};
    max_out = min_out;

    for (size_t i = 1; i < n; ++i)
    {
        min_out.x = std::fmin(min_out.x, x[i]);
        min_out.y = std::fmin(min_out.y, y[i]);
        min_out.z = std::fmin(min_out.z, z[i]);
        max_out.x = std::fmax(max_out.x, x[i]);
        max_out.y = std::fmax(max_out.y, y[i]);
        max_out.z = std::fmax(max_out.z, z[i]);
    }
}

}   // anonymous namespace

// ─── LineSeries3D ────────────────────────────────────────────────────────────

LineSeries3D::LineSeries3D(std::span<const float> x,
                           std::span<const float> y,
                           std::span<const float> z)
{
    set_x(x);
    set_y(y);
    set_z(z);
}

LineSeries3D& LineSeries3D::set_x(std::span<const float> x)
{
    assign_coord(x_, x, dirty_);
    return *this;
}

LineSeries3D& LineSeries3D::set_y(std::span<const float> y)
{
    assign_coord(y_, y, dirty_);
    return *this;
}

LineSeries3D& LineSeries3D::set_z(std::span<const float> z)
{
    assign_coord(z_, z, dirty_);
    return *this;
}

void LineSeries3D::append(float x, float y, float z)
{
    x_.push_back(x);
    y_.push_back(y);
    z_.push_back(z);
    dirty_ = true;
}

vec3 LineSeries3D::compute_centroid() const
{
    return compute_centroid_xyz(x_, y_, z_);
}

void LineSeries3D::get_bounds(vec3& min_out, vec3& max_out) const
{
    get_bounds_xyz(x_, y_, z_, min_out, max_out);
}

// ─── ScatterSeries3D ─────────────────────────────────────────────────────────

ScatterSeries3D::ScatterSeries3D(std::span<const float> x,
                                 std::span<const float> y,
                                 std::span<const float> z)
{
    set_x(x);
    set_y(y);
    set_z(z);
}

ScatterSeries3D& ScatterSeries3D::set_x(std::span<const float> x)
{
    assign_coord(x_, x, dirty_);
    return *this;
}

ScatterSeries3D& ScatterSeries3D::set_y(std::span<const float> y)
{
    assign_coord(y_, y, dirty_);
    return *this;
}

ScatterSeries3D& ScatterSeries3D::set_z(std::span<const float> z)
{
    assign_coord(z_, z, dirty_);
    return *this;
}

void ScatterSeries3D::append(float x, float y, float z)
{
    x_.push_back(x);
    y_.push_back(y);
    z_.push_back(z);
    dirty_ = true;
}

vec3 ScatterSeries3D::compute_centroid() const
{
    return compute_centroid_xyz(x_, y_, z_);
}

void ScatterSeries3D::get_bounds(vec3& min_out, vec3& max_out) const
{
    get_bounds_xyz(x_, y_, z_, min_out, max_out);
}

// ─── SurfaceSeries ───────────────────────────────────────────────────────────

SurfaceSeries::SurfaceSeries(std::span<const float> x_grid,
                             std::span<const float> y_grid,
                             std::span<const float> z_values)
{
    set_data(x_grid, y_grid, z_values);
}

void SurfaceSeries::set_data(std::span<const float> x_grid,
                             std::span<const float> y_grid,
                             std::span<const float> z_values)
{
    x_grid_.assign(x_grid.begin(), x_grid.end());
    y_grid_.assign(y_grid.begin(), y_grid.end());
    z_values_.assign(z_values.begin(), z_values.end());

    cols_ = static_cast<int>(x_grid_.size());
    rows_ = static_cast<int>(y_grid_.size());

    mesh_generated_           = false;
    wireframe_mesh_generated_ = false;
    dirty_                    = true;
}

void SurfaceSeries::generate_mesh()
{
    if (cols_ < 2 || rows_ < 2)
    {
        mesh_generated_ = false;
        return;
    }

    mesh_.vertices.clear();
    mesh_.indices.clear();

    size_t expected_z_count = static_cast<size_t>(rows_) * static_cast<size_t>(cols_);
    if (z_values_.size() != expected_z_count)
    {
        mesh_generated_ = false;
        return;
    }

    mesh_.vertex_count = rows_ * cols_;
    mesh_.vertices.reserve(mesh_.vertex_count * 6);

    for (int i = 0; i < rows_; ++i)
    {
        for (int j = 0; j < cols_; ++j)
        {
            float x = x_grid_[j];
            float y = y_grid_[i];
            float z = z_values_[i * cols_ + j];

            // Central differences in the interior, one-sided differences on the
            // grid border (neighbour indices clamped), so every vertex gets a
            // normal that follows the surface slope.
            const int j0 = (j > 0) ? j - 1 : j;
            const int j1 = (j < cols_ - 1) ? j + 1 : j;
            const int i0 = (i > 0) ? i - 1 : i;
            const int i1 = (i < rows_ - 1) ? i + 1 : i;

            const float span_x = x_grid_[j1] - x_grid_[j0];
            const float span_y = y_grid_[i1] - y_grid_[i0];

            float dz_dx = 0.0f;
            float dz_dy = 0.0f;
            if (span_x != 0.0f)
                dz_dx = (z_values_[i * cols_ + j1] - z_values_[i * cols_ + j0]) / span_x;
            if (span_y != 0.0f)
                dz_dy = (z_values_[i1 * cols_ + j] - z_values_[i0 * cols_ + j]) / span_y;
            if (!std::isfinite(dz_dx))
                dz_dx = 0.0f;
            if (!std::isfinite(dz_dy))
                dz_dy = 0.0f;

            vec3 tangent_x{1.0f, 0.0f, dz_dx};
            vec3 tangent_y{0.0f, 1.0f, dz_dy};
            vec3 normal = vec3_normalize(vec3_cross(tangent_x, tangent_y));

            mesh_.vertices.push_back(x);
            mesh_.vertices.push_back(y);
            mesh_.vertices.push_back(z);
            mesh_.vertices.push_back(normal.x);
            mesh_.vertices.push_back(normal.y);
            mesh_.vertices.push_back(normal.z);
        }
    }

    mesh_.triangle_count = (rows_ - 1) * (cols_ - 1) * 2;
    mesh_.indices.reserve(mesh_.triangle_count * 3);

    for (int i = 0; i < rows_ - 1; ++i)
    {
        for (int j = 0; j < cols_ - 1; ++j)
        {
            uint32_t idx0 = i * cols_ + j;
            uint32_t idx1 = i * cols_ + (j + 1);
            uint32_t idx2 = (i + 1) * cols_ + j;
            uint32_t idx3 = (i + 1) * cols_ + (j + 1);

            mesh_.indices.push_back(idx0);
            mesh_.indices.push_back(idx2);
            mesh_.indices.push_back(idx1);

            mesh_.indices.push_back(idx1);
            mesh_.indices.push_back(idx2);
            mesh_.indices.push_back(idx3);
        }
    }

    mesh_generated_ = true;
}

void SurfaceSeries::generate_wireframe_mesh()
{
    if (cols_ < 2 || rows_ < 2)
    {
        wireframe_mesh_generated_ = false;
        return;
    }

    size_t expected_z_count = static_cast<size_t>(rows_) * static_cast<size_t>(cols_);
    if (z_values_.size() != expected_z_count)
    {
        wireframe_mesh_generated_ = false;
        return;
    }

    wireframe_mesh_.vertices.clear();
    wireframe_mesh_.indices.clear();

    // Reuse the same vertex layout as the solid mesh (position + normal)
    wireframe_mesh_.vertex_count = rows_ * cols_;
    wireframe_mesh_.vertices.reserve(wireframe_mesh_.vertex_count * 6);

    for (int i = 0; i < rows_; ++i)
    {
        for (int j = 0; j < cols_; ++j)
        {
            float x = x_grid_[j];
            float y = y_grid_[i];
            float z = z_values_[i * cols_ + j];

            // Normal pointing up for wireframe (not used for lighting but needed for vertex layout)
            wireframe_mesh_.vertices.push_back(x);
            wireframe_mesh_.vertices.push_back(y);
            wireframe_mesh_.vertices.push_back(z);
            wireframe_mesh_.vertices.push_back(0.0f);
            wireframe_mesh_.vertices.push_back(0.0f);
            wireframe_mesh_.vertices.push_back(1.0f);
        }
    }

    // Generate line indices: horizontal lines + vertical lines
    // Each line segment = 2 indices
    for (int i = 0; i < rows_; ++i)
    {
        for (int j = 0; j < cols_ - 1; ++j)
        {
            wireframe_mesh_.indices.push_back(static_cast<uint32_t>(i * cols_ + j));
            wireframe_mesh_.indices.push_back(static_cast<uint32_t>(i * cols_ + j + 1));
        }
    }
    for (int j = 0; j < cols_; ++j)
    {
        for (int i = 0; i < rows_ - 1; ++i)
        {
            wireframe_mesh_.indices.push_back(static_cast<uint32_t>(i * cols_ + j));
            wireframe_mesh_.indices.push_back(static_cast<uint32_t>((i + 1) * cols_ + j));
        }
    }

    wireframe_mesh_.triangle_count = 0;   // Not triangles — line segments
    wireframe_mesh_generated_      = true;
}

vec3 SurfaceSeries::compute_centroid() const
{
    if (x_grid_.empty() || y_grid_.empty() || z_values_.empty())
    {
        return {0.0f, 0.0f, 0.0f};
    }

    float x_sum = 0.0f;
    float y_sum = 0.0f;
    float z_sum = 0.0f;
    for (float x : x_grid_)
        x_sum += x;
    for (float y : y_grid_)
        y_sum += y;
    for (float z : z_values_)
        z_sum += z;

    return {x_sum / static_cast<float>(x_grid_.size()),
            y_sum / static_cast<float>(y_grid_.size()),
            z_sum / static_cast<float>(z_values_.size())};
}

void SurfaceSeries::get_bounds(vec3& min_out, vec3& max_out) const
{
    if (x_grid_.empty() || y_grid_.empty() || z_values_.empty())
    {
        min_out = max_out = {0.0f, 0.0f, 0.0f};
        return;
    }

    auto [x_min, x_max] = std::minmax_element(x_grid_.begin(), x_grid_.end());
    auto [y_min, y_max] = std::minmax_element(y_grid_.begin(), y_grid_.end());
    auto [z_min, z_max] = std::minmax_element(z_values_.begin(), z_values_.end());

    min_out = {*x_min, *y_min, *z_min};
    max_out = {*x_max, *y_max, *z_max};
}

// ─── MeshSeries ──────────────────────────────────────────────────────────────

MeshSeries::MeshSeries(std::span<const float> vertices, std::span<const uint32_t> indices)
{
    set_vertices(vertices);
    set_indices(indices);
}

void MeshSeries::set_vertices(std::span<const float> vertices)
{
    // Vertices are interleaved {x, y, z, nx, ny, nz}; drop a trailing partial
    // vertex so every consumer can read vertices_[i * 6 + k] for
    // i < vertex_count() without running past the end.  Indices that referenced
    // only complete vertices stay valid because no complete vertex is removed.
    const size_t usable = vertices.size() - vertices.size() % 6;
    vertices_.assign(vertices.begin(), vertices.begin() + static_cast<ptrdiff_t>(usable));
    dirty_ = true;
}

void MeshSeries::set_indices(std::span<const uint32_t> indices)
{
    indices_.assign(indices.begin(), indices.end());
    dirty_ = true;
}

vec3 MeshSeries::compute_centroid() const
{
    if (vertices_.size() < 6)
        return {0.0f, 0.0f, 0.0f};

    vec3   sum{0.0f, 0.0f, 0.0f};
    size_t vertex_count = vertices_.size() / 6;

    for (size_t i = 0; i < vertex_count; ++i)
    {
        sum.x += vertices_[i * 6 + 0];
        sum.y += vertices_[i * 6 + 1];
        sum.z += vertices_[i * 6 + 2];
    }

    return sum / static_cast<float>(vertex_count);
}

void MeshSeries::get_bounds(vec3& min_out, vec3& max_out) const
{
    if (vertices_.size() < 6)
    {
        min_out = max_out = {0.0f, 0.0f, 0.0f};
        return;
    }

    min_out = {vertices_[0], vertices_[1], vertices_[2]};
    max_out = min_out;

    size_t vertex_count = vertices_.size() / 6;
    for (size_t i = 1; i < vertex_count; ++i)
    {
        float x = vertices_[i * 6 + 0];
        float y = vertices_[i * 6 + 1];
        float z = vertices_[i * 6 + 2];

        min_out.x = std::fmin(min_out.x, x);
        min_out.y = std::fmin(min_out.y, y);
        min_out.z = std::fmin(min_out.z, z);
        max_out.x = std::fmax(max_out.x, x);
        max_out.y = std::fmax(max_out.y, y);
        max_out.z = std::fmax(max_out.z, z);
    }
}

// ─── Colormap Support ─────────────────────────────────────────────────────────

namespace
{

// Evaluates a degree-6 RGB polynomial c[0] + c[1] t + ... + c[6] t^6, clamped to [0, 1].
Color colormap_poly(float t, const std::array<std::array<float, 3>, 7>& c)
{
    float rgb[3];
    for (int ch = 0; ch < 3; ++ch)
    {
        float v = c[6][ch];
        for (int i = 5; i >= 0; --i)
            v = v * t + c[i][ch];
        rgb[ch] = std::fmax(0.0f, std::fmin(1.0f, v));
    }
    return {rgb[0], rgb[1], rgb[2], 1.0f};
}

}   // namespace

SurfaceSeries& SurfaceSeries::colormap(const std::string& name)
{
    if (name == "viridis")
        colormap_ = ColormapType::Viridis;
    else if (name == "plasma")
        colormap_ = ColormapType::Plasma;
    else if (name == "inferno")
        colormap_ = ColormapType::Inferno;
    else if (name == "magma")
        colormap_ = ColormapType::Magma;
    else if (name == "jet")
        colormap_ = ColormapType::Jet;
    else if (name == "coolwarm")
        colormap_ = ColormapType::Coolwarm;
    else if (name == "grayscale")
        colormap_ = ColormapType::Grayscale;
    else
        colormap_ = ColormapType::None;
    dirty_ = true;
    return *this;
}

Color SurfaceSeries::sample_colormap(ColormapType cm, float t)
{
    t = std::fmax(0.0f, std::fmin(1.0f, t));

    switch (cm)
    {
        // Viridis, plasma, inferno and magma: degree-6 polynomial fits of the matplotlib
        // maps (max error about 0.02 per channel). Keep in sync with surface3d.frag.
        case ColormapType::Viridis:
            return colormap_poly(
                t,
                {{{0.2777273272234177f, 0.005407344544966578f, 0.3340998053353061f},
                  {0.1050930431085774f, 1.404613529898575f, 1.384590162594685f},
                  {-0.3308618287255563f, 0.214847559468213f, 0.09509516302823659f},
                  {-4.634230498983486f, -5.799100973351585f, -19.33244095627987f},
                  {6.228269936347081f, 14.17993336680509f, 56.69055260068105f},
                  {4.776384997670288f, -13.74514537774601f, -65.35303263337234f},
                  {-5.435455855934631f, 4.645852612178535f, 26.3124352495832f}}});
        case ColormapType::Plasma:
            return colormap_poly(
                t,
                {{{0.05873234392399702f, 0.02333670892565664f, 0.5433401826748754f},
                  {2.176514634195958f, 0.2383834171260182f, 0.7539604599784036f},
                  {-2.689460476458034f, -7.455851135738909f, 3.110799939717086f},
                  {6.130348345893603f, 42.3461881477227f, -28.51885465332158f},
                  {-11.10743619062271f, -82.66631109428045f, 60.13984767418263f},
                  {10.02306557647065f, 71.41361770095349f, -54.07218655560067f},
                  {-3.658713842777788f, -22.93153465461149f, 18.19190778539828f}}});
        case ColormapType::Inferno:
            return colormap_poly(
                t,
                {{{0.0002189403691192265f, 0.001651004631001012f, -0.01948089843709184f},
                  {0.1065134194856116f, 0.5639564367884091f, 3.932712388889277f},
                  {11.60249308247187f, -3.972853965665698f, -15.9423941062914f},
                  {-41.70399613139459f, 17.43639888205313f, 44.35414519872813f},
                  {77.162935699427f, -33.40235894210092f, -81.80730925738993f},
                  {-71.31942824499214f, 32.62606426397723f, 73.20951985803202f},
                  {25.13112622477341f, -12.24266895238567f, -23.07032500287172f}}});
        case ColormapType::Magma:
            return colormap_poly(
                t,
                {{{-0.002136485053939582f, -0.000749655052795221f, -0.005386127855323933f},
                  {0.2516605407371642f, 0.6775232436837668f, 2.494026599312351f},
                  {8.353717279216625f, -3.577719514958484f, 0.3144679030132573f},
                  {-27.66873308576866f, 14.26473078096533f, -13.64921318813922f},
                  {52.17613981234068f, -27.94360607168351f, 12.94416944238394f},
                  {-50.76852536473588f, 29.04658282127291f, 4.23415299384598f},
                  {18.65570506591883f, -11.48977351997711f, -5.601961508734096f}}});
        case ColormapType::Jet:
        {
            // Classic jet: blue → cyan → green → yellow → red
            float r = std::fmax(0.0f, std::fmin(1.0f, 1.5f - std::fabs(t - 0.75f) * 4.0f));
            float g = std::fmax(0.0f, std::fmin(1.0f, 1.5f - std::fabs(t - 0.5f) * 4.0f));
            float b = std::fmax(0.0f, std::fmin(1.0f, 1.5f - std::fabs(t - 0.25f) * 4.0f));
            return {r, g, b, 1.0f};
        }
        case ColormapType::Coolwarm:
        {
            // Cool (blue) to warm (red) diverging
            float r = std::fmax(0.0f, std::fmin(1.0f, 0.23f + 1.5f * t - 0.7f * t * t));
            float g = std::fmax(0.0f, std::fmin(1.0f, 0.3f + 1.2f * t - 1.5f * t * t));
            float b = std::fmax(0.0f, std::fmin(1.0f, 0.75f - 0.5f * t - 0.2f * t * t));
            return {r, g, b, 1.0f};
        }
        case ColormapType::Grayscale:
            return {t, t, t, 1.0f};
        case ColormapType::None:
        default:
            return {0.5f, 0.5f, 0.5f, 1.0f};
    }
}

}   // namespace spectra
