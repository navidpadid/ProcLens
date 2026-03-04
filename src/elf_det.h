/* SPDX-License-Identifier: MIT */
#pragma once

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/if.h>
#include <linux/string.h>
typedef u64 eh_u64;
#else
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif
typedef unsigned long long u64;
typedef u64 eh_u64;
#endif

/* Copy PID input from proc write into destination buffer safely.
 * Clears destination to avoid stale bytes from previous writes.
 * Returns bytes copied from src.
 */
static inline size_t update_pid_write_buffer(char *dst,
					     size_t dst_size,
					     const char *src,
					     size_t src_len)
{
	size_t copy_len;

	if (!dst || !src || dst_size == 0)
		return 0;

	copy_len = (src_len < (dst_size - 1)) ? src_len : (dst_size - 1);
	memset(dst, 0, dst_size);
	memcpy(dst, src, copy_len);
	dst[copy_len] = '\0';

	return copy_len;
}

/* Toggle procfile read state.
 * Returns 1 when read should return EOF, 0 when data should be emitted.
 */
static inline int procfile_read_should_finish(int *finished)
{
	if (!finished)
		return 1;

	if (*finished) {
		*finished = 0;
		return 1;
	}

	*finished = 1;
	return 0;
}

/* Format procfile read output into caller-provided buffer. */
static inline int
format_procfile_output(const char *src, char *out, int out_size)
{
	if (!src || !out || out_size <= 0)
		return 0;

	return snprintf(out, out_size, "buff variable : %s\n", src);
}

/* Compute CPU usage permyriad (percent * 100) from total_ns and delta_ns */
static inline eh_u64 compute_usage_permyriad(eh_u64 total_ns, eh_u64 delta_ns)
{
	if (delta_ns == 0)
		return 0;
	return (10000ULL * total_ns) / delta_ns;
}

/* Compute BSS range from end_data and start_brk; returns 0 on invalid
 * BSS (Block Started by Symbol): Uninitialized data segment
 * Note: Modern ELF binaries may have zero-length BSS if end_data == start_brk
 * This is normal and not an error.
 */
static inline int compute_bss_range(unsigned long end_data,
				    unsigned long start_brk,
				    unsigned long *out_start,
				    unsigned long *out_end)
{
	if (start_brk < end_data) {
		*out_start = 0;
		*out_end = 0;
		return 0;
	}
	*out_start = end_data;
	*out_end = start_brk;
	return 1;
}

/* Compute heap range from start_brk and brk; returns 0 on invalid
 * Heap: Dynamic memory allocation region
 * LIMITATION: This only tracks brk-based heap (traditional heap).
 * Modern allocators (glibc malloc) also use mmap for large allocations
 * and arena-based heaps, which are NOT included in this range.
 * To see full heap usage, you would need to parse /proc/pid/maps for
 * anonymous mappings marked as [heap] or unnamed mmap regions.
 */
static inline int compute_heap_range(unsigned long start_brk,
				     unsigned long brk,
				     unsigned long *out_start,
				     unsigned long *out_end)
{
	if (brk < start_brk) {
		*out_start = 0;
		*out_end = 0;
		return 0;
	}
	*out_start = start_brk;
	*out_end = brk;
	return 1;
}

/* Check if an address falls within a memory range (inclusive)
 * Used for finding VMAs that contain specific addresses like stack
 * Returns 1 if addr is within [range_start, range_end), 0 otherwise
 */
static inline int is_address_in_range(unsigned long addr,
				      unsigned long range_start,
				      unsigned long range_end)
{
	if (range_start > range_end)
		return 0;
	if (addr >= range_start && addr < range_end)
		return 1;
	return 0;
}

/* Convert thread/task state value to single character representation
 * Kernel state constants (only available in kernel context):
 * - TASK_RUNNING (0x0000) -> 'R'
 * - TASK_INTERRUPTIBLE (0x0001) -> 'S'
 * - TASK_UNINTERRUPTIBLE (0x0002) -> 'D'
 * - __TASK_STOPPED (0x0004) -> 'T'
 * - __TASK_TRACED (0x0008) -> 't'
 * - EXIT_ZOMBIE (0x0020) -> 'Z'
 * - EXIT_DEAD (0x0040) -> 'X'
 * For testing: pass raw state values (0, 1, 2, 4, 8, 32, 64)
 */
static inline char get_thread_state_char(unsigned long state)
{
	switch (state) {
	case 0x0000: /* TASK_RUNNING */
		return 'R';
	case 0x0001: /* TASK_INTERRUPTIBLE */
		return 'S';
	case 0x0002: /* TASK_UNINTERRUPTIBLE */
		return 'D';
	case 0x0004: /* __TASK_STOPPED */
		return 'T';
	case 0x0008: /* __TASK_TRACED */
		return 't';
	case 0x0020: /* EXIT_ZOMBIE */
		return 'Z';
	case 0x0040: /* EXIT_DEAD */
		return 'X';
	default:
		return '?';
	}
}

/* Build CPU affinity mask string from bitmap
 * Checks up to max_cpus and appends CPU numbers (comma-separated)
 * Returns number of characters written (excluding null terminator)
 * If no CPUs are set, writes "none"
 *
 * Parameters:
 *   cpu_mask: array representing CPU bitmap (1 = CPU available, 0 = not)
 *   max_cpus: maximum number of CPUs to check
 *   out_buf: output buffer for the affinity string
 *   buf_size: size of output buffer
 */
static inline int build_cpu_affinity_string(const int *cpu_mask,
					    int max_cpus,
					    char *out_buf,
					    int buf_size)
{
	int i, len = 0;
	int has_cpu = 0;

	if (!out_buf || buf_size < 5)
		return 0;

	for (i = 0; i < max_cpus && len < buf_size - 2; i++) {
		if (cpu_mask[i]) {
			has_cpu = 1;
			len += snprintf(out_buf + len, buf_size - len, "%d,",
					i);
		}
	}

	if (has_cpu && len > 0) {
		out_buf[len - 1] = '\0'; /* Remove trailing comma */
		return len - 1;
	}

	snprintf(out_buf, buf_size, "none");
	return 4;
}

/* Memory region structure for visualization */
struct memory_region {
	const char *name;
	unsigned long size;
	int exists; /* 1 if region should be displayed, 0 otherwise */
};

#define ELF_DET_NETDEV_MAX	8
#define ELF_DET_NETDEV_NAME_MAX IFNAMSIZ

struct netdev_count {
	int ifindex;
	int count;
	char name[ELF_DET_NETDEV_NAME_MAX];
};

/* Format size with appropriate unit (B, KB, MB)
 * Returns number of characters written (excluding null terminator)
 */
static inline int
format_size_with_unit(unsigned long size, char *out_buf, int buf_size)
{
	if (!out_buf || buf_size < 10)
		return 0;

	if (size >= 1024 * 1024)
		return snprintf(out_buf, buf_size, "%lu MB",
				size / (1024 * 1024));
	else if (size >= 1024)
		return snprintf(out_buf, buf_size, "%lu KB", size / 1024);
	else
		return snprintf(out_buf, buf_size, "%lu B", size);
}

/* Calculate proportional bar width for visualization
 * Ensures at least 1 character width for non-zero sizes
 * Returns the proportional width
 */
static inline int calculate_bar_width(unsigned long region_size,
				      unsigned long total_size,
				      int bar_width)
{
	int width;

	if (total_size == 0)
		return 0;

	width = (region_size * bar_width) / total_size;

	/* Ensure at least 1 char for non-zero sizes */
	if (region_size > 0 && width == 0)
		width = 1;

	return width;
}

/* Generate memory visualization for a single region
 * Writes the region name, size, and proportional bar to output buffer
 * Returns number of characters written
 *
 * Parameters:
 *   region: memory region information
 *   width: calculated proportional width for this region
 *   bar_width: total bar width for visualization
 *   out_buf: output buffer
 *   buf_size: size of output buffer
 */
static inline int
generate_region_visualization(const struct memory_region *region,
			      int width,
			      int bar_width,
			      char *out_buf,
			      int buf_size)
{
	char size_str[32];
	int len = 0;
	int i;

	if (!region || !out_buf || buf_size < 100)
		return 0;

	if (!region->exists || region->size == 0)
		return 0;

	/* Format size */
	format_size_with_unit(region->size, size_str, sizeof(size_str));

	/* Write region header */
	len += snprintf(out_buf + len, buf_size - len, "%-5s (%s)\n",
			region->name, size_str);

	/* Write bar */
	len += snprintf(out_buf + len, buf_size - len, "      [");

	for (i = 0; i < width && len < buf_size - 2; i++)
		len += snprintf(out_buf + len, buf_size - len, "=");

	for (i = width; i < bar_width && len < buf_size - 2; i++)
		len += snprintf(out_buf + len, buf_size - len, " ");

	len += snprintf(out_buf + len, buf_size - len, "]\n\n");

	return len;
}

/* Track interface usage count by ifindex
 * Adds or increments entry; respects max_entries.
 */
static inline void add_netdev_count(struct netdev_count *list,
				    int *list_len,
				    int max_entries,
				    int ifindex,
				    const char *name)
{
	int i;

	if (!list || !list_len || !name || max_entries <= 0)
		return;

	for (i = 0; i < *list_len; i++) {
		if (list[i].ifindex == ifindex) {
			list[i].count++;
			return;
		}
	}

	if (*list_len >= max_entries)
		return;

	list[*list_len].ifindex = ifindex;
	list[*list_len].count = 1;
	snprintf(list[*list_len].name, sizeof(list[*list_len].name), "%s",
		 name);
	(*list_len)++;
}

/* Memory Pressure Statistics Helper Functions */

/* Calculate RSS (Resident Set Size) from component pages
 * RSS = Anonymous pages + File-backed pages + Shared memory pages
 * Returns total RSS in pages
 */
static inline unsigned long calculate_rss_pages(unsigned long anon_pages,
						unsigned long file_pages,
						unsigned long shmem_pages)
{
	return anon_pages + file_pages + shmem_pages;
}

/* Convert pages to kilobytes
 * Uses PAGE_SHIFT (typically 12 for 4KB pages)
 * Formula: pages << (PAGE_SHIFT - 10) = pages * (4096 / 1024)
 * For testing without PAGE_SHIFT, use pages * 4
 */
static inline unsigned long pages_to_kb(unsigned long pages)
{
#ifdef __KERNEL__
	return pages << (PAGE_SHIFT - 10);
#else
	/* Assume 4KB pages for user-space testing */
	return pages * 4;
#endif
}

/* Calculate total page faults
 * Returns sum of major and minor page faults
 */
static inline unsigned long calculate_total_faults(unsigned long major_faults,
						   unsigned long minor_faults)
{
	return major_faults + minor_faults;
}

/* Validate OOM score adjustment value
 * Valid range: -1000 to 1000
 * Returns 1 if valid, 0 if invalid
 */
static inline int is_valid_oom_score_adj(long oom_score_adj)
{
	return (oom_score_adj >= -1000 && oom_score_adj <= 1000);
}

/* Calculate memory usage percentage
 * Returns (used_kb * 100) / total_kb
 * Returns 0 if total_kb is 0 to avoid division by zero
 */
static inline unsigned long
calculate_memory_usage_percent(unsigned long used_kb, unsigned long total_kb)
{
	if (total_kb == 0)
		return 0;
	return (used_kb * 100) / total_kb;
}

/* Format page fault statistics string
 * Returns number of characters written
 */
static inline int format_page_fault_stats(unsigned long major_faults,
					  unsigned long minor_faults,
					  char *out_buf,
					  int buf_size)
{
	if (!out_buf || buf_size < 50)
		return 0;

	return snprintf(out_buf, buf_size, "Major: %lu, Minor: %lu, Total: %lu",
			major_faults, minor_faults,
			major_faults + minor_faults);
}

/* Check if memory pressure is high based on swap usage
 * Returns 1 if swap usage indicates high memory pressure, 0 otherwise
 * Threshold: swap > 10% of RSS indicates pressure
 */
static inline int is_high_memory_pressure(unsigned long rss_kb,
					  unsigned long swap_kb)
{
	if (rss_kb == 0)
		return (swap_kb > 0);
	return (swap_kb * 10 > rss_kb); /* swap > 10% of RSS */
}

/* Convert socket family value to string representation
 * Common values: AF_INET (2), AF_INET6 (10), AF_UNIX (1), AF_NETLINK (16)
 * Returns string representation or "UNKNOWN" for unrecognized families
 * For testing: use numeric values (1, 2, 10, 16)
 */
static inline const char *socket_family_to_string(unsigned short family)
{
	switch (family) {
	case 1: /* AF_UNIX */
		return "AF_UNIX";
	case 2: /* AF_INET */
		return "AF_INET";
	case 10: /* AF_INET6 */
		return "AF_INET6";
	case 16: /* AF_NETLINK */
		return "AF_NETLINK";
	default:
		return "UNKNOWN";
	}
}

/* Convert socket type value to string representation
 * Common values: SOCK_STREAM (1), SOCK_DGRAM (2), SOCK_RAW (3)
 * Returns string representation or "UNKNOWN" for unrecognized types
 * For testing: use numeric values (1, 2, 3)
 */
static inline const char *socket_type_to_string(unsigned short type)
{
	switch (type) {
	case 1: /* SOCK_STREAM */
		return "STREAM";
	case 2: /* SOCK_DGRAM */
		return "DGRAM";
	case 3: /* SOCK_RAW */
		return "RAW";
	default:
		return "UNKNOWN";
	}
}

/* Convert socket protocol value to string representation
 * Common values: TCP (6), UDP (17)
 * Returns "OTHER" for unrecognized protocols
 * For testing: use numeric values (6, 17)
 */
static inline const char *socket_protocol_to_string(unsigned short protocol)
{
	switch (protocol) {
	case 6: /* IPPROTO_TCP */
		return "TCP";
	case 17: /* IPPROTO_UDP */
		return "UDP";
	default:
		return "OTHER";
	}
}

/* Format TCP per-socket traffic statistics line
 * Returns number of characters written
 */
static inline int format_tcp_traffic_line(eh_u64 rx_packets,
					  eh_u64 rx_bytes,
					  eh_u64 tx_packets,
					  eh_u64 tx_bytes,
					  char *out_buf,
					  int buf_size)
{
	if (!out_buf || buf_size < 80)
		return 0;

	return snprintf(out_buf, buf_size,
			"          Traffic: RX pkts=%llu bytes=%llu  TX "
			"pkts=%llu bytes=%llu\n",
			rx_packets, rx_bytes, tx_packets, tx_bytes);
}

/* Format UDP per-socket traffic statistics line (queue snapshot)
 * Returns number of characters written
 */
static inline int format_udp_traffic_line(eh_u64 rx_packets,
					  eh_u64 rx_bytes,
					  eh_u64 tx_packets,
					  eh_u64 tx_bytes,
					  char *out_buf,
					  int buf_size)
{
	if (!out_buf || buf_size < 90)
		return 0;

	return snprintf(out_buf, buf_size,
			"          Traffic: RX pkts=%llu bytes=%llu  TX "
			"pkts=%llu bytes=%llu (queued)\n",
			rx_packets, rx_bytes, tx_packets, tx_bytes);
}

/* Convert TCP socket state value to string representation
 * TCP state values: 1-12 representing connection states
 * Returns string representation or "UNKNOWN" for invalid states
 * For testing: use numeric values (1-12)
 */
static inline const char *socket_state_to_string(unsigned char state)
{
	switch (state) {
	case 1: /* TCP_ESTABLISHED */
		return "ESTABLISHED";
	case 2: /* TCP_SYN_SENT */
		return "SYN_SENT";
	case 3: /* TCP_SYN_RECV */
		return "SYN_RECV";
	case 4: /* TCP_FIN_WAIT1 */
		return "FIN_WAIT1";
	case 5: /* TCP_FIN_WAIT2 */
		return "FIN_WAIT2";
	case 6: /* TCP_TIME_WAIT */
		return "TIME_WAIT";
	case 7: /* TCP_CLOSE */
		return "CLOSE";
	case 8: /* TCP_CLOSE_WAIT */
		return "CLOSE_WAIT";
	case 9: /* TCP_LAST_ACK */
		return "LAST_ACK";
	case 10: /* TCP_LISTEN */
		return "LISTEN";
	case 11: /* TCP_CLOSING */
		return "CLOSING";
	case 12: /* TCP_NEW_SYN_RECV */
		return "NEW_SYN_RECV";
	default:
		return "UNKNOWN";
	}
}
