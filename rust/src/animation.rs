//! Animation helpers: backend-driven animations and a frame pacer.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::{Duration, Instant};

use crate::codec;
use crate::error::Result;
use crate::figure::Figure;
use crate::protocol as p;
use crate::session::Session;

/// A running animation (see [`Figure::animate`]).
///
/// Dropping it does not stop the animation; call [`Animator::stop`].
pub struct Animator {
    figure: Figure,
    running: Arc<AtomicBool>,
}

impl Animator {
    pub(crate) fn new(figure: Figure, running: Arc<AtomicBool>) -> Self {
        Animator { figure, running }
    }

    /// Whether the animation is still running (not stopped, duration not over).
    pub fn is_running(&self) -> bool {
        self.running.load(Ordering::SeqCst)
    }

    /// Stop the frames and remove the callback.
    pub fn stop(&self) -> Result<()> {
        if !self.running.swap(false, Ordering::SeqCst) {
            return Ok(());
        }
        let id = self.figure.id();
        self.figure.session.unregister_animator(id);
        self.figure
            .session
            .request(p::REQ_ANIM_STOP, &codec::encode_anim_stop(id))?;
        Ok(())
    }
}

/// Paces a client-driven update loop at a fixed frame rate while handling
/// backend events.
///
/// ```no_run
/// # use spectra_plot::*;
/// # fn main() -> Result<()> {
/// let session = Session::new()?;
/// let fig = session.figure("live")?;
/// let line = fig.subplot(1, 1, 1)?.line(&[0.0], &[0.0])?;
/// fig.show()?;
/// let mut pacer = FramePacer::new(60.0);
/// let mut t = 0.0;
/// while fig.is_visible() {
///     t += pacer.pace(&session)?;
///     line.append(&[t], &[t.sin()])?;
/// }
/// # Ok(()) }
/// ```
pub struct FramePacer {
    interval: Duration,
    last: Instant,
}

impl FramePacer {
    /// A pacer for `fps` frames per second.
    pub fn new(fps: f64) -> Self {
        FramePacer {
            interval: Duration::from_secs_f64(1.0 / fps.max(1.0)),
            last: Instant::now(),
        }
    }

    /// Target frames per second.
    pub fn fps(&self) -> f64 {
        1.0 / self.interval.as_secs_f64()
    }

    /// Change the target frame rate.
    pub fn set_fps(&mut self, fps: f64) {
        self.interval = Duration::from_secs_f64(1.0 / fps.max(1.0));
    }

    /// Wait for the next frame (handling events) and return the seconds
    /// since the previous call.
    pub fn pace(&mut self, session: &Session) -> Result<f64> {
        let elapsed = self.last.elapsed();
        if elapsed < self.interval {
            session.sleep(self.interval - elapsed)?;
        }
        let now = Instant::now();
        let dt = now.duration_since(self.last).as_secs_f64();
        self.last = now;
        Ok(dt)
    }
}
