//! Plot data input: anything that can be read as a list of numbers.

use std::borrow::Cow;

/// A number that can be plotted.
pub trait Scalar: Copy {
    /// The value as `f64`.
    fn to_f64(self) -> f64;
}

macro_rules! scalar {
    ($($t:ty),*) => {$(
        impl Scalar for $t {
            #[inline]
            fn to_f64(self) -> f64 {
                self as f64
            }
        }
    )*};
}
scalar!(f32, f64, i8, i16, i32, i64, isize, u8, u16, u32, u64, usize);

impl Scalar for bool {
    #[inline]
    fn to_f64(self) -> f64 {
        if self {
            1.0
        } else {
            0.0
        }
    }
}

/// A one-dimensional sequence of plot values.
///
/// Implemented for slices, arrays and `Vec`s of any [`Scalar`] (all integer
/// and float types), and for 1-D `ndarray` arrays with the `ndarray` feature.
/// Pass it by reference: `ax.line(&x, &y)`.
pub trait Values {
    /// Number of values.
    fn len(&self) -> usize;
    /// `true` when there are no values.
    fn is_empty(&self) -> bool {
        self.len() == 0
    }
    /// The values as `f64` (borrowed when they already are `f64`).
    fn as_f64(&self) -> Cow<'_, [f64]>;
}

/// Specialisation-free fast path: `f64` slices are borrowed, others converted.
trait AsF64Slice {
    fn cow(s: &[Self]) -> Cow<'_, [f64]>
    where
        Self: Sized;
}

impl<T: Scalar + 'static> AsF64Slice for T {
    fn cow(s: &[T]) -> Cow<'_, [f64]> {
        if std::any::TypeId::of::<T>() == std::any::TypeId::of::<f64>() {
            // SAFETY: T is f64, checked above; same layout and lifetime.
            let s = unsafe { std::slice::from_raw_parts(s.as_ptr() as *const f64, s.len()) };
            Cow::Borrowed(s)
        } else {
            Cow::Owned(s.iter().map(|v| v.to_f64()).collect())
        }
    }
}

impl<T: Scalar + 'static> Values for [T] {
    fn len(&self) -> usize {
        <[T]>::len(self)
    }
    fn as_f64(&self) -> Cow<'_, [f64]> {
        T::cow(self)
    }
}

impl<T: Scalar + 'static> Values for Vec<T> {
    fn len(&self) -> usize {
        Vec::len(self)
    }
    fn as_f64(&self) -> Cow<'_, [f64]> {
        T::cow(self)
    }
}

impl<T: Scalar + 'static, const N: usize> Values for [T; N] {
    fn len(&self) -> usize {
        N
    }
    fn as_f64(&self) -> Cow<'_, [f64]> {
        T::cow(self)
    }
}

impl<V: Values + ?Sized> Values for &V {
    fn len(&self) -> usize {
        (**self).len()
    }
    fn as_f64(&self) -> Cow<'_, [f64]> {
        (**self).as_f64()
    }
}

#[cfg(feature = "ndarray")]
impl<S, T> Values for ndarray::ArrayBase<S, ndarray::Ix1>
where
    S: ndarray::Data<Elem = T>,
    T: Scalar + 'static,
{
    fn len(&self) -> usize {
        ndarray::ArrayBase::len(self)
    }
    fn as_f64(&self) -> Cow<'_, [f64]> {
        match self.as_slice() {
            Some(s) => T::cow(s),
            None => Cow::Owned(self.iter().map(|v| v.to_f64()).collect()),
        }
    }
}

#[cfg(feature = "ndarray")]
impl<S, T> Values for ndarray::ArrayBase<S, ndarray::Ix2>
where
    S: ndarray::Data<Elem = T>,
    T: Scalar + 'static,
{
    /// A 2-D array is read in row-major order (used for surface heights).
    fn len(&self) -> usize {
        ndarray::ArrayBase::len(self)
    }
    fn as_f64(&self) -> Cow<'_, [f64]> {
        match self.as_slice() {
            Some(s) => T::cow(s),
            None => Cow::Owned(self.iter().map(|v| v.to_f64()).collect()),
        }
    }
}

/// `n` evenly spaced values from `start` to `stop` inclusive (like `linspace`).
pub fn linspace(start: f64, stop: f64, n: usize) -> Vec<f64> {
    match n {
        0 => Vec::new(),
        1 => vec![start],
        _ => {
            let step = (stop - start) / (n - 1) as f64;
            (0..n)
                .map(|i| {
                    if i == n - 1 {
                        stop
                    } else {
                        start + step * i as f64
                    }
                })
                .collect()
        }
    }
}

/// `0, 1, ..., n-1` as `f64`.
pub fn arange(n: usize) -> Vec<f64> {
    (0..n).map(|i| i as f64).collect()
}

// ─── Precision helpers (ports of src/core/data_origin.hpp) ───────────────────

fn f32_exact(v: f64) -> bool {
    (v as f32) as f64 == v
}

/// Origin for storing `values` as `f32` offsets without losing precision:
/// 0 while `f32` holds them well relative to their spread, otherwise the
/// centre of their range (e.g. Unix timestamps).
pub(crate) fn choose_data_origin(values: &[f64]) -> f64 {
    let mut lo = f64::INFINITY;
    let mut hi = f64::NEG_INFINITY;
    for &v in values {
        if v.is_finite() {
            lo = lo.min(v);
            hi = hi.max(v);
        }
    }
    if lo > hi {
        return 0.0;
    }
    let rng = hi - lo;
    if lo.abs().max(hi.abs()) <= 64.0 * rng {
        return 0.0;
    }
    if rng == 0.0 && f32_exact(lo) {
        return 0.0;
    }
    lo + 0.5 * rng
}

/// Origin picked from the first streamed value: values of 1000 or more become it.
pub(crate) fn choose_stream_origin(first: f64) -> f64 {
    if !first.is_finite() || first.abs() < 1000.0 {
        0.0
    } else {
        first
    }
}

/// Interleave equal-length columns as `f32`, each shifted by its origin.
pub(crate) fn interleave(cols: &[(&[f64], f64)]) -> Vec<f32> {
    let n = cols.iter().map(|(c, _)| c.len()).min().unwrap_or(0);
    let k = cols.len();
    let mut out = vec![0f32; n * k];
    for (j, (c, o)) in cols.iter().enumerate() {
        for i in 0..n {
            out[i * k + j] = (c[i] - o) as f32;
        }
    }
    out
}

/// `(min, max, smallest positive)` of the finite values with |v| < 1e11.
pub(crate) fn finite_bounds(values: &[f64]) -> Option<(f64, f64, Option<f64>)> {
    let mut lo = f64::INFINITY;
    let mut hi = f64::NEG_INFINITY;
    let mut pos: Option<f64> = None;
    for &v in values {
        if v.is_finite() && v.abs() < 1e11 {
            lo = lo.min(v);
            hi = hi.max(v);
            if v > 0.0 {
                pos = Some(pos.map_or(v, |p: f64| p.min(v)));
            }
        }
    }
    (lo <= hi).then_some((lo, hi, pos))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn values_for_common_types() {
        assert_eq!(vec![1i32, 2, 3].as_f64().as_ref(), &[1.0, 2.0, 3.0]);
        assert_eq!([0.5f32, 1.5].as_f64().as_ref(), &[0.5, 1.5]);
        let v = vec![1.0f64, 2.0];
        assert!(matches!(v.as_f64(), Cow::Borrowed(_)));
        assert_eq!(v[..].len(), 2);
    }

    #[test]
    fn origin_for_timestamps() {
        let ts = [1.7e9, 1.7e9 + 10.0];
        let o = choose_data_origin(&ts);
        assert_eq!(o, 1.7e9 + 5.0);
        assert_eq!(choose_data_origin(&[0.0, 1.0, 2.0]), 0.0);
        assert_eq!(choose_data_origin(&[f64::NAN]), 0.0);
        assert_eq!(choose_stream_origin(5.0), 0.0);
        assert_eq!(choose_stream_origin(1.7e9), 1.7e9);
    }

    #[test]
    fn interleave_shifts() {
        let x = [10.0, 11.0];
        let y = [1.0, 2.0];
        assert_eq!(
            interleave(&[(&x, 10.0), (&y, 0.0)]),
            vec![0.0, 1.0, 1.0, 2.0]
        );
    }

    #[test]
    fn bounds_skip_nan_and_sentinels() {
        assert_eq!(
            finite_bounds(&[f64::NAN, -2.0, 3.0, 1e12]),
            Some((-2.0, 3.0, Some(3.0)))
        );
        assert_eq!(finite_bounds(&[f64::NAN]), None);
    }

    #[test]
    fn linspace_endpoints() {
        let v = linspace(0.0, 1.0, 5);
        assert_eq!(v, vec![0.0, 0.25, 0.5, 0.75, 1.0]);
    }
}
