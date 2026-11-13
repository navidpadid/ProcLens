use std::path::PathBuf;

pub const PID_INPUT_MAX: usize = 20;
pub const PROC_BUF_SIZE: usize = 262144;
pub const MAX_SNAPSHOTS: usize = 120;
pub const OVERVIEW_PLOT_WIDTH: usize = 32;
pub const OVERVIEW_PLOT_BUF_SIZE: usize = OVERVIEW_PLOT_WIDTH * 4 + 1;

pub fn proc_root() -> PathBuf {
    if let Ok(dir) = std::env::var("PROCLENS_PROC_DIR") {
        if !dir.is_empty() {
            return PathBuf::from(dir);
        }
    }
    PathBuf::from("/proc")
}

pub fn proc_path(pid: &str, file: &str) -> PathBuf {
    proc_root().join(pid).join(file)
}

pub fn use_color() -> bool {
    if std::env::var("NO_COLOR").map(|v| !v.is_empty()).unwrap_or(false) {
        return false;
    }
    // Check if stdout is a tty
    unsafe { libc::isatty(libc::STDOUT_FILENO) == 1 }
}

pub fn color_code(code: &str) -> &str {
    if use_color() {
        code
    } else {
        ""
    }
}

pub const C_RESET: &str = "\x1b[0m";
pub const C_BOLD: &str = "\x1b[1m";
pub const C_CYAN: &str = "\x1b[36m";
pub const C_GREEN: &str = "\x1b[32m";
pub const C_YELLOW: &str = "\x1b[33m";
pub const C_MAGENTA: &str = "\x1b[35m";
pub const C_BLUE: &str = "\x1b[34m";
