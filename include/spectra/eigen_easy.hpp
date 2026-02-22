#pragma once

// ─── Spectra Easy API + Eigen ───────────────────────────────────────────────
//
// Drop-in replacement for <spectra/easy.hpp> that adds Eigen overloads to
// every free-function plotting call.  Include this instead of easy.hpp when
// working with Eigen types.
//
//   #include <spectra/eigen_easy.hpp>
//
//   Eigen::VectorXf x = Eigen::VectorXf::LinSpaced(100, 0, 6.28f);
//   Eigen::VectorXf y = x.array().sin();
//
//   spectra::plot(x, y, "r--o");
//   spectra::title("sin(x)");
//   spectra::show();
//
// ─────────────────────────────────────────────────────────────────────────────

#include <spectra/easy.hpp>
#include <spectra/eigen.hpp>

namespace spectra
{

// ─── 2D Plotting (Eigen) ────────────────────────────────────────────────────

// plot(EigenVec, EigenVec, fmt)
template <typename XDerived, typename YDerived>
auto plot(const Eigen::DenseBase<XDerived>& x,
          const Eigen::DenseBase<YDerived>& y,
          std::string_view                  fmt = "-")
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<
                            XDerived> && eigen_detail::is_eigen_float_vector_v<YDerived>,
                        LineSeries&>
{
    return detail::easy_state().ensure_axes().plot(eigen_detail::to_span(x),
                                                   eigen_detail::to_span(y),
                                                   fmt);
}

// plot(EigenVec, EigenVec, PlotStyle)
template <typename XDerived, typename YDerived>
auto plot(const Eigen::DenseBase<XDerived>& x,
          const Eigen::DenseBase<YDerived>& y,
          const PlotStyle&                  style)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<
                            XDerived> && eigen_detail::is_eigen_float_vector_v<YDerived>,
                        LineSeries&>
{
    return detail::easy_state().ensure_axes().plot(eigen_detail::to_span(x),
                                                   eigen_detail::to_span(y),
                                                   style);
}

// scatter(EigenVec, EigenVec)
template <typename XDerived, typename YDerived>
auto scatter(const Eigen::DenseBase<XDerived>& x, const Eigen::DenseBase<YDerived>& y)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<
                            XDerived> && eigen_detail::is_eigen_float_vector_v<YDerived>,
                        ScatterSeries&>
{
    return detail::easy_state().ensure_axes().scatter(eigen_detail::to_span(x),
                                                      eigen_detail::to_span(y));
}

// ─── 3D Plotting (Eigen) ────────────────────────────────────────────────────

// plot3(EigenVec, EigenVec, EigenVec)
template <typename XDerived, typename YDerived, typename ZDerived>
auto plot3(const Eigen::DenseBase<XDerived>& x,
           const Eigen::DenseBase<YDerived>& y,
           const Eigen::DenseBase<ZDerived>& z)
    -> std::enable_if_t<
        eigen_detail::is_eigen_float_vector_v<
            XDerived> && eigen_detail::is_eigen_float_vector_v<YDerived> && eigen_detail::is_eigen_float_vector_v<ZDerived>,
        LineSeries3D&>
{
    return detail::easy_state().ensure_axes3d().line3d(eigen_detail::to_span(x),
                                                       eigen_detail::to_span(y),
                                                       eigen_detail::to_span(z));
}

// scatter3(EigenVec, EigenVec, EigenVec)
template <typename XDerived, typename YDerived, typename ZDerived>
auto scatter3(const Eigen::DenseBase<XDerived>& x,
              const Eigen::DenseBase<YDerived>& y,
              const Eigen::DenseBase<ZDerived>& z)
    -> std::enable_if_t<
        eigen_detail::is_eigen_float_vector_v<
            XDerived> && eigen_detail::is_eigen_float_vector_v<YDerived> && eigen_detail::is_eigen_float_vector_v<ZDerived>,
        ScatterSeries3D&>
{
    return detail::easy_state().ensure_axes3d().scatter3d(eigen_detail::to_span(x),
                                                          eigen_detail::to_span(y),
                                                          eigen_detail::to_span(z));
}

// surf(EigenVec, EigenVec, EigenVec)
template <typename XDerived, typename YDerived, typename ZDerived>
auto surf(const Eigen::DenseBase<XDerived>& x_grid,
          const Eigen::DenseBase<YDerived>& y_grid,
          const Eigen::DenseBase<ZDerived>& z_values)
    -> std::enable_if_t<
        eigen_detail::is_eigen_float_vector_v<
            XDerived> && eigen_detail::is_eigen_float_vector_v<YDerived> && eigen_detail::is_eigen_float_vector_v<ZDerived>,
        SurfaceSeries&>
{
    return detail::easy_state().ensure_axes3d().surface(eigen_detail::to_span(x_grid),
                                                        eigen_detail::to_span(y_grid),
                                                        eigen_detail::to_span(z_values));
}

// mesh(EigenVec vertices, EigenVectorXi indices)
template <typename VDerived>
auto mesh(const Eigen::DenseBase<VDerived>& vertices, const Eigen::VectorXi& indices)
    -> std::enable_if_t<eigen_detail::is_eigen_float_vector_v<VDerived>, MeshSeries&>
{
    return detail::easy_state().ensure_axes3d().mesh(eigen_detail::to_span(vertices),
                                                     eigen_detail::to_index_span(indices));
}

}   // namespace spectra
