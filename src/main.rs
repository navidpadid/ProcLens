use clap::Parser;

mod collector;
mod config;
mod live;
mod procfs;
mod render;
mod util;

#[derive(Parser, Debug)]
#[command(name = "proclens", version, about = "Pure userspace process inspector")]
struct Args {
    /// PID to inspect (one-shot mode). Omit for live dashboard.
    pid: Option<String>,
}

fn print_usage() {
    println!("Usage: proclens [OPTIONS] [PID]");
    println!();
    println!("Options:");
    println!("  -h, --help       Show this help message and exit");
    println!("  -v, --version    Show version and exit");
    println!();
    println!("Arguments:");
    println!("  PID              Show one-shot process info for the given PID");
    println!();
    println!("Live mode (no arguments):");
    println!("  Auto-refreshes every 1s. Controls shown in-app:");
    println!("    1  Memory section");
    println!("    2  Network section");
    println!("    3  Threads section");
    println!("    4  I/O section");
    println!("    5  Overview section");
    println!("    0  Change PID");
    println!("    Up/k  Older snapshot   Down/j  Newer snapshot");
    println!("    f     Resume live follow");
    println!("    Ctrl+C  Exit");
}

fn main() {
    let args = Args::parse();

    // Handle --help / --version via clap automatically
    if let Some(pid_str) = args.pid {
        // Validate PID
        let pid = if pid_str.len() >= config::PID_INPUT_MAX {
            pid_str[..config::PID_INPUT_MAX - 1].to_string()
        } else {
            pid_str.clone()
        };

        // Check process exists
        let status_path = config::proc_root().join(&pid).join("status");
        if !status_path.exists() {
            eprintln!("error: no such process: {}", pid);
            eprintln!("hint: check PID or try sudo for other users' processes");
            std::process::exit(1);
        }

        render::print_logo();
        match collector::capture_snapshot(&pid, collector::View::Overview) {
            Ok(snap) => render::print_process_info(&snap),
            Err(e) => {
                eprintln!("error: failed to capture snapshot: {}", e);
                std::process::exit(1);
            }
        }
    } else {
        // Live mode
        render::print_logo();
        if let Err(e) = live::run_live() {
            eprintln!("error: {}", e);
            std::process::exit(1);
        }
    }
}
