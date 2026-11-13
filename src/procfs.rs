use anyhow::{Context, Result};
use std::collections::HashMap;
use std::path::PathBuf;

use crate::config::proc_root;

fn read_file(path: &PathBuf) -> Result<String> {
    std::fs::read_to_string(path).with_context(|| format!("failed to read {}", path.display()))
}

fn proc_file(pid: &str, name: &str) -> PathBuf {
    proc_root().join(pid).join(name)
}

// ── Status ──
#[derive(Debug, Default, Clone)]
pub struct Status {
    pub vm_rss_kb: u64,
    pub vm_size_kb: u64,
    pub vm_swap_kb: u64,
    pub rss_anon_kb: u64,
    pub rss_file_kb: u64,
    pub rss_shmem_kb: u64,
}

fn parse_kb_value(line: &str) -> Option<u64> {
    // e.g. "VmRSS:    1234 kB"
    let parts: Vec<&str> = line.split_whitespace().collect();
    if parts.len() >= 2 {
        parts[1].parse().ok()
    } else {
        None
    }
}

pub fn read_status(pid: &str) -> Result<Status> {
    let path = proc_file(pid, "status");
    let content = read_file(&path)?;
    let mut s = Status::default();
    for line in content.lines() {
        if line.starts_with("VmRSS:") {
            if let Some(v) = parse_kb_value(line) {
                s.vm_rss_kb = v;
            }
        } else if line.starts_with("VmSize:") {
            if let Some(v) = parse_kb_value(line) {
                s.vm_size_kb = v;
            }
        } else if line.starts_with("VmSwap:") {
            if let Some(v) = parse_kb_value(line) {
                s.vm_swap_kb = v;
            }
        } else if line.starts_with("RssAnon:") {
            if let Some(v) = parse_kb_value(line) {
                s.rss_anon_kb = v;
            }
        } else if line.starts_with("RssFile:") {
            if let Some(v) = parse_kb_value(line) {
                s.rss_file_kb = v;
            }
        } else if line.starts_with("RssShmem:") {
            if let Some(v) = parse_kb_value(line) {
                s.rss_shmem_kb = v;
            }
        }
    }
    Ok(s)
}

// ── Stat ──
#[derive(Debug, Default, Clone)]
pub struct Stat {
    pub comm: String,
    pub state: char,
    pub minflt: u64,
    pub majflt: u64,
    pub utime: u64,
    pub stime: u64,
    pub num_threads: u64,
    pub starttime: u64,
}

pub fn read_stat(pid: &str) -> Result<Stat> {
    let path = proc_file(pid, "stat");
    let content = read_file(&path)?;
    // comm is in parentheses and may contain spaces/parens
    // format: pid (comm) state ppid ...
    let lparen = content.find('(').context("no '(' in stat")?;
    let rparen = content.rfind(')').context("no ')' in stat")?;
    let comm = content[lparen + 1..rparen].to_string();
    let rest = content[rparen + 2..].trim(); // skip ") "
    let fields: Vec<&str> = rest.split_whitespace().collect();
    // fields[0]=state, [7]=minflt, [9]=majflt, [11]=utime, [12]=stime, [17]=num_threads, [19]=starttime
    // indices relative to after comm
    let mut s = Stat { comm, ..Default::default() };
    if fields.len() > 19 {
        s.state = fields[0].chars().next().unwrap_or('?');
        s.minflt = fields[7].parse().unwrap_or(0);
        s.majflt = fields[9].parse().unwrap_or(0);
        s.utime = fields[11].parse().unwrap_or(0);
        s.stime = fields[12].parse().unwrap_or(0);
        s.num_threads = fields[17].parse().unwrap_or(0);
        s.starttime = fields[19].parse().unwrap_or(0);
    }
    Ok(s)
}

// ── Statm ──
#[derive(Debug, Default, Clone)]
pub struct Statm {
    pub size_pages: u64,
    pub resident_pages: u64,
}

pub fn read_statm(pid: &str) -> Result<Statm> {
    let path = proc_file(pid, "statm");
    let content = read_file(&path)?;
    let fields: Vec<&str> = content.split_whitespace().collect();
    let mut s = Statm::default();
    if fields.len() >= 2 {
        s.size_pages = fields[0].parse().unwrap_or(0);
        s.resident_pages = fields[1].parse().unwrap_or(0);
    }
    Ok(s)
}

// ── IO ──
#[derive(Debug, Default, Clone)]
pub struct IoStats {
    pub rchar: u64,
    pub wchar: u64,
    pub syscr: u64,
    pub syscw: u64,
    pub read_bytes: u64,
    pub write_bytes: u64,
    pub cancelled_write_bytes: u64,
    pub available: bool,
}

pub fn read_io(pid: &str) -> IoStats {
    let path = proc_file(pid, "io");
    let content = match std::fs::read_to_string(&path) {
        Ok(c) => c,
        Err(_) => return IoStats { available: false, ..Default::default() },
    };
    let mut io = IoStats { available: true, ..Default::default() };
    for line in content.lines() {
        let mut parts = line.splitn(2, ':');
        let key = parts.next().unwrap_or("").trim();
        let val = parts.next().unwrap_or("").trim();
        let v: u64 = val.parse().unwrap_or(0);
        match key {
            "rchar" => io.rchar = v,
            "wchar" => io.wchar = v,
            "syscr" => io.syscr = v,
            "syscw" => io.syscw = v,
            "read_bytes" => io.read_bytes = v,
            "write_bytes" => io.write_bytes = v,
            "cancelled_write_bytes" => io.cancelled_write_bytes = v,
            _ => {}
        }
    }
    io
}

// ── Cmdline ──
pub fn read_cmdline(pid: &str) -> String {
    let path = proc_file(pid, "cmdline");
    match std::fs::read(&path) {
        Ok(bytes) if !bytes.is_empty() => {
            let mut s = String::new();
            for b in bytes {
                if b == 0 {
                    s.push(' ');
                } else {
                    s.push(b as char);
                }
            }
            s.trim().to_string()
        }
        _ => String::new(),
    }
}

// ── Maps ──
#[derive(Debug, Clone)]
pub struct MapEntry {
    pub start: u64,
    pub end: u64,
    pub perms: String,
    pub pathname: String,
}

pub fn read_maps(pid: &str) -> Result<Vec<MapEntry>> {
    let path = proc_file(pid, "maps");
    let content = read_file(&path)?;
    let mut entries = Vec::new();
    for line in content.lines() {
        // format: 00400000-0040b000 r--p 00000000 00:00 0  /usr/bin/foo
        // or:     7f...-7f... rw-p 00000000 00:00 0  [heap]
        let mut parts = line.split_whitespace();
        let range = parts.next().unwrap_or("");
        let perms = parts.next().unwrap_or("").to_string();
        // skip offset, dev, inode
        parts.next();
        parts.next();
        parts.next();
        let pathname = parts.next().unwrap_or("").to_string();
        if let Some(dash) = range.find('-') {
            let start = u64::from_str_radix(&range[..dash], 16).unwrap_or(0);
            let end = u64::from_str_radix(&range[dash + 1..], 16).unwrap_or(0);
            entries.push(MapEntry { start, end, perms, pathname });
        }
    }
    Ok(entries)
}

// ── OOM Score Adj ──
pub fn read_oom_score_adj(pid: &str) -> i32 {
    let path = proc_file(pid, "oom_score_adj");
    std::fs::read_to_string(&path).ok().and_then(|s| s.trim().parse().ok()).unwrap_or(0)
}

// ── Task PIDs ──
pub fn read_task_pids(pid: &str) -> Vec<String> {
    let path = proc_root().join(pid).join("task");
    let mut tids = Vec::new();
    if let Ok(entries) = std::fs::read_dir(&path) {
        for e in entries.flatten() {
            if let Some(name) = e.file_name().to_str() {
                if name.chars().all(|c| c.is_ascii_digit()) {
                    tids.push(name.to_string());
                }
            }
        }
    }
    tids.sort_by_key(|n| n.parse::<u32>().unwrap_or(0));
    tids
}

// ── Thread stat ──
#[derive(Debug, Default, Clone)]
pub struct ThreadStat {
    pub comm: String,
    pub state: char,
    pub utime: u64,
    pub stime: u64,
    pub priority: i32,
    pub nice: i32,
}

pub fn read_thread_stat(pid: &str, tid: &str) -> Result<ThreadStat> {
    let path = proc_root().join(pid).join("task").join(tid).join("stat");
    let content = read_file(&path)?;
    let lparen = content.find('(').context("no '(' in thread stat")?;
    let rparen = content.rfind(')').context("no ')' in thread stat")?;
    let comm = content[lparen + 1..rparen].to_string();
    let rest = content[rparen + 2..].trim();
    let fields: Vec<&str> = rest.split_whitespace().collect();
    let mut s = ThreadStat { comm, ..Default::default() };
    if fields.len() > 16 {
        s.state = fields[0].chars().next().unwrap_or('?');
        s.utime = fields[11].parse().unwrap_or(0);
        s.stime = fields[12].parse().unwrap_or(0);
        s.priority = fields[15].parse().unwrap_or(0);
        s.nice = fields[16].parse().unwrap_or(0);
    }
    Ok(s)
}

pub fn read_thread_affinity(pid: &str, tid: &str) -> String {
    // Try to read affinity via sched_getaffinity if available
    // Fallback: read /proc/pid/task/tid/status Cpus_allowed_list
    let path = proc_root().join(pid).join("task").join(tid).join("status");
    if let Ok(content) = std::fs::read_to_string(&path) {
        for line in content.lines() {
            if line.starts_with("Cpus_allowed_list:") {
                let val = line["Cpus_allowed_list:".len()..].trim();
                if !val.is_empty() {
                    return val.to_string();
                }
            }
        }
    }
    "none".to_string()
}

// ── Uptime / btime ──
pub fn read_uptime_secs() -> f64 {
    std::fs::read_to_string(proc_root().join("uptime"))
        .ok()
        .and_then(|s| s.split_whitespace().next()?.parse().ok())
        .unwrap_or(0.0)
}

pub fn clk_tck() -> u64 {
    unsafe { libc::sysconf(libc::_SC_CLK_TCK) as u64 }
}

pub fn page_size() -> u64 {
    unsafe { libc::sysconf(libc::_SC_PAGESIZE) as u64 }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use tempfile::TempDir;

    fn setup_fake_proc(files: &[(&str, &str)]) -> TempDir {
        let dir = TempDir::new().unwrap();
        for (name, content) in files {
            let path = dir.path().join(name);
            if let Some(parent) = path.parent() {
                fs::create_dir_all(parent).unwrap();
            }
            fs::write(&path, content).unwrap();
        }
        dir
    }

    #[test]
    fn test_read_status() {
        let dir = setup_fake_proc(&[("123/status", "Name:\tfoo\nVmRSS:\t  1024 kB\nVmSize:\t  2048 kB\nVmSwap:\t    64 kB\nRssAnon:\t   512 kB\nRssFile:\t   256 kB\nRssShmem:\t   128 kB\n")]);
        std::env::set_var("PROCLENS_PROC_DIR", dir.path().to_str().unwrap());
        let s = read_status("123").unwrap();
        assert_eq!(s.vm_rss_kb, 1024);
        assert_eq!(s.vm_size_kb, 2048);
        assert_eq!(s.vm_swap_kb, 64);
        assert_eq!(s.rss_anon_kb, 512);
        std::env::remove_var("PROCLENS_PROC_DIR");
    }

    #[test]
    fn test_read_stat() {
        let dir = setup_fake_proc(&[("123/stat", "123 (my proc) S 1 123 123 0 -1 4194304 100 0 5 0 10 20 0 0 20 0 1 0 12345 1000 100 18446744073709551615 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0")]);
        std::env::set_var("PROCLENS_PROC_DIR", dir.path().to_str().unwrap());
        let s = read_stat("123").unwrap();
        assert_eq!(s.comm, "my proc");
        assert_eq!(s.state, 'S');
        assert_eq!(s.minflt, 100);
        assert_eq!(s.majflt, 5);
        assert_eq!(s.utime, 10);
        assert_eq!(s.stime, 20);
        std::env::remove_var("PROCLENS_PROC_DIR");
    }

    #[test]
    fn test_read_io_available() {
        let dir = setup_fake_proc(&[("123/io", "rchar: 1000\nwchar: 2000\nsyscr: 10\nsyscw: 20\nread_bytes: 4096\nwrite_bytes: 8192\ncancelled_write_bytes: 0\n")]);
        std::env::set_var("PROCLENS_PROC_DIR", dir.path().to_str().unwrap());
        let io = read_io("123");
        assert!(io.available);
        assert_eq!(io.rchar, 1000);
        assert_eq!(io.read_bytes, 4096);
        std::env::remove_var("PROCLENS_PROC_DIR");
    }

    #[test]
    fn test_read_io_unavailable() {
        let dir = TempDir::new().unwrap();
        std::env::set_var("PROCLENS_PROC_DIR", dir.path().to_str().unwrap());
        let io = read_io("999");
        assert!(!io.available);
        std::env::remove_var("PROCLENS_PROC_DIR");
    }

    #[test]
    fn test_read_maps() {
        let dir = setup_fake_proc(&[("123/maps", "00400000-0040b000 r--p 00000000 00:00 0 /usr/bin/foo\n0040b000-00410000 r-xp 0000b000 00:00 0 /usr/bin/foo\n01a00000-01a21000 rw-p 00000000 00:00 0 [heap]\n7ffd12345000-7ffd12366000 rw-p 00000000 00:00 0 [stack]\n")]);
        std::env::set_var("PROCLENS_PROC_DIR", dir.path().to_str().unwrap());
        let maps = read_maps("123").unwrap();
        assert_eq!(maps.len(), 4);
        assert_eq!(maps[0].pathname, "/usr/bin/foo");
        assert_eq!(maps[2].pathname, "[heap]");
        assert_eq!(maps[3].pathname, "[stack]");
        std::env::remove_var("PROCLENS_PROC_DIR");
    }
}
