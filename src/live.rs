use crate::collector::{capture_snapshot, Snapshot, View};
use crate::config::{
    color_code, C_BLUE, C_BOLD, C_CYAN, C_GREEN, C_MAGENTA, C_RESET, C_YELLOW, MAX_SNAPSHOTS,
    OVERVIEW_PLOT_WIDTH,
};
use crate::render;
use crate::util;
use anyhow::Result;
use crossterm::event::{self, Event, KeyCode, KeyEvent};
use crossterm::terminal::{disable_raw_mode, enable_raw_mode};

fn fix_raw_output() {
    // crossterm raw mode disables OPOST, so \n doesn't do \r\n.
    // Re-enable OPOST so println! works correctly.
    unsafe {
        let mut termios: libc::termios = std::mem::zeroed();
        if libc::tcgetattr(libc::STDIN_FILENO, &mut termios) == 0 {
            termios.c_oflag |= libc::OPOST;
            libc::tcsetattr(libc::STDIN_FILENO, libc::TCSANOW, &mut termios);
        }
    }
}
use std::collections::VecDeque;
use std::io::{self, Write};
use std::time::Duration;

pub struct RingBuffer {
    pub buf: VecDeque<Snapshot>,
    pub browse_offset: usize,
}

impl RingBuffer {
    pub fn new() -> Self {
        Self { buf: VecDeque::with_capacity(MAX_SNAPSHOTS), browse_offset: 0 }
    }
    pub fn push(&mut self, snap: Snapshot) {
        if self.buf.len() >= MAX_SNAPSHOTS {
            self.buf.pop_front();
        }
        self.buf.push_back(snap);
    }
    pub fn len(&self) -> usize {
        self.buf.len()
    }
    pub fn get_by_offset(&self, offset: usize) -> Option<&Snapshot> {
        if offset >= self.buf.len() {
            return None;
        }
        let idx = self.buf.len() - 1 - offset;
        self.buf.get(idx)
    }
    pub fn clear(&mut self) {
        self.buf.clear();
        self.browse_offset = 0;
    }
}

pub fn format_current_time() -> String {
    let now = chrono::Local::now();
    now.format("%y/%m/%d %H:%M:%S").to_string()
}

fn print_live_header(snap: &Snapshot, ring: &RingBuffer) {
    // Only clear screen if stdout is a TTY
    if crossterm::tty::IsTty::is_tty(&io::stdout()) {
        print!("\x1b[H\x1b[2J");
        let _ = io::stdout().flush();
    }
    println!(
        "{}===============================================================",
        color_code(C_GREEN)
    );
    println!("PROC LENS - LIVE VIEW (refresh: 1s)");
    println!("Snapshot start: {}", snap.captured_at);
    println!(
        "==============================================================={}",
        color_code(C_RESET)
    );
    println!("{}PID:{} {}", color_code(C_YELLOW), color_code(C_RESET), snap.pid);
    println!(
        "{}Current section:{} {}",
        color_code(C_YELLOW),
        color_code(C_RESET),
        snap.view.name()
    );
    println!(
        "{}Snapshot index:{} {}/{}",
        color_code(C_YELLOW),
        color_code(C_RESET),
        ring.len() - ring.browse_offset,
        ring.len()
    );
    println!("Sections: [1] Memory  [2] Network  [3] Threads  [4] I/O  [5] Overview");
    println!("History: [Up/k] older  [Down/j] newer  [f] follow live");
    println!("Commands: 1/2/3/4/5 switch view, 0 change PID, Ctrl+C exit");
    if ring.browse_offset > 0 {
        println!("Mode: browsing history (auto-refresh paused)");
    } else {
        println!("Mode: live follow");
    }
    println!("---------------------------------------------------------------");
}

fn print_live_footer(captured_at: &str) {
    println!("---------------------------------------------------------------");
    println!("Snapshot end:   {}", captured_at);
    println!(
        "{}==============================================================={}",
        color_code(C_GREEN),
        color_code(C_RESET)
    );
}

fn print_live_snapshot(snap: &Snapshot, ring: &RingBuffer) {
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
    render::print_det_preamble(snap);

    match snap.view {
        View::Overview => {
            print_overview(snap, ring);
            print_live_footer(&snap.captured_at);
        }
        View::Memory => {
            render::print_memory_view(snap);
            print_live_footer(&snap.captured_at);
        }
        View::Network => {
            render::print_network_view(snap);
            print_live_footer(&snap.captured_at);
        }
        View::Io => {
            render::print_io_view(snap);
            print_live_footer(&snap.captured_at);
        }
        View::Threads => {
            println!(
                "{}===============================================================",
                color_code(C_MAGENTA)
            );
            println!("THREAD INFORMATION");
            println!(
                "==============================================================={}",
                color_code(C_RESET)
            );
            render::print_threads_view(snap);
            print_live_footer(&snap.captured_at);
        }
    }
}

fn print_overview(snap: &Snapshot, ring: &RingBuffer) {
    // Collect series for sparklines
    let (cpu_vals, rss_vals, rx_vals, tx_vals, rd_vals, wr_vals) = collect_overview_series(ring);

    let cpu_plot = util::render_sparkline(&cpu_vals);
    let rss_plot = util::render_sparkline(&rss_vals);
    let rx_plot = util::render_sparkline(&rx_vals);
    let tx_plot = util::render_sparkline(&tx_vals);
    let rd_plot = util::render_sparkline(&rd_vals);
    let wr_plot = util::render_sparkline(&wr_vals);

    let rss_label = util::format_compact_size(snap.memory_pressure.rss_kb * 1024, None);
    // Current rates are last delta
    let cur_rx = rx_vals.last().copied().unwrap_or(0);
    let cur_tx = tx_vals.last().copied().unwrap_or(0);
    let cur_rd = rd_vals.last().copied().unwrap_or(0);
    let cur_wr = wr_vals.last().copied().unwrap_or(0);
    let rx_label = util::format_compact_size(cur_rx, Some("/s"));
    let tx_label = util::format_compact_size(cur_tx, Some("/s"));
    let rd_label = util::format_compact_size(cur_rd, Some("/s"));
    let wr_label = util::format_compact_size(cur_wr, Some("/s"));

    println!("{}TRENDS{}", color_code(C_CYAN), color_code(C_RESET));
    if cpu_vals.is_empty() {
        println!("  collecting samples...");
        println!();
    } else {
        println!(
            "{}  CPU%     |{}|  {:>4}.{:02}%{}",
            color_code(C_CYAN),
            cpu_plot,
            snap.basic.cpu_permyriad / 100,
            snap.basic.cpu_permyriad % 100,
            color_code(C_RESET)
        );
        println!();
        println!(
            "{}  RSS      |{}|  {}{}",
            color_code(C_CYAN),
            rss_plot,
            rss_label,
            color_code(C_RESET)
        );
        println!();
        println!(
            "{}  RX/s     |{}|  {}{}",
            color_code(C_CYAN),
            rx_plot,
            rx_label,
            color_code(C_RESET)
        );
        println!();
        println!(
            "{}  TX/s     |{}|  {}{}",
            color_code(C_CYAN),
            tx_plot,
            tx_label,
            color_code(C_RESET)
        );
        println!();
        println!(
            "{}  RD/s     |{}|  {}{}",
            color_code(C_CYAN),
            rd_plot,
            rd_label,
            color_code(C_RESET)
        );
        println!();
        println!(
            "{}  WR/s     |{}|  {}{}",
            color_code(C_CYAN),
            wr_plot,
            wr_label,
            color_code(C_RESET)
        );
        println!();
    }

    // Memory snapshot
    println!("{}MEMORY SNAPSHOT{}", color_code(C_BLUE), color_code(C_RESET));
    println!(
        "{}  RSS (Resident):  {} KB{}",
        color_code(C_YELLOW),
        snap.memory_pressure.rss_kb,
        color_code(C_RESET)
    );
    println!(
        "{}  VSZ (Virtual):   {} KB{}",
        color_code(C_YELLOW),
        snap.memory_pressure.vsz_kb,
        color_code(C_RESET)
    );
    println!(
        "{}  Swap Usage:      {} KB{}",
        color_code(C_YELLOW),
        snap.memory_pressure.swap_kb,
        color_code(C_RESET)
    );
    println!("  Page Faults:");
    println!(
        "{}    - Major:       {}{}",
        color_code(C_CYAN),
        snap.memory_pressure.major_faults,
        color_code(C_RESET)
    );
    println!(
        "{}    - Minor:       {}{}",
        color_code(C_CYAN),
        snap.memory_pressure.minor_faults,
        color_code(C_RESET)
    );
    println!();

    // Network snapshot
    println!("{}NETWORK SNAPSHOT{}", color_code(C_GREEN), color_code(C_RESET));
    println!(
        "{}sockets_total: {} (tcp: {}, udp: {}, unix: {}){}",
        color_code(C_YELLOW),
        snap.network_brief.sockets_total,
        snap.network_brief.tcp_count,
        snap.network_brief.udp_count,
        snap.network_brief.unix_count,
        color_code(C_RESET)
    );
    println!(
        "{}rx_bytes: {}{}",
        color_code(C_YELLOW),
        snap.network_brief.rx_bytes,
        color_code(C_RESET)
    );
    println!(
        "{}tx_bytes: {}{}",
        color_code(C_YELLOW),
        snap.network_brief.tx_bytes,
        color_code(C_RESET)
    );
    println!(
        "{}tcp_retransmits: {}{}",
        color_code(C_YELLOW),
        snap.network_brief.tcp_retransmits,
        color_code(C_RESET)
    );
    println!("{}drops: {}{}", color_code(C_YELLOW), snap.network_brief.drops, color_code(C_RESET));
    println!("  Top talkers:");
    if snap.network_brief.top_talkers.is_empty() {
        println!("  none");
    } else {
        println!(
            "{}  RANK  FD   PROTO   FAMILY      RX_BYTES   TX_BYTES   TOTAL_BYTES{}",
            color_code(C_CYAN),
            color_code(C_RESET)
        );
        println!(
            "{}  ----  ---  ------  ----------  ---------  ---------  -----------{}",
            color_code(C_CYAN),
            color_code(C_RESET)
        );
        for t in &snap.network_brief.top_talkers {
            println!(
                "{}  #{:<3}  {:<3}  {:<6}  {:<10}  {:<9}  {:<9}  {:<9}{}",
                color_code(C_MAGENTA),
                t.rank,
                t.fd,
                t.proto,
                t.family,
                t.rx_bytes,
                t.tx_bytes,
                t.total_bytes,
                color_code(C_RESET)
            );
        }
    }
    println!();

    // IO snapshot
    println!("{}I/O SNAPSHOT{}", color_code(C_GREEN), color_code(C_RESET));
    if !snap.io_stats.available {
        println!(
            "{}status: unavailable (CONFIG_TASK_XACCT disabled){}",
            color_code(C_CYAN),
            color_code(C_RESET)
        );
    } else {
        println!(
            "{}read_bytes: {}{}",
            color_code(C_YELLOW),
            util::format_size_with_unit(snap.io_stats.read_bytes),
            color_code(C_RESET)
        );
        println!(
            "{}write_bytes: {}{}",
            color_code(C_YELLOW),
            util::format_size_with_unit(snap.io_stats.write_bytes),
            color_code(C_RESET)
        );
        println!("{}syscr: {}{}", color_code(C_YELLOW), snap.io_stats.syscr, color_code(C_RESET));
        println!("{}syscw: {}{}", color_code(C_YELLOW), snap.io_stats.syscw, color_code(C_RESET));
        println!(
            "{}io_intensity: {}{}",
            color_code(C_YELLOW),
            util::format_size_with_unit(snap.io_stats.io_intensity),
            color_code(C_RESET)
        );
    }
    println!();

    // Thread hotspots
    println!("{}THREAD HOTSPOTS{}", color_code(C_MAGENTA), color_code(C_RESET));
    println!(
        "{}Total threads: {}{}",
        color_code(C_YELLOW),
        snap.threads.len(),
        color_code(C_RESET)
    );
    if snap.threads.is_empty() {
        println!("  Top threads by CPU: unavailable");
    } else {
        // Top 3 by cpu
        let mut sorted = snap.threads.clone();
        sorted.sort_by(|a, b| b.cpu_permyriad.cmp(&a.cpu_permyriad));
        println!("  Top threads by CPU:");
        println!(
            "{}  TID    NAME             CPU(%)   STATE  PRIORITY  NICE  CPU_AFFINITY{}",
            color_code(C_CYAN),
            color_code(C_RESET)
        );
        println!(
            "{}  -----  ---------------  -------  -----  --------  ----  ----------------{}",
            color_code(C_CYAN),
            color_code(C_RESET)
        );
        for t in sorted.iter().take(3) {
            println!(
                "{}  {:<5}  {:<15}  {:>4}.{:02}   {}      {:>4}      {:>4}  {}{}",
                color_code(C_MAGENTA),
                t.tid,
                &t.name[..t.name.len().min(15)],
                t.cpu_permyriad / 100,
                t.cpu_permyriad % 100,
                t.state,
                t.priority,
                t.nice,
                t.affinity,
                color_code(C_RESET)
            );
        }
    }
}

fn collect_overview_series(
    ring: &RingBuffer,
) -> (Vec<u64>, Vec<u64>, Vec<u64>, Vec<u64>, Vec<u64>, Vec<u64>) {
    let mut cpu = Vec::new();
    let mut rss = Vec::new();
    let mut rx_rate = Vec::new();
    let mut tx_rate = Vec::new();
    let mut rd_rate = Vec::new();
    let mut wr_rate = Vec::new();

    let snaps: Vec<&Snapshot> = ring.buf.iter().collect();
    let start =
        if snaps.len() > OVERVIEW_PLOT_WIDTH { snaps.len() - OVERVIEW_PLOT_WIDTH } else { 0 };

    let mut prev_rx: Option<u64> = None;
    let mut prev_tx: Option<u64> = None;
    let mut prev_rd: Option<u64> = None;
    let mut prev_wr: Option<u64> = None;

    for snap in &snaps[start..] {
        cpu.push(snap.basic.cpu_permyriad);
        rss.push(snap.memory_pressure.rss_kb);

        let cur_rx = snap.network_brief.rx_bytes;
        let cur_tx = snap.network_brief.tx_bytes;
        let cur_rd = snap.io_stats.read_bytes;
        let cur_wr = snap.io_stats.write_bytes;

        rx_rate.push(prev_rx.map(|p| cur_rx.saturating_sub(p)).unwrap_or(0));
        tx_rate.push(prev_tx.map(|p| cur_tx.saturating_sub(p)).unwrap_or(0));
        rd_rate.push(prev_rd.map(|p| cur_rd.saturating_sub(p)).unwrap_or(0));
        wr_rate.push(prev_wr.map(|p| cur_wr.saturating_sub(p)).unwrap_or(0));

        prev_rx = Some(cur_rx);
        prev_tx = Some(cur_tx);
        prev_rd = Some(cur_rd);
        prev_wr = Some(cur_wr);
    }

    (cpu, rss, rx_rate, tx_rate, rd_rate, wr_rate)
}

fn prompt_for_pid() -> Option<String> {
    print!(">> Enter process ID: ");
    let _ = io::stdout().flush();
    let mut line = String::new();
    match io::stdin().read_line(&mut line) {
        Ok(0) => return None, // EOF
        Ok(_) => {}
        Err(_) => return None,
    }
    let pid = line.trim().to_string();
    if pid.is_empty() {
        None
    } else {
        Some(pid)
    }
}

pub fn run_live() -> Result<()> {
    let mut pid = match prompt_for_pid() {
        Some(p) => p,
        None => {
            eprintln!("invalid input");
            return Ok(());
        }
    };
    let mut view = View::Overview;
    let mut ring = RingBuffer::new();

    if enable_raw_mode().is_err() {
        // Not a TTY — run in non-interactive mode: capture one snapshot and display
        if let Ok(snap) = capture_snapshot(&pid, view) {
            ring.push(snap);
            if let Some(current) = ring.get_by_offset(0) {
                print_live_header(current, &ring);
                print_live_snapshot(current, &ring);
            }
        }
        return Ok(());
    }
    fix_raw_output();

    // Ensure raw mode is disabled on exit
    let result = (|| -> Result<()> {
        loop {
            if ring.browse_offset == 0 {
                if let Ok(snap) = capture_snapshot(&pid, view) {
                    ring.push(snap);
                }
            }

            if let Some(current) = ring.get_by_offset(ring.browse_offset) {
                print_live_header(current, &ring);
                print_live_snapshot(current, &ring);
                let _ = io::stdout().flush();
            }

            // Poll for input with 1s timeout
            if !event::poll(Duration::from_secs(1))? {
                continue;
            }

            let ev = event::read()?;
            let key = match ev {
                Event::Key(KeyEvent { code, .. }) => code,
                _ => continue,
            };

            match key {
                KeyCode::Char('1') => {
                    view = View::Memory;
                    ring.clear();
                }
                KeyCode::Char('2') => {
                    view = View::Network;
                    ring.clear();
                }
                KeyCode::Char('3') => {
                    view = View::Threads;
                    ring.clear();
                }
                KeyCode::Char('4') => {
                    view = View::Io;
                    ring.clear();
                }
                KeyCode::Char('5') => {
                    view = View::Overview;
                    ring.clear();
                }
                KeyCode::Char('k') | KeyCode::Up => {
                    if ring.browse_offset + 1 < ring.len() {
                        ring.browse_offset += 1;
                    }
                }
                KeyCode::Char('j') | KeyCode::Down => {
                    if ring.browse_offset > 0 {
                        ring.browse_offset -= 1;
                    }
                }
                KeyCode::Char('f') => {
                    ring.browse_offset = 0;
                }
                KeyCode::Char('0') => {
                    disable_raw_mode()?;
                    if let Some(new_pid) = prompt_for_pid() {
                        pid = new_pid;
                    } else {
                        eprintln!("invalid PID");
                    }
                    enable_raw_mode()?;
                    fix_raw_output();
                    ring.clear();
                }
                KeyCode::Esc => break,
                KeyCode::Char('c') => {
                    // Check for Ctrl+C via crossterm - it sends Char('c') with modifiers
                    // crossterm handles Ctrl+C as Char('c') with CONTROL modifier, but we check via event
                    // For simplicity, treat 'q' as quit too
                    break;
                }
                _ => {}
            }
        }
        Ok(())
    })();

    let _ = disable_raw_mode();
    result
}
