// Pure helpers ported from proclens_module.h

pub fn compute_usage_permyriad(total_ns: u64, delta_ns: u64) -> u64 {
    if delta_ns == 0 {
        return 0;
    }
    (10000u64 * total_ns) / delta_ns
}

pub fn compute_bss_range(end_data: u64, start_brk: u64) -> Option<(u64, u64)> {
    if start_brk < end_data {
        return None;
    }
    Some((end_data, start_brk))
}

pub fn compute_heap_range(start_brk: u64, brk: u64) -> Option<(u64, u64)> {
    if brk < start_brk {
        return None;
    }
    Some((start_brk, brk))
}

pub fn is_address_in_range(addr: u64, range_start: u64, range_end: u64) -> bool {
    if range_start > range_end {
        return false;
    }
    addr >= range_start && addr < range_end
}

pub fn get_thread_state_char(state: u64) -> char {
    match state {
        0x0000 => 'R',
        0x0001 => 'S',
        0x0002 => 'D',
        0x0004 => 'T',
        0x0008 => 't',
        0x0020 => 'Z',
        0x0040 => 'X',
        _ => '?',
    }
}

pub fn thread_state_char_from_str(s: &str) -> char {
    match s {
        "R" => 'R',
        "S" => 'S',
        "D" => 'D',
        "T" => 'T',
        "t" => 't',
        "Z" => 'Z',
        "X" => 'X',
        "I" => 'S',
        _ => '?',
    }
}

pub fn build_cpu_affinity_string(cpu_mask: &[bool]) -> String {
    let cpus: Vec<String> =
        cpu_mask.iter().enumerate().filter(|(_, &v)| v).map(|(i, _)| i.to_string()).collect();
    if cpus.is_empty() {
        "none".to_string()
    } else {
        cpus.join(",")
    }
}

pub fn format_size_with_unit(size: u64) -> String {
    if size >= 1024 * 1024 * 1024 {
        format!("{} GB", size / (1024 * 1024 * 1024))
    } else if size >= 1024 * 1024 {
        format!("{} MB", size / (1024 * 1024))
    } else if size >= 1024 {
        format!("{} KB", size / 1024)
    } else {
        format!("{} B", size)
    }
}

pub fn calculate_bar_width(region_size: u64, total_size: u64, bar_width: usize) -> usize {
    if total_size == 0 {
        return 0;
    }
    let w = (region_size as usize * bar_width) / total_size as usize;
    if region_size > 0 && w == 0 {
        1
    } else {
        w
    }
}

pub struct MemoryRegion {
    pub name: &'static str,
    pub size: u64,
    pub exists: bool,
}

pub fn generate_region_visualization(
    region: &MemoryRegion,
    width: usize,
    bar_width: usize,
) -> Option<String> {
    if !region.exists || region.size == 0 {
        return None;
    }
    let size_str = format_size_with_unit(region.size);
    let mut out = format!("{:<5} ({})\n", region.name, size_str);
    out.push_str("      [");
    out.push_str(&"=".repeat(width));
    out.push_str(&" ".repeat(bar_width - width));
    out.push_str("]\n\n");
    Some(out)
}

pub fn calculate_rss_pages(anon: u64, file: u64, shmem: u64) -> u64 {
    anon + file + shmem
}
pub fn pages_to_kb(pages: u64) -> u64 {
    pages * 4
}
pub fn calculate_total_faults(major: u64, minor: u64) -> u64 {
    major + minor
}
pub fn is_valid_oom_score_adj(v: i32) -> bool {
    v >= -1000 && v <= 1000
}
pub fn calculate_memory_usage_percent(used: u64, total: u64) -> u64 {
    if total == 0 {
        0
    } else {
        (used * 100) / total
    }
}
pub fn format_page_fault_stats(major: u64, minor: u64) -> String {
    format!("Major: {}, Minor: {}, Total: {}", major, minor, major + minor)
}
pub fn is_high_memory_pressure(rss_kb: u64, swap_kb: u64) -> bool {
    if rss_kb == 0 {
        swap_kb > 0
    } else {
        swap_kb * 10 > rss_kb
    }
}
pub fn calculate_avg_bytes_per_syscall(total: u64, count: u64) -> u64 {
    if count == 0 {
        0
    } else {
        total / count
    }
}
pub fn calculate_io_intensity(read: u64, write: u64) -> u64 {
    read + write
}

pub fn socket_family_to_string(family: u16) -> &'static str {
    match family {
        1 => "AF_UNIX",
        2 => "AF_INET",
        10 => "AF_INET6",
        16 => "AF_NETLINK",
        _ => "UNKNOWN",
    }
}
pub fn socket_type_to_string(ty: u16) -> &'static str {
    match ty {
        1 => "STREAM",
        2 => "DGRAM",
        3 => "RAW",
        _ => "UNKNOWN",
    }
}
pub fn socket_protocol_to_string(proto: u16) -> &'static str {
    match proto {
        6 => "TCP",
        17 => "UDP",
        _ => "OTHER",
    }
}
pub fn socket_state_to_string(state: u8) -> &'static str {
    match state {
        1 => "ESTABLISHED",
        2 => "SYN_SENT",
        3 => "SYN_RECV",
        4 => "FIN_WAIT1",
        5 => "FIN_WAIT2",
        6 => "TIME_WAIT",
        7 => "CLOSE",
        8 => "CLOSE_WAIT",
        9 => "LAST_ACK",
        10 => "LISTEN",
        11 => "CLOSING",
        12 => "NEW_SYN_RECV",
        _ => "UNKNOWN",
    }
}

pub fn format_tcp_traffic_line(rx_pkts: u64, rx_bytes: u64, tx_pkts: u64, tx_bytes: u64) -> String {
    format!(
        "          Traffic: RX pkts={} bytes={}  TX pkts={} bytes={}\n",
        rx_pkts, rx_bytes, tx_pkts, tx_bytes
    )
}
pub fn format_udp_traffic_line(rx_pkts: u64, rx_bytes: u64, tx_pkts: u64, tx_bytes: u64) -> String {
    format!(
        "          Traffic: RX pkts={} bytes={}  TX pkts={} bytes={} (queued)\n",
        rx_pkts, rx_bytes, tx_pkts, tx_bytes
    )
}

#[derive(Debug, Clone)]
pub struct TopTalkerEntry {
    pub fd: u32,
    pub family: u16,
    pub protocol: u16,
    pub rx_bytes: u64,
    pub tx_bytes: u64,
    pub total_bytes: u64,
}

pub fn try_insert_top_talker(
    list: &mut Vec<TopTalkerEntry>,
    max: usize,
    fd: u32,
    family: u16,
    protocol: u16,
    rx: u64,
    tx: u64,
) {
    let total = rx + tx;
    if total == 0 {
        return;
    }
    let entry =
        TopTalkerEntry { fd, family, protocol, rx_bytes: rx, tx_bytes: tx, total_bytes: total };
    let pos = list.iter().position(|e| total > e.total_bytes).unwrap_or(list.len());
    if list.len() < max {
        list.insert(pos, entry);
    } else if pos < max {
        list.insert(pos, entry);
        list.truncate(max);
    }
}

#[derive(Debug, Clone)]
pub struct NetdevCount {
    pub ifindex: i32,
    pub count: usize,
    pub name: String,
}

pub fn add_netdev_count(list: &mut Vec<NetdevCount>, max: usize, ifindex: i32, name: &str) {
    if let Some(e) = list.iter_mut().find(|e| e.ifindex == ifindex) {
        e.count += 1;
        return;
    }
    if list.len() >= max {
        return;
    }
    list.push(NetdevCount { ifindex, count: 1, name: name.to_string() });
}

pub fn format_compact_size(value: u64, suffix: Option<&str>) -> String {
    let units = ["B", "KB", "MB", "GB", "TB"];
    let mut scaled = value;
    let mut idx = 0;
    while scaled >= 1024 && idx < units.len() - 1 {
        scaled /= 1024;
        idx += 1;
    }
    match suffix {
        Some(s) => format!("{} {}{}", scaled, units[idx], s),
        None => format!("{} {}", scaled, units[idx]),
    }
}

pub fn render_sparkline(values: &[u64]) -> String {
    if values.is_empty() {
        return String::new();
    }
    let levels = ["▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"];
    let min = *values.iter().min().unwrap();
    let max = *values.iter().max().unwrap();
    let range = max - min;
    let mut out = String::new();
    for &v in values {
        let glyph = if range == 0 {
            if max == 0 {
                levels[0]
            } else {
                levels[levels.len() - 1]
            }
        } else {
            let mut idx = ((v - min) * (levels.len() as u64 - 1) / range) as usize;
            if v > 0 && idx < levels.len() - 1 {
                idx += 1;
            }
            levels[idx]
        };
        out.push_str(glyph);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn test_compute_usage_permyriad() {
        assert_eq!(compute_usage_permyriad(0, 1000000), 0);
        assert_eq!(compute_usage_permyriad(500000, 1000000), 5000);
        assert_eq!(compute_usage_permyriad(0, 0), 0);
    }
    #[test]
    fn test_bss_range() {
        assert_eq!(compute_bss_range(1000, 2000), Some((1000, 2000)));
        assert_eq!(compute_bss_range(3000, 2000), None);
    }
    #[test]
    fn test_heap_range() {
        assert_eq!(compute_heap_range(5000, 8000), Some((5000, 8000)));
        assert_eq!(compute_heap_range(9000, 7000), None);
        assert_eq!(compute_heap_range(10000, 10000), Some((10000, 10000)));
    }
    #[test]
    fn test_is_address_in_range() {
        assert!(is_address_in_range(5000, 1000, 10000));
        assert!(is_address_in_range(1000, 1000, 10000));
        assert!(!is_address_in_range(10000, 1000, 10000));
        assert!(!is_address_in_range(500, 1000, 10000));
        assert!(!is_address_in_range(5000, 10000, 1000));
    }
    #[test]
    fn test_thread_state() {
        assert_eq!(get_thread_state_char(0x0000), 'R');
        assert_eq!(get_thread_state_char(0x0001), 'S');
        assert_eq!(get_thread_state_char(0xFFFF), '?');
    }
    #[test]
    fn test_format_size() {
        assert_eq!(format_size_with_unit(512), "512 B");
        assert_eq!(format_size_with_unit(1024), "1 KB");
        assert_eq!(format_size_with_unit(1024 * 1024), "1 MB");
        assert_eq!(format_size_with_unit(1024 * 1024 * 1024), "1 GB");
        assert_eq!(format_size_with_unit(0), "0 B");
    }
    #[test]
    fn test_bar_width() {
        assert_eq!(calculate_bar_width(100, 1000, 50), 5);
        assert_eq!(calculate_bar_width(500, 1000, 50), 25);
        assert_eq!(calculate_bar_width(1, 1000000, 50), 1);
        assert_eq!(calculate_bar_width(0, 1000, 50), 0);
        assert_eq!(calculate_bar_width(100, 0, 50), 0);
    }
    #[test]
    fn test_socket_strings() {
        assert_eq!(socket_family_to_string(2), "AF_INET");
        assert_eq!(socket_family_to_string(99), "UNKNOWN");
        assert_eq!(socket_type_to_string(1), "STREAM");
        assert_eq!(socket_protocol_to_string(6), "TCP");
        assert_eq!(socket_protocol_to_string(99), "OTHER");
        assert_eq!(socket_state_to_string(1), "ESTABLISHED");
        assert_eq!(socket_state_to_string(99), "UNKNOWN");
    }
    #[test]
    fn test_top_talker() {
        let mut list = Vec::new();
        try_insert_top_talker(&mut list, 3, 10, 2, 6, 500, 500);
        assert_eq!(list.len(), 1);
        assert_eq!(list[0].total_bytes, 1000);
        try_insert_top_talker(&mut list, 3, 20, 2, 6, 100, 100);
        try_insert_top_talker(&mut list, 3, 30, 1, 17, 700, 500);
        assert_eq!(list[0].fd, 30);
        assert_eq!(list[0].total_bytes, 1200);
        try_insert_top_talker(&mut list, 3, 50, 2, 6, 0, 0);
        assert_eq!(list.len(), 3);
    }
    #[test]
    fn test_sparkline() {
        assert_eq!(render_sparkline(&[]), "");
        assert_eq!(render_sparkline(&[0, 0, 0]), "▁▁▁");
        assert_eq!(render_sparkline(&[5, 5, 5]), "███");
    }
    #[test]
    fn test_compact_size() {
        assert_eq!(format_compact_size(512, None), "512 B");
        assert_eq!(format_compact_size(2048, None), "2 KB");
        assert_eq!(format_compact_size(1024, Some("/s")), "1 KB/s");
    }
}
