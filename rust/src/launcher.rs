//! Find a running Spectra broker, or start `spectra-backend`.
//!
//! Mirrors `python/spectra/_launcher.py` and the C++ publisher discovery in
//! `src/ipc/publisher_client.cpp`, so Rust, Python and C++ clients all meet
//! on the same broker socket.

use std::collections::HashSet;
use std::env;
use std::fs;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::thread;
use std::time::{Duration, Instant, SystemTime};

use crate::error::{Error, Result};
use crate::transport::Stream;

const EXE: &str = if cfg!(windows) { ".exe" } else { "" };

/// Directory the C++ side puts broker sockets in (`ipc::runtime_dir()`).
pub(crate) fn runtime_socket_dir() -> PathBuf {
    if cfg!(windows) {
        let temp = env::var_os("TEMP")
            .or_else(|| env::var_os("TMP"))
            .map(PathBuf::from)
            .unwrap_or_else(env::temp_dir);
        return temp.join("spectra");
    }
    if let Some(xdg) = env::var_os("XDG_RUNTIME_DIR") {
        let xdg = PathBuf::from(xdg);
        if !xdg.as_os_str().is_empty() && socket_path_fits(&xdg.join("spectra-4194304.sock")) {
            return xdg;
        }
    }
    posix_temp_dir()
}

fn posix_temp_dir() -> PathBuf {
    for var in ["TMPDIR", "TMP", "TEMP", "TEMPDIR"] {
        if let Some(v) = env::var_os(var) {
            let p = PathBuf::from(v);
            if p.is_dir() {
                return p;
            }
            break;
        }
    }
    PathBuf::from("/tmp")
}

fn sun_path_max() -> usize {
    if cfg!(target_os = "linux") {
        108
    } else {
        104
    }
}

fn socket_path_fits(path: &Path) -> bool {
    cfg!(windows) || path.as_os_str().len() < sun_path_max()
}

/// Connect, then check the server did not accept-and-close (a dying backend).
pub(crate) fn can_connect(path: &str) -> bool {
    let Ok(mut s) = Stream::connect(path) else {
        return false;
    };
    let _ = s.set_read_timeout(Some(Duration::from_millis(50)));
    let mut b = [0u8; 1];
    !matches!(s.read(&mut b), Ok(0))
}

fn broker_sockets(dir: &Path) -> Vec<(SystemTime, String)> {
    let Ok(entries) = fs::read_dir(dir) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for e in entries.flatten() {
        let name = e.file_name().to_string_lossy().into_owned();
        if !name.starts_with("spectra-") || !name.ends_with(".sock") {
            continue;
        }
        if name.starts_with("spectra-auto-") {
            continue; // UI automation socket, not the broker
        }
        let mtime = e
            .metadata()
            .and_then(|m| m.modified())
            .unwrap_or(SystemTime::UNIX_EPOCH);
        out.push((mtime, e.path().to_string_lossy().into_owned()));
    }
    out
}

/// Live broker sockets in the runtime directory, newest first.
pub(crate) fn discover_live_brokers() -> Vec<String> {
    let mut socks = broker_sockets(&runtime_socket_dir());
    socks.sort_by_key(|s| std::cmp::Reverse(s.0));
    socks
        .into_iter()
        .map(|(_, p)| p)
        .filter(|p| can_connect(p))
        .collect()
}

/// Candidate socket paths in order of preference.
pub(crate) fn socket_candidates(explicit: Option<&str>) -> Vec<String> {
    if let Some(p) = explicit.filter(|p| !p.is_empty()) {
        return vec![p.to_string()];
    }
    if let Ok(p) = env::var("SPECTRA_SOCKET") {
        if !p.is_empty() {
            return vec![p];
        }
    }
    discover_live_brokers()
}

/// Preferred socket path, or "" when no broker is running yet.
pub(crate) fn resolve_socket_path(explicit: Option<&str>) -> String {
    socket_candidates(explicit)
        .into_iter()
        .next()
        .unwrap_or_default()
}

fn is_executable(p: &Path) -> bool {
    if !p.is_file() {
        return false;
    }
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        fs::metadata(p)
            .map(|m| m.permissions().mode() & 0o111 != 0)
            .unwrap_or(false)
    }
    #[cfg(not(unix))]
    {
        true
    }
}

/// ELF, Mach-O or PE executable (not a script wrapper such as a pip shim).
fn is_native_binary(p: &Path) -> bool {
    let Ok(mut f) = fs::File::open(p) else {
        return false;
    };
    let mut m = [0u8; 4];
    if f.read_exact(&mut m).is_err() {
        return false;
    }
    m == *b"\x7fELF"
        || [
            [0xfe, 0xed, 0xfa, 0xce],
            [0xfe, 0xed, 0xfa, 0xcf],
            [0xcf, 0xfa, 0xed, 0xfe],
            [0xce, 0xfa, 0xed, 0xfe],
        ]
        .contains(&m)
        || m[..2] == *b"MZ"
}

/// Locate the `spectra-backend` executable.
///
/// Order: `$SPECTRA_BACKEND_PATH`, `$SPECTRA_BACKEND`, next to the running
/// executable, a `build/` directory of the Spectra checkout this crate was
/// built from, then `PATH`.
pub(crate) fn find_backend_binary() -> Option<PathBuf> {
    for var in ["SPECTRA_BACKEND_PATH", "SPECTRA_BACKEND"] {
        if let Some(v) = env::var_os(var) {
            let p = PathBuf::from(v);
            if is_executable(&p) {
                return Some(p);
            }
        }
    }
    let name = format!("spectra-backend{EXE}");
    if let Ok(exe) = env::current_exe() {
        if let Some(dir) = exe.parent() {
            let p = dir.join(&name);
            if is_executable(&p) {
                return Some(p);
            }
        }
    }
    // Developer checkout: <repo>/rust is this crate, <repo>/build the C++ build.
    let repo = Path::new(env!("CARGO_MANIFEST_DIR")).join("..");
    for dir in [
        "build",
        "build_release",
        "build-release",
        "build_debug",
        "build-debug",
        "out/build",
    ] {
        let p = repo.join(dir).join(&name);
        if is_executable(&p) {
            return Some(p);
        }
    }
    if let Some(path) = env::var_os("PATH") {
        for dir in env::split_paths(&path) {
            let p = dir.join(&name);
            if is_executable(&p) && is_native_binary(&p) {
                return Some(p);
            }
        }
    }
    None
}

fn install_hint() -> &'static str {
    "Install Spectra (a release package, Homebrew/Scoop, or `pip install spectra-plot`, which \
     bundles the backend), put spectra-backend on PATH, or set SPECTRA_BACKEND_PATH to the \
     executable."
}

fn read_tail(path: &Path, max: u64) -> String {
    let Ok(mut f) = fs::File::open(path) else {
        return String::new();
    };
    let len = f.metadata().map(|m| m.len()).unwrap_or(0);
    let _ = f.seek(SeekFrom::Start(len.saturating_sub(max)));
    let mut s = String::new();
    let mut buf = Vec::new();
    if f.read_to_end(&mut buf).is_ok() {
        s = String::from_utf8_lossy(&buf).trim_end().to_string();
    }
    s
}

fn current_brokers() -> HashSet<String> {
    broker_sockets(&runtime_socket_dir())
        .into_iter()
        .map(|(_, p)| p)
        .collect()
}

/// Make sure a backend is listening. Returns the socket path in use.
pub(crate) fn ensure_backend(socket_path: &str, timeout: Duration) -> Result<String> {
    ensure_backend_excluding(socket_path, timeout, &HashSet::new())
}

/// [`ensure_backend`], never reusing a socket in `exclude` (for example an
/// in-process app's topic broker, which cannot host figures).
pub(crate) fn ensure_backend_excluding(
    socket_path: &str,
    timeout: Duration,
    exclude: &HashSet<String>,
) -> Result<String> {
    let socket_path = if exclude.contains(socket_path) {
        ""
    } else {
        socket_path
    };
    if !socket_path.is_empty() && can_connect(socket_path) {
        return Ok(socket_path.to_string());
    }
    if let Some(found) = discover_live_brokers()
        .into_iter()
        .find(|p| !exclude.contains(p))
    {
        if socket_path.is_empty() || found == socket_path {
            return Ok(found);
        }
    }

    let binary = find_backend_binary().ok_or_else(|| {
        Error::BackendNotFound(format!(
            "spectra-backend{EXE} was not found. {}",
            install_hint()
        ))
    })?;

    let mut socket_path = socket_path.to_string();
    if !socket_path.is_empty() && !socket_path_fits(Path::new(&socket_path)) {
        return Err(Error::invalid(format!(
            "socket path is too long for a Unix domain socket ({} bytes, limit {}): {socket_path}",
            socket_path.len(),
            sun_path_max() - 1
        )));
    }
    if socket_path.is_empty() && !cfg!(windows) {
        if let Some(xdg) = env::var_os("XDG_RUNTIME_DIR") {
            if runtime_socket_dir().as_path() != std::path::Path::new(&xdg) {
                // XDG_RUNTIME_DIR too long for sun_path: pin a short path instead.
                socket_path = runtime_socket_dir()
                    .join(format!("spectra-{}.sock", std::process::id()))
                    .to_string_lossy()
                    .into_owned();
            }
        }
    }
    if !socket_path.is_empty() {
        if let Some(dir) = Path::new(&socket_path).parent() {
            let _ = fs::create_dir_all(dir);
        }
        let _ = fs::remove_file(&socket_path);
    }

    let (log_path, owns_log) = match env::var_os("SPECTRA_DEBUG_LOG") {
        Some(p) => (PathBuf::from(p), false),
        None => (
            env::temp_dir().join(format!(
                "spectra-backend-{}-{}.log",
                std::process::id(),
                SystemTime::now()
                    .duration_since(SystemTime::UNIX_EPOCH)
                    .map(|d| d.as_nanos())
                    .unwrap_or(0)
            )),
            true,
        ),
    };
    let log_file = fs::File::create(&log_path)?;

    let before = if socket_path.is_empty() {
        current_brokers()
    } else {
        HashSet::new()
    };

    let mut cmd = Command::new(&binary);
    if !socket_path.is_empty() {
        cmd.arg("--socket").arg(&socket_path);
    }
    cmd.stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::from(log_file));
    #[cfg(unix)]
    {
        use std::os::unix::process::CommandExt;
        cmd.process_group(0); // outlive Ctrl-C of the client, like start_new_session
    }
    #[cfg(windows)]
    {
        use std::os::windows::process::CommandExt;
        cmd.creation_flags(0x0800_0000 | 0x0000_0200); // CREATE_NO_WINDOW | NEW_PROCESS_GROUP
    }
    let mut child = cmd
        .spawn()
        .map_err(|e| Error::BackendNotFound(format!("cannot start {}: {e}", binary.display())))?;

    let started = |path: String| -> Result<String> {
        if owns_log && cfg!(unix) {
            let _ = fs::remove_file(&log_path);
        }
        Ok(path)
    };

    let deadline = Instant::now() + timeout;
    while Instant::now() < deadline {
        if !socket_path.is_empty() {
            if can_connect(&socket_path) {
                return started(socket_path);
            }
        } else {
            let mut fresh: Vec<_> = current_brokers().difference(&before).cloned().collect();
            fresh.sort();
            if let Some(p) = fresh.into_iter().find(|p| can_connect(p)) {
                return started(p);
            }
        }
        if let Ok(Some(_)) = child.try_wait() {
            break;
        }
        thread::sleep(Duration::from_millis(50));
    }

    let status = child.try_wait().ok().flatten();
    let tail = read_tail(&log_path, 8192);
    if owns_log {
        let _ = fs::remove_file(&log_path);
    }
    let what = match status {
        Some(s) => format!("spectra-backend exited during startup ({s})"),
        None => format!("spectra-backend did not start within {timeout:?}"),
    };
    let mut msg = format!("{what}. Binary: {}", binary.display());
    if !tail.is_empty() {
        msg.push_str("\nBackend stderr:\n");
        msg.push_str(&tail);
    }
    Err(Error::BackendNotFound(msg))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn explicit_socket_wins() {
        assert_eq!(
            socket_candidates(Some("/tmp/x.sock")),
            vec!["/tmp/x.sock".to_string()]
        );
    }

    #[test]
    fn runtime_dir_is_absolute() {
        assert!(runtime_socket_dir().is_absolute());
    }

    #[test]
    fn dead_socket_is_not_connectable() {
        assert!(!can_connect("/nonexistent/spectra-1.sock"));
    }
}
