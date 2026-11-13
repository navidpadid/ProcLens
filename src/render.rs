use crate::collector::{Snapshot, View};
use crate::config::{color_code, C_BLUE, C_BOLD, C_CYAN, C_GREEN, C_MAGENTA, C_RESET, C_YELLOW};
use crate::util;

pub fn print_logo() {
    println!("{}{}", color_code(C_CYAN), color_code(C_BOLD));
    println!(" /$$$$$$$                               /$$                                    ");
    println!("| $$__  $$                             | $$                                    ");
    println!("| $$  \\ $$ /$$$$$$   /$$$$$$   /$$$$$$$| $$        /$$$$$$  /$$$$$$$   /$$$$$$$");
    println!("| $$$$$$$//$$__  $$ /$$__  $$ /$$_____/| $$       /$$__  $$| $$__  $$ /$$_____/");
    println!("| $$____/| $$  \\__/| $$  \\ $$| $$      | $$      | $$$$$$$$| $$  \\ $$|  $$$$$$ ");
    println!("| $$     | $$      | $$  | $$| $$      | $$      | $$_____/| $$  | $$ \\____  $$");
    println!("| $$     | $$      |  $$$$$$/|  $$$$$$$| $$$$$$$$|  $$$$$$$| $$  | $$ /$$$$$$$/");
    println!("|__/     |__/       \\______/  \\_______/|________/ \\_______/|__/  |__/|_______/ ");
    println!("                                                                               ");
    println!(
        "latest release:{} https://github.com/navidpadid/ProcLens/releases/latest",
        color_code(C_RESET)
    );
    println!("version:{} {}", color_code(C_RESET), env!("CARGO_PKG_VERSION"));
    println!("like ProcLens?{} consider giving it a star on GitHub.", color_code(C_RESET));
    println!("{}", color_code(C_RESET));
}

pub fn print_process_info(snap: &Snapshot) {
    println!();
    println!(
        "{}===============================================================",
        color_code(C_CYAN)
    );
    println!("PROCESS INFORMATION");
    println!(
        "==============================================================={}",
        color_code(C_RESET)
    );
    if !snap.basic.cmdline.is_empty() {
        println!(
            "{}Command line:{}   {}",
            color_code(C_YELLOW),
            color_code(C_RESET),
            snap.basic.cmdline
        );
    }
    print_det_content(snap);
    println!();
    println!(
        "{}===============================================================",
        color_code(C_MAGENTA)
    );
    println!("THREAD INFORMATION");
    println!(
        "==============================================================={}",
        color_code(C_RESET)
    );
    print_threads(snap);
    println!("===============================================================");
}

fn print_det_content(snap: &Snapshot) {
    // Basic
    println!("Process ID:      {}", snap.basic.pid);
    println!("Name:            {}", snap.basic.comm);
    println!(
        "CPU Usage:       {}.{:02}%",
        snap.basic.cpu_permyriad / 100,
        snap.basic.cpu_permyriad % 100
    );

    // Memory pressure
    print_memory_pressure(snap);
    // Memory layout
    print_memory_layout(snap);
    // Network
    print_network(snap);
    // Sockets
    print_sockets(snap);
    // IO
    print_io(snap);
}

fn print_memory_pressure(snap: &Snapshot) {
    let mp = &snap.memory_pressure;
    println!();
    println!("Memory Pressure Statistics:");
    println!("-----------------------------------------------------------------");
    println!("  RSS (Resident):  {} KB", mp.rss_kb);
    println!("    - Anonymous:   {} KB", mp.anon_kb);
    println!("    - File-backed: {} KB", mp.file_kb);
    println!("    - Shared Mem:  {} KB", mp.shmem_kb);
    println!("  VSZ (Virtual):   {} KB", mp.vsz_kb);
    println!("  Swap Usage:      {} KB", mp.swap_kb);
    println!("  Page Faults:");
    println!("    - Major:       {}", mp.major_faults);
    println!("    - Minor:       {}", mp.minor_faults);
    println!("    - Total:       {}", mp.major_faults + mp.minor_faults);
    println!("  OOM Score Adj:   {}", mp.oom_score_adj);
    println!("-----------------------------------------------------------------");
}

fn print_memory_layout(snap: &Snapshot) {
    let ml = &snap.memory_layout;
    println!();
    println!("Memory Layout:");
    println!("-----------------------------------------------------------------");
    println!("  Code Section:    0x{:016x} - 0x{:016x}", ml.code_start, ml.code_end);
    println!("  Data Section:    0x{:016x} - 0x{:016x}", ml.data_start, ml.data_end);
    println!("  BSS Section:     0x{:016x} - 0x{:016x}", ml.bss_start, ml.bss_end);
    println!("  Heap:            0x{:016x} - 0x{:016x}", ml.heap_start, ml.heap_end);
    println!("  Stack:           0x{:016x} - 0x{:016x}", ml.stack_start, ml.stack_end);
    println!("  ELF Base:        0x{:016x}", ml.elf_base);

    // Visualization
    let regions = [
        util::MemoryRegion {
            name: "CODE",
            size: ml.code_end.saturating_sub(ml.code_start),
            exists: ml.code_end > ml.code_start,
        },
        util::MemoryRegion {
            name: "DATA",
            size: ml.data_end.saturating_sub(ml.data_start),
            exists: ml.data_end > ml.data_start,
        },
        util::MemoryRegion {
            name: "BSS",
            size: ml.bss_end.saturating_sub(ml.bss_start),
            exists: ml.bss_end > ml.bss_start,
        },
        util::MemoryRegion {
            name: "HEAP",
            size: ml.heap_end.saturating_sub(ml.heap_start),
            exists: ml.heap_end > ml.heap_start,
        },
        util::MemoryRegion {
            name: "STACK",
            size: ml.stack_start.saturating_sub(ml.stack_end),
            exists: ml.stack_start > ml.stack_end,
        },
    ];
    let total: u64 = regions.iter().filter(|r| r.exists).map(|r| r.size).sum();
    if total > 0 {
        println!();
        println!("Memory Layout Visualization:");
        println!("-----------------------------------------------------------------");
        println!("Low:  0x{:016x}", ml.code_start);
        println!();
        for r in &regions {
            let w = util::calculate_bar_width(r.size, total, 50);
            if let Some(viz) = util::generate_region_visualization(r, w, 50) {
                print!("{}", viz);
            }
        }
        println!("High: 0x{:016x}", ml.stack_start);
        println!("-----------------------------------------------------------------");
    }
}

fn print_network(snap: &Snapshot) {
    let nb = &snap.network_brief;
    println!();
    println!("[network]");
    println!(
        "sockets_total: {} (tcp: {}, udp: {}, unix: {})",
        nb.sockets_total, nb.tcp_count, nb.udp_count, nb.unix_count
    );
    println!("rx_packets: {}", nb.rx_packets);
    println!("tx_packets: {}", nb.tx_packets);
    println!("rx_bytes: {}", nb.rx_bytes);
    println!("tx_bytes: {}", nb.tx_bytes);
    println!("tcp_retransmits: {}", nb.tcp_retransmits);
    println!("drops: {}", nb.drops);
    if nb.net_devices.is_empty() {
        println!("net_devices: none");
    } else {
        let devs: Vec<String> =
            nb.net_devices.iter().map(|(n, c)| format!("{}={}", n, c)).collect();
        println!("net_devices: {}", devs.join(" "));
    }
    println!("top_talkers:");
    if nb.top_talkers.is_empty() {
        println!("  none");
    } else {
        for t in &nb.top_talkers {
            println!(
                "  #{} FD {} Proto: {:<5} Family: {:<10} RX bytes={} TX bytes={} Total bytes={}",
                t.rank, t.fd, t.proto, t.family, t.rx_bytes, t.tx_bytes, t.total_bytes
            );
        }
    }
}

fn print_sockets(snap: &Snapshot) {
    println!();
    println!("Open Sockets:");
    println!("-----------------------------------------------------------------");
    if snap.sockets.is_empty() {
        println!("  No open sockets");
    } else {
        for s in &snap.sockets {
            println!(
                "  [FD {}] Family: {:<10} Type: {:<8} State: {:<12} Proto: {:<5}",
                s.fd, s.family, s.sock_type, s.state, s.proto
            );
            if s.proto == "TCP" || s.proto == "UDP" {
                let queued = if s.proto == "UDP" { " (queued)" } else { "" };
                println!(
                    "          Traffic: RX pkts={} bytes={}  TX pkts={} bytes={}{}",
                    s.rx_packets, s.rx_bytes, s.tx_packets, s.tx_bytes, queued
                );
            }
            println!("          Local:  {}  Remote: {}", s.local, s.remote);
        }
    }
    println!("-----------------------------------------------------------------");
}

fn print_io(snap: &Snapshot) {
    println!();
    println!("[io]");
    if !snap.io_stats.available {
        println!("status: unavailable (CONFIG_TASK_XACCT disabled)");
        return;
    }
    let io = &snap.io_stats;
    println!("rchar: {}", util::format_size_with_unit(io.rchar));
    println!("wchar: {}", util::format_size_with_unit(io.wchar));
    println!("syscr: {}", io.syscr);
    println!("syscw: {}", io.syscw);
    println!("read_bytes: {}", util::format_size_with_unit(io.read_bytes));
    println!("write_bytes: {}", util::format_size_with_unit(io.write_bytes));
    println!("cancelled_write_bytes: {}", util::format_size_with_unit(io.cancelled_write_bytes));
    println!(
        "avg_read_bytes_per_syscall: {}",
        util::format_size_with_unit(io.avg_read_per_syscall)
    );
    println!(
        "avg_write_bytes_per_syscall: {}",
        util::format_size_with_unit(io.avg_write_per_syscall)
    );
    println!("io_intensity: {}", util::format_size_with_unit(io.io_intensity));
}

fn print_threads(snap: &Snapshot) {
    println!("TID    NAME             CPU(%)   STATE  PRIORITY  NICE  CPU_AFFINITY");
    println!("-----  ---------------  -------  -----  --------  ----  ----------------");
    for t in &snap.threads {
        println!(
            "{:<5}  {:<15}  {:>4}.{:02}   {}      {:>4}      {:>4}  {}",
            t.tid,
            &t.name[..t.name.len().min(15)],
            t.cpu_permyriad / 100,
            t.cpu_permyriad % 100,
            t.state,
            t.priority,
            t.nice,
            t.affinity
        );
    }
    println!("-----------------------------------------------------------------");
    println!("Total threads: {}", snap.threads.len());
}

// ── Live helpers ──
pub fn print_det_preamble(snap: &Snapshot) {
    println!("Process ID:      {}", snap.basic.pid);
    println!("Name:            {}", snap.basic.comm);
    println!(
        "CPU Usage:       {}.{:02}%",
        snap.basic.cpu_permyriad / 100,
        snap.basic.cpu_permyriad % 100
    );
}

pub fn print_memory_view(snap: &Snapshot) {
    print_det_preamble(snap);
    print_memory_pressure(snap);
    print_memory_layout(snap);
}

pub fn print_network_view(snap: &Snapshot) {
    print_network(snap);
    print_sockets(snap);
}

pub fn print_io_view(snap: &Snapshot) {
    print_io(snap);
}

pub fn print_threads_view(snap: &Snapshot) {
    print_threads(snap);
}
