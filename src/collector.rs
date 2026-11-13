use crate::procfs;
use crate::util;
use anyhow::Result;

#[derive(Debug, Clone)]
pub struct Snapshot {
    pub pid: String,
    pub view: View,
    pub captured_at: String,
    pub basic: BasicInfo,
    pub memory_pressure: MemoryPressure,
    pub memory_layout: MemoryLayout,
    pub network_brief: NetworkBrief,
    pub sockets: Vec<SocketInfo>,
    pub io_stats: IoStats,
    pub threads: Vec<ThreadInfo>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum View {
    Memory = 1,
    Network = 2,
    Threads = 3,
    Io = 4,
    Overview = 5,
}

impl View {
    pub fn from_u8(v: u8) -> Self {
        match v {
            1 => View::Memory,
            2 => View::Network,
            3 => View::Threads,
            4 => View::Io,
            _ => View::Overview,
        }
    }
    pub fn name(&self) -> &'static str {
        match self {
            View::Memory => "Memory",
            View::Network => "Network",
            View::Threads => "Threads",
            View::Io => "I/O",
            View::Overview => "Overview",
        }
    }
}

#[derive(Debug, Clone, Default)]
pub struct BasicInfo {
    pub pid: u32,
    pub comm: String,
    pub cpu_permyriad: u64,
    pub cmdline: String,
}

#[derive(Debug, Clone, Default)]
pub struct MemoryPressure {
    pub rss_kb: u64,
    pub anon_kb: u64,
    pub file_kb: u64,
    pub shmem_kb: u64,
    pub vsz_kb: u64,
    pub swap_kb: u64,
    pub major_faults: u64,
    pub minor_faults: u64,
    pub oom_score_adj: i32,
}

#[derive(Debug, Clone, Default)]
pub struct MemoryLayout {
    pub code_start: u64,
    pub code_end: u64,
    pub data_start: u64,
    pub data_end: u64,
    pub bss_start: u64,
    pub bss_end: u64,
    pub heap_start: u64,
    pub heap_end: u64,
    pub stack_start: u64,
    pub stack_end: u64,
    pub elf_base: u64,
}

#[derive(Debug, Clone, Default)]
pub struct NetworkBrief {
    pub sockets_total: usize,
    pub tcp_count: usize,
    pub udp_count: usize,
    pub unix_count: usize,
    pub rx_packets: u64,
    pub tx_packets: u64,
    pub rx_bytes: u64,
    pub tx_bytes: u64,
    pub tcp_retransmits: u64,
    pub drops: u64,
    pub net_devices: Vec<(String, usize)>,
    pub top_talkers: Vec<TopTalker>,
}

#[derive(Debug, Clone, Default)]
pub struct TopTalker {
    pub rank: usize,
    pub fd: u32,
    pub proto: String,
    pub family: String,
    pub rx_bytes: u64,
    pub tx_bytes: u64,
    pub total_bytes: u64,
}

#[derive(Debug, Clone, Default)]
pub struct SocketInfo {
    pub fd: u32,
    pub family: String,
    pub sock_type: String,
    pub state: String,
    pub proto: String,
    pub rx_packets: u64,
    pub rx_bytes: u64,
    pub tx_packets: u64,
    pub tx_bytes: u64,
    pub local: String,
    pub remote: String,
}

#[derive(Debug, Clone, Default)]
pub struct IoStats {
    pub rchar: u64,
    pub wchar: u64,
    pub syscr: u64,
    pub syscw: u64,
    pub read_bytes: u64,
    pub write_bytes: u64,
    pub cancelled_write_bytes: u64,
    pub avg_read_per_syscall: u64,
    pub avg_write_per_syscall: u64,
    pub io_intensity: u64,
    pub available: bool,
}

#[derive(Debug, Clone, Default)]
pub struct ThreadInfo {
    pub tid: u32,
    pub name: String,
    pub cpu_permyriad: u64,
    pub state: char,
    pub priority: i32,
    pub nice: i32,
    pub affinity: String,
}

pub fn capture_snapshot(pid: &str, view: View) -> Result<Snapshot> {
    let captured_at = crate::live::format_current_time();

    // Basic
    let stat = procfs::read_stat(pid)?;
    let cmdline = procfs::read_cmdline(pid);
    let clk = procfs::clk_tck();
    let uptime = procfs::read_uptime_secs();
    // starttime is in clock ticks since boot; convert to seconds
    let start_secs = stat.starttime as f64 / clk as f64;
    let elapsed = (uptime - start_secs).max(0.1);
    let total_ticks = stat.utime + stat.stime;
    // total_ns approx: ticks * (1e9 / clk_tck)
    let total_ns = total_ticks * 1_000_000_000 / clk;
    let delta_ns = (elapsed * 1e9) as u64;
    let cpu_permyriad = util::compute_usage_permyriad(total_ns, delta_ns);

    let basic = BasicInfo {
        pid: pid.parse().unwrap_or(0),
        comm: stat.comm.clone(),
        cpu_permyriad,
        cmdline,
    };

    // Memory pressure
    let status = procfs::read_status(pid).unwrap_or_default();
    let oom_adj = procfs::read_oom_score_adj(pid);
    let memory_pressure = MemoryPressure {
        rss_kb: status.vm_rss_kb,
        anon_kb: status.rss_anon_kb,
        file_kb: status.rss_file_kb,
        shmem_kb: status.rss_shmem_kb,
        vsz_kb: status.vm_size_kb,
        swap_kb: status.vm_swap_kb,
        major_faults: stat.majflt,
        minor_faults: stat.minflt,
        oom_score_adj: oom_adj,
    };

    // Memory layout from maps
    let maps = procfs::read_maps(pid).unwrap_or_default();
    let mut layout = MemoryLayout::default();
    // Find code/data/heap/stack/elf_base from maps
    for m in &maps {
        if m.perms.contains("x") && m.pathname.starts_with('/') && layout.code_start == 0 {
            layout.code_start = m.start;
            layout.code_end = m.end;
            if layout.elf_base == 0 {
                layout.elf_base = m.start;
            }
        }
        if m.perms == "rw-p" && m.pathname.starts_with('/') && layout.data_start == 0 {
            // heuristic: first rw-p with file is data
            layout.data_start = m.start;
            layout.data_end = m.end;
        }
        if m.pathname == "[heap]" {
            layout.heap_start = m.start;
            layout.heap_end = m.end;
        }
        if m.pathname == "[stack]" {
            layout.stack_start = m.end; // stack grows down, start is high
            layout.stack_end = m.start;
        }
    }
    // BSS: try to infer as gap between data_end and heap_start, or 0
    if layout.data_end != 0 && layout.heap_start != 0 && layout.heap_start >= layout.data_end {
        layout.bss_start = layout.data_end;
        layout.bss_end = layout.heap_start;
    }
    // Fallback elf_base
    if layout.elf_base == 0 {
        if let Some(first) = maps.first() {
            layout.elf_base = first.start;
        }
    }

    // IO
    let io_raw = procfs::read_io(pid);
    let io_stats = if io_raw.available {
        IoStats {
            rchar: io_raw.rchar,
            wchar: io_raw.wchar,
            syscr: io_raw.syscr,
            syscw: io_raw.syscw,
            read_bytes: io_raw.read_bytes,
            write_bytes: io_raw.write_bytes,
            cancelled_write_bytes: io_raw.cancelled_write_bytes,
            avg_read_per_syscall: util::calculate_avg_bytes_per_syscall(io_raw.rchar, io_raw.syscr),
            avg_write_per_syscall: util::calculate_avg_bytes_per_syscall(
                io_raw.wchar,
                io_raw.syscw,
            ),
            io_intensity: util::calculate_io_intensity(io_raw.read_bytes, io_raw.write_bytes),
            available: true,
        }
    } else {
        IoStats { available: false, ..Default::default() }
    };

    // Sockets & network brief (best-effort via /proc/net/tcp etc, fallback to fd count)
    let (sockets, network_brief) = collect_network(pid);

    // Threads
    let threads = collect_threads(pid, &stat);

    Ok(Snapshot {
        pid: pid.to_string(),
        view,
        captured_at,
        basic,
        memory_pressure,
        memory_layout: layout,
        network_brief,
        sockets,
        io_stats,
        threads,
    })
}

fn collect_network(pid: &str) -> (Vec<SocketInfo>, NetworkBrief) {
    // Count sockets via /proc/pid/fd
    let fd_path = crate::config::proc_root().join(pid).join("fd");
    let mut sockets = Vec::new();
    let mut tcp_count = 0;
    let mut udp_count = 0;
    let mut unix_count = 0;
    let mut total = 0;

    if let Ok(entries) = std::fs::read_dir(&fd_path) {
        for e in entries.flatten() {
            let target =
                std::fs::read_link(e.path()).ok().and_then(|p| p.to_str().map(|s| s.to_string()));
            if let Some(t) = target {
                if t.starts_with("socket:[") {
                    total += 1;
                    // Try to determine type via fd number
                    let fd: u32 = e.file_name().to_str().and_then(|n| n.parse().ok()).unwrap_or(0);
                    // Heuristic: check /proc/pid/net/tcp for inode match would need netlink
                    // For now, count as generic
                    sockets.push(SocketInfo {
                        fd,
                        family: "UNKNOWN".to_string(),
                        sock_type: "UNKNOWN".to_string(),
                        state: "UNKNOWN".to_string(),
                        proto: "OTHER".to_string(),
                        rx_packets: 0,
                        rx_bytes: 0,
                        tx_packets: 0,
                        tx_bytes: 0,
                        local: "0.0.0.0:0".to_string(),
                        remote: "0.0.0.0:0".to_string(),
                    });
                } else if t.starts_with("anon_inode:[eventpoll]") || t.contains("socket") {
                    // ignore
                }
            }
        }
    }

    // Try to parse /proc/pid/net/tcp for more detail (if accessible)
    // For simplicity, we keep generic counts; detailed parsing is best-effort
    let brief = NetworkBrief {
        sockets_total: total,
        tcp_count,
        udp_count,
        unix_count,
        rx_packets: 0,
        tx_packets: 0,
        rx_bytes: 0,
        tx_bytes: 0,
        tcp_retransmits: 0,
        drops: 0,
        net_devices: Vec::new(),
        top_talkers: Vec::new(),
    };

    (sockets, brief)
}

fn collect_threads(pid: &str, stat: &procfs::Stat) -> Vec<ThreadInfo> {
    let tids = procfs::read_task_pids(pid);
    let mut threads = Vec::new();
    for tid in tids {
        let ts = match procfs::read_thread_stat(pid, &tid) {
            Ok(s) => s,
            Err(_) => continue,
        };
        let affinity = procfs::read_thread_affinity(pid, &tid);
        // CPU permyriad per thread: similar to process but per-thread
        let clk = procfs::clk_tck();
        let uptime = procfs::read_uptime_secs();
        // Use thread starttime if available, else process starttime
        let total_ticks = ts.utime + ts.stime;
        let total_ns = total_ticks * 1_000_000_000 / clk.max(1);
        let elapsed = uptime.max(0.1);
        let delta_ns = (elapsed * 1e9) as u64;
        let cpu_permyriad = util::compute_usage_permyriad(total_ns, delta_ns);

        threads.push(ThreadInfo {
            tid: tid.parse().unwrap_or(0),
            name: ts.comm,
            cpu_permyriad,
            state: util::thread_state_char_from_str(&ts.state.to_string()),
            priority: ts.priority,
            nice: ts.nice,
            affinity,
        });
    }
    // Fallback: if no task dir, use main stat
    if threads.is_empty() {
        threads.push(ThreadInfo {
            tid: pid.parse().unwrap_or(0),
            name: stat.comm.clone(),
            cpu_permyriad: 0,
            state: util::thread_state_char_from_str(&stat.state.to_string()),
            priority: 0,
            nice: 0,
            affinity: "none".to_string(),
        });
    }
    threads
}
