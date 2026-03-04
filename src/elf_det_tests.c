// SPDX-License-Identifier: MIT
#include "elf_det.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
	/* compute_usage_permyriad tests */
	assert(compute_usage_permyriad(0, 1000000ULL) == 0);
	assert(compute_usage_permyriad(500000ULL, 1000000ULL) == 5000ULL);
	assert(compute_usage_permyriad(250000ULL, 1000000ULL) == 2500ULL);
	assert(compute_usage_permyriad(1000000ULL, 1000000ULL) == 10000ULL);
	assert(compute_usage_permyriad(0, 0) == 0);

	/* compute_bss_range tests */
	unsigned long s = 0, e = 0;
	int ret1, ret2, ret3, ret4;

	ret1 = compute_bss_range(1000UL, 2000UL, &s, &e);
	assert(ret1 == 1);
	assert(s == 1000UL && e == 2000UL);

	ret2 = compute_bss_range(3000UL, 2000UL, &s, &e);
	assert(ret2 == 0);
	assert(s == 0UL && e == 0UL);

	/* compute_heap_range tests */
	ret3 = compute_heap_range(5000UL, 8000UL, &s, &e);
	assert(ret3 == 1);
	assert(s == 5000UL && e == 8000UL);

	ret4 = compute_heap_range(9000UL, 7000UL, &s, &e);
	assert(ret4 == 0);
	assert(s == 0UL && e == 0UL);

	/* Test heap with same start and end (empty heap) */
	ret1 = compute_heap_range(10000UL, 10000UL, &s, &e);
	assert(ret1 == 1);
	assert(s == 10000UL && e == 10000UL);

	/* is_address_in_range tests */
	/* Test address within range */
	assert(is_address_in_range(5000UL, 1000UL, 10000UL) == 1);
	assert(is_address_in_range(1000UL, 1000UL, 10000UL) == 1);

	/* Test address at boundary (range_end is exclusive) */
	assert(is_address_in_range(10000UL, 1000UL, 10000UL) == 0);

	/* Test address outside range */
	assert(is_address_in_range(500UL, 1000UL, 10000UL) == 0);
	assert(is_address_in_range(15000UL, 1000UL, 10000UL) == 0);

	/* Test invalid range (start > end) */
	assert(is_address_in_range(5000UL, 10000UL, 1000UL) == 0);

	/* Test edge cases */
	assert(is_address_in_range(0UL, 0UL, 1UL) == 1);
	assert(is_address_in_range(ULONG_MAX - 1, 0UL, ULONG_MAX) == 1);

	/* get_thread_state_char tests */
	assert(get_thread_state_char(0x0000) == 'R'); /* TASK_RUNNING */
	assert(get_thread_state_char(0x0001) == 'S'); /* TASK_INTERRUPTIBLE */
	assert(get_thread_state_char(0x0002) == 'D'); /* TASK_UNINTERRUPTIBLE */
	assert(get_thread_state_char(0x0004) == 'T'); /* __TASK_STOPPED */
	assert(get_thread_state_char(0x0008) == 't'); /* __TASK_TRACED */
	assert(get_thread_state_char(0x0020) == 'Z'); /* EXIT_ZOMBIE */
	assert(get_thread_state_char(0x0040) == 'X'); /* EXIT_DEAD */
	assert(get_thread_state_char(0x9999) == '?'); /* Unknown state */
	assert(get_thread_state_char(0xFFFF) == '?'); /* Unknown state */

	/* build_cpu_affinity_string tests */
	char buf[64];
	int len;
	const int mask1[8] = {1, 0, 1, 0, 1, 0, 1, 0}; /* CPUs 0,2,4,6 */
	const int mask2[8] = {1, 1, 1, 1, 1, 1, 1, 1}; /* All CPUs */
	const int mask3[8] = {0, 0, 0, 0, 0, 0, 0, 0}; /* No CPUs */
	const int mask4[8] = {0, 0, 0, 0, 0, 0, 0, 1}; /* Only CPU 7 */
	const int mask5[8] = {1, 0, 0, 0, 0, 0, 0, 0}; /* Only CPU 0 */

	/* Test normal mask */
	len = build_cpu_affinity_string(mask1, 8, buf, sizeof(buf));
	assert(len == 7); /* "0,2,4,6" */
	assert(strcmp(buf, "0,2,4,6") == 0);

	/* Test all CPUs */
	len = build_cpu_affinity_string(mask2, 8, buf, sizeof(buf));
	assert(len == 15); /* "0,1,2,3,4,5,6,7" */
	assert(strcmp(buf, "0,1,2,3,4,5,6,7") == 0);

	/* Test no CPUs */
	len = build_cpu_affinity_string(mask3, 8, buf, sizeof(buf));
	assert(len == 4); /* "none" */
	assert(strcmp(buf, "none") == 0);

	/* Test single CPU at end */
	len = build_cpu_affinity_string(mask4, 8, buf, sizeof(buf));
	assert(len == 1); /* "7" */
	assert(strcmp(buf, "7") == 0);

	/* format_size_with_unit tests */
	char size_buf[32];

	/* Test bytes */
	len = format_size_with_unit(512, size_buf, sizeof(size_buf));
	assert(strcmp(size_buf, "512 B") == 0);
	assert(len > 0);

	/* Test kilobytes */
	len = format_size_with_unit(1024, size_buf, sizeof(size_buf));
	assert(strcmp(size_buf, "1 KB") == 0);
	assert(len > 0);

	len = format_size_with_unit(2048, size_buf, sizeof(size_buf));
	assert(strcmp(size_buf, "2 KB") == 0);

	/* Test megabytes */
	len = format_size_with_unit(1024 * 1024, size_buf, sizeof(size_buf));
	assert(strcmp(size_buf, "1 MB") == 0);
	assert(len > 0);

	len = format_size_with_unit(5 * 1024 * 1024, size_buf,
				    sizeof(size_buf));
	assert(strcmp(size_buf, "5 MB") == 0);

	/* Test edge cases */
	len = format_size_with_unit(0, size_buf, sizeof(size_buf));
	assert(strcmp(size_buf, "0 B") == 0);

	len = format_size_with_unit(1023, size_buf, sizeof(size_buf));
	assert(strcmp(size_buf, "1023 B") == 0);

	/* calculate_bar_width tests */
	int width;

	/* Test normal proportions */
	width = calculate_bar_width(100, 1000, 50);
	assert(width == 5); /* 100/1000 * 50 = 5 */

	width = calculate_bar_width(500, 1000, 50);
	assert(width == 25); /* Half */

	width = calculate_bar_width(1000, 1000, 50);
	assert(width == 50); /* Full */

	/* Test minimum width for non-zero sizes */
	width = calculate_bar_width(1, 1000000, 50);
	assert(width == 1); /* Should be at least 1 for non-zero */

	/* Test zero size */
	width = calculate_bar_width(0, 1000, 50);
	assert(width == 0);

	/* Test zero total (edge case) */
	width = calculate_bar_width(100, 0, 50);
	assert(width == 0);

	/* generate_region_visualization tests */
	struct memory_region region;
	char viz_buf[256];

	/* Test normal region */
	region.name = "CODE";
	region.size = 1024 * 1024; /* 1 MB */
	region.exists = 1;
	len = generate_region_visualization(&region, 25, 50, viz_buf,
					    sizeof(viz_buf));
	assert(len > 0);
	assert(strstr(viz_buf, "CODE"));
	assert(strstr(viz_buf, "1 MB"));
	assert(strstr(viz_buf, "[========================="));

	/* Test small region */
	region.name = "DATA";
	region.size = 512; /* 512 B */
	region.exists = 1;
	len = generate_region_visualization(&region, 5, 50, viz_buf,
					    sizeof(viz_buf));
	assert(len > 0);
	assert(strstr(viz_buf, "DATA"));
	assert(strstr(viz_buf, "512 B"));

	/* Test non-existent region */
	region.name = "BSS";
	region.size = 0;
	region.exists = 0;
	len = generate_region_visualization(&region, 0, 50, viz_buf,
					    sizeof(viz_buf));
	assert(len == 0); /* Should return 0 for non-existent regions */

	/* Test zero size but exists flag set */
	region.name = "HEAP";
	region.size = 0;
	region.exists = 1;
	len = generate_region_visualization(&region, 0, 50, viz_buf,
					    sizeof(viz_buf));
	assert(len == 0); /* Should return 0 for zero size */

	/* Test single CPU at start */
	len = build_cpu_affinity_string(mask5, 8, buf, sizeof(buf));
	assert(len == 1); /* "0" */
	assert(strcmp(buf, "0") == 0);

	/* Test with smaller max_cpus */
	len = build_cpu_affinity_string(mask1, 4, buf, sizeof(buf));
	assert(len == 3); /* "0,2" */
	assert(strcmp(buf, "0,2") == 0);

	/* Test with small buffer */
	char small_buf[6];

	len = build_cpu_affinity_string(mask1, 8, small_buf, sizeof(small_buf));
	assert(len >= 0); /* Should handle gracefully */
	assert(strlen(small_buf) < sizeof(small_buf));

	/* Test NULL buffer */
	len = build_cpu_affinity_string(mask1, 8, NULL, 64);
	assert(len == 0);

	/* Test too small buffer */
	len = build_cpu_affinity_string(mask1, 8, buf, 3);
	assert(len == 0);

	/* Memory Pressure Statistics Tests */

	/* calculate_rss_pages tests */
	unsigned long rss;

	/* Basic RSS calculation */
	rss = calculate_rss_pages(1000, 2000, 500);
	assert(rss == 3500);

	/* Zero values */
	rss = calculate_rss_pages(0, 0, 0);
	assert(rss == 0);

	/* Only anonymous pages */
	rss = calculate_rss_pages(5000, 0, 0);
	assert(rss == 5000);

	/* Only file-backed pages */
	rss = calculate_rss_pages(0, 8000, 0);
	assert(rss == 8000);

	/* Only shared memory */
	rss = calculate_rss_pages(0, 0, 3000);
	assert(rss == 3000);

	/* Large values */
	rss = calculate_rss_pages(1000000, 2000000, 500000);
	assert(rss == 3500000);

	/* pages_to_kb tests */
	unsigned long kb;

	/* Standard page conversions (assuming 4KB pages in userspace) */
	kb = pages_to_kb(1);
	assert(kb == 4); /* 1 page = 4 KB */

	kb = pages_to_kb(256);
	assert(kb == 1024); /* 256 pages = 1 MB = 1024 KB */

	kb = pages_to_kb(0);
	assert(kb == 0);

	kb = pages_to_kb(1024);
	assert(kb == 4096); /* 1024 pages = 4 MB */

	/* Large value */
	kb = pages_to_kb(262144); /* 1 GB worth of pages */
	assert(kb == 1048576);

	/* calculate_total_faults tests */
	unsigned long total;

	/* Basic fault calculation */
	total = calculate_total_faults(10, 1000);
	assert(total == 1010);

	/* Zero faults */
	total = calculate_total_faults(0, 0);
	assert(total == 0);

	/* Only major faults */
	total = calculate_total_faults(500, 0);
	assert(total == 500);

	/* Only minor faults */
	total = calculate_total_faults(0, 5000);
	assert(total == 5000);

	/* Large values */
	total = calculate_total_faults(1000000, 5000000);
	assert(total == 6000000);

	/* is_valid_oom_score_adj tests */

	/* Valid scores */
	assert(is_valid_oom_score_adj(0) == 1);
	assert(is_valid_oom_score_adj(-1000) == 1);
	assert(is_valid_oom_score_adj(1000) == 1);
	assert(is_valid_oom_score_adj(-500) == 1);
	assert(is_valid_oom_score_adj(500) == 1);
	assert(is_valid_oom_score_adj(1) == 1);
	assert(is_valid_oom_score_adj(-1) == 1);

	/* Invalid scores */
	assert(is_valid_oom_score_adj(-1001) == 0);
	assert(is_valid_oom_score_adj(1001) == 0);
	assert(is_valid_oom_score_adj(-2000) == 0);
	assert(is_valid_oom_score_adj(2000) == 0);

	/* calculate_memory_usage_percent tests */
	unsigned long percent;

	/* Basic percentage calculations */
	percent = calculate_memory_usage_percent(500, 1000);
	assert(percent == 50); /* 50% */

	percent = calculate_memory_usage_percent(250, 1000);
	assert(percent == 25); /* 25% */

	percent = calculate_memory_usage_percent(1000, 1000);
	assert(percent == 100); /* 100% */

	percent = calculate_memory_usage_percent(0, 1000);
	assert(percent == 0); /* 0% */

	/* Division by zero protection */
	percent = calculate_memory_usage_percent(500, 0);
	assert(percent == 0);

	/* Over 100% (used more than total) */
	percent = calculate_memory_usage_percent(1500, 1000);
	assert(percent == 150);

	/* Large values */
	percent = calculate_memory_usage_percent(1024 * 1024, 2048 * 1024);
	assert(percent == 50);

	/* format_page_fault_stats tests */
	char fault_buf[128];

	/* Basic formatting */
	len = format_page_fault_stats(10, 1000, fault_buf, sizeof(fault_buf));
	assert(len > 0);
	assert(strstr(fault_buf, "Major: 10"));
	assert(strstr(fault_buf, "Minor: 1000"));
	assert(strstr(fault_buf, "Total: 1010"));

	/* Zero faults */
	len = format_page_fault_stats(0, 0, fault_buf, sizeof(fault_buf));
	assert(len > 0);
	assert(strstr(fault_buf, "Major: 0"));
	assert(strstr(fault_buf, "Total: 0"));

	/* Large values */
	len = format_page_fault_stats(500000, 2500000, fault_buf,
				      sizeof(fault_buf));
	assert(len > 0);
	assert(strstr(fault_buf, "Total: 3000000"));

	/* Buffer too small */
	len = format_page_fault_stats(10, 100, fault_buf, 10);
	assert(len >= 0); /* Should handle gracefully */

	/* NULL buffer */
	len = format_page_fault_stats(10, 100, NULL, 128);
	assert(len == 0);

	/* is_high_memory_pressure tests */
	int high_pressure;

	/* No swap usage - low pressure */
	high_pressure = is_high_memory_pressure(10000, 0);
	assert(high_pressure == 0);

	/* Swap < 10% of RSS - low pressure */
	high_pressure = is_high_memory_pressure(10000, 500); /* 5% */
	assert(high_pressure == 0);

	/* Swap = 10% of RSS - borderline */
	high_pressure = is_high_memory_pressure(10000, 1000); /* 10% */
	assert(high_pressure == 0);

	/* Swap > 10% of RSS - high pressure */
	high_pressure = is_high_memory_pressure(10000, 1001); /* 10.01% */
	assert(high_pressure == 1);

	high_pressure = is_high_memory_pressure(10000, 5000); /* 50% */
	assert(high_pressure == 1);

	/* Zero RSS but has swap - high pressure */
	high_pressure = is_high_memory_pressure(0, 100);
	assert(high_pressure == 1);

	/* Zero RSS and zero swap - low pressure */
	high_pressure = is_high_memory_pressure(0, 0);
	assert(high_pressure == 0);

	/* Large values */
	high_pressure =
		is_high_memory_pressure(1024 * 1024, 200 * 1024); /* ~19% */
	assert(high_pressure == 1);

	/* socket_family_to_string tests */
	const char *family_str;

	/* Test known families */
	family_str = socket_family_to_string(1); /* AF_UNIX */
	assert(strcmp(family_str, "AF_UNIX") == 0);

	family_str = socket_family_to_string(2); /* AF_INET */
	assert(strcmp(family_str, "AF_INET") == 0);

	family_str = socket_family_to_string(10); /* AF_INET6 */
	assert(strcmp(family_str, "AF_INET6") == 0);

	family_str = socket_family_to_string(16); /* AF_NETLINK */
	assert(strcmp(family_str, "AF_NETLINK") == 0);

	/* Test unknown family */
	family_str = socket_family_to_string(99);
	assert(strcmp(family_str, "UNKNOWN") == 0);

	family_str = socket_family_to_string(0);
	assert(strcmp(family_str, "UNKNOWN") == 0);

	/* socket_type_to_string tests */
	const char *type_str;

	/* Test known types */
	type_str = socket_type_to_string(1); /* SOCK_STREAM */
	assert(strcmp(type_str, "STREAM") == 0);

	type_str = socket_type_to_string(2); /* SOCK_DGRAM */
	assert(strcmp(type_str, "DGRAM") == 0);

	type_str = socket_type_to_string(3); /* SOCK_RAW */
	assert(strcmp(type_str, "RAW") == 0);

	/* Test unknown type */
	type_str = socket_type_to_string(99);
	assert(strcmp(type_str, "UNKNOWN") == 0);

	type_str = socket_type_to_string(0);
	assert(strcmp(type_str, "UNKNOWN") == 0);

	/* socket_protocol_to_string tests */
	const char *proto_str;

	proto_str = socket_protocol_to_string(6); /* IPPROTO_TCP */
	assert(strcmp(proto_str, "TCP") == 0);

	proto_str = socket_protocol_to_string(17); /* IPPROTO_UDP */
	assert(strcmp(proto_str, "UDP") == 0);

	proto_str = socket_protocol_to_string(1); /* ICMP */
	assert(strcmp(proto_str, "OTHER") == 0);

	proto_str = socket_protocol_to_string(0);
	assert(strcmp(proto_str, "OTHER") == 0);

	/* traffic line formatting tests */
	char traffic_buf[200];

	len = format_tcp_traffic_line(10, 2048, 11, 3072, traffic_buf,
				      sizeof(traffic_buf));
	assert(len > 0);
	assert(strcmp(traffic_buf, "          Traffic: RX pkts=10 bytes=2048  "
				   "TX pkts=11 bytes=3072\n") == 0);

	len = format_udp_traffic_line(3, 512, 4, 1024, traffic_buf,
				      sizeof(traffic_buf));
	assert(len > 0);
	assert(strcmp(traffic_buf, "          Traffic: RX pkts=3 bytes=512  TX "
				   "pkts=4 bytes=1024 (queued)\n") == 0);

	len = format_tcp_traffic_line(1, 1, 1, 1, NULL, sizeof(traffic_buf));
	assert(len == 0);

	len = format_udp_traffic_line(1, 1, 1, 1, traffic_buf, 10);
	assert(len == 0);

	/* socket_state_to_string tests */
	const char *state_str;

	/* Test all TCP states */
	state_str = socket_state_to_string(1); /* TCP_ESTABLISHED */
	assert(strcmp(state_str, "ESTABLISHED") == 0);

	state_str = socket_state_to_string(2); /* TCP_SYN_SENT */
	assert(strcmp(state_str, "SYN_SENT") == 0);

	state_str = socket_state_to_string(3); /* TCP_SYN_RECV */
	assert(strcmp(state_str, "SYN_RECV") == 0);

	state_str = socket_state_to_string(4); /* TCP_FIN_WAIT1 */
	assert(strcmp(state_str, "FIN_WAIT1") == 0);

	state_str = socket_state_to_string(5); /* TCP_FIN_WAIT2 */
	assert(strcmp(state_str, "FIN_WAIT2") == 0);

	state_str = socket_state_to_string(6); /* TCP_TIME_WAIT */
	assert(strcmp(state_str, "TIME_WAIT") == 0);

	state_str = socket_state_to_string(7); /* TCP_CLOSE */
	assert(strcmp(state_str, "CLOSE") == 0);

	state_str = socket_state_to_string(8); /* TCP_CLOSE_WAIT */
	assert(strcmp(state_str, "CLOSE_WAIT") == 0);

	state_str = socket_state_to_string(9); /* TCP_LAST_ACK */
	assert(strcmp(state_str, "LAST_ACK") == 0);

	state_str = socket_state_to_string(10); /* TCP_LISTEN */
	assert(strcmp(state_str, "LISTEN") == 0);

	state_str = socket_state_to_string(11); /* TCP_CLOSING */
	assert(strcmp(state_str, "CLOSING") == 0);

	state_str = socket_state_to_string(12); /* TCP_NEW_SYN_RECV */
	assert(strcmp(state_str, "NEW_SYN_RECV") == 0);

	/* Test unknown state */
	state_str = socket_state_to_string(0);
	assert(strcmp(state_str, "UNKNOWN") == 0);

	state_str = socket_state_to_string(99);
	assert(strcmp(state_str, "UNKNOWN") == 0);

	state_str = socket_state_to_string(255);
	assert(strcmp(state_str, "UNKNOWN") == 0);

	/* netdev_count tests */
	struct netdev_count devs[ELF_DET_NETDEV_MAX];
	int dev_len = 0;
	int start_len;
	int i;

	memset(devs, 0, sizeof(devs));
	add_netdev_count(devs, &dev_len, ELF_DET_NETDEV_MAX, 2, "eth0");
	assert(dev_len == 1);
	assert(devs[0].ifindex == 2);
	assert(devs[0].count == 1);
	assert(strcmp(devs[0].name, "eth0") == 0);

	add_netdev_count(devs, &dev_len, ELF_DET_NETDEV_MAX, 2, "eth1");
	assert(dev_len == 1);
	assert(devs[0].count == 2);
	assert(strcmp(devs[0].name, "eth0") == 0);

	add_netdev_count(devs, &dev_len, ELF_DET_NETDEV_MAX, 1, "lo");
	assert(dev_len == 2);
	assert(devs[1].ifindex == 1);
	assert(devs[1].count == 1);
	assert(strcmp(devs[1].name, "lo") == 0);

	start_len = dev_len;
	for (i = start_len; i < ELF_DET_NETDEV_MAX; i++) {
		char name_buf[16];

		snprintf(name_buf, sizeof(name_buf), "d%d", i);
		add_netdev_count(devs, &dev_len, ELF_DET_NETDEV_MAX, 100 + i,
				 name_buf);
	}
	assert(dev_len == ELF_DET_NETDEV_MAX);

	add_netdev_count(devs, &dev_len, ELF_DET_NETDEV_MAX, 999, "extra");
	assert(dev_len == ELF_DET_NETDEV_MAX);

	/* procfile write/read logic tests */
	{
		char pid_buf[20];
		size_t copied;
		char out_buf[64];
		int finished_state;

		copied = update_pid_write_buffer(pid_buf, sizeof(pid_buf),
						 "12345", 5);
		assert(copied == 5);
		assert(strcmp(pid_buf, "12345") == 0);

		copied = update_pid_write_buffer(pid_buf, sizeof(pid_buf), "1",
						 1);
		assert(copied == 1);
		assert(strcmp(pid_buf, "1") == 0);
		assert(pid_buf[1] == '\0');

		copied = update_pid_write_buffer(pid_buf, sizeof(pid_buf),
						 "1234567890123456789012345",
						 25);
		assert(copied == sizeof(pid_buf) - 1);
		assert(pid_buf[sizeof(pid_buf) - 1] == '\0');

		finished_state = 0;
		assert(procfile_read_should_finish(&finished_state) == 0);
		assert(finished_state == 1);
		assert(procfile_read_should_finish(&finished_state) == 1);
		assert(finished_state == 0);

		assert(format_procfile_output("42", out_buf, sizeof(out_buf)) >
		       0);
		assert(strcmp(out_buf, "buff variable : 42\n") == 0);
	}

	puts("elf_helpers tests passed");
	puts("memory_pressure tests passed");
	puts("socket_helpers tests passed");
	puts("netdev_count tests passed");
	return 0;
}
