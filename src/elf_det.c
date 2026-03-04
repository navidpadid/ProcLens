// SPDX-License-Identifier: MIT
#include <linux/init.h>
#include <linux/module.h> //for module programming
#include <linux/sched.h> //for task_struct
#include <linux/jiffies.h> //for file_operations write and read
#include <linux/kernel.h> //for kernel programming
#include <linux/cred.h>
#include <linux/proc_fs.h> //for using proc
#include <linux/seq_file.h> // for using seq operations
#include <linux/fs.h> //for using file_operations
#include <linux/mm_types.h> //for using vm_area struct
#include <linux/mm.h> //for mm_struct and VMA access
#include <linux/uaccess.h> //for user to kernel and vice versa access
#include <linux/string.h> //for string libs
#include <linux/sched/signal.h> //for task iteration
#include <linux/sched/cputime.h> //for task_cputime
#include <linux/fdtable.h> //for file descriptor table
#include <linux/net.h> //for socket operations
#include <linux/netdevice.h> //for net_device
#include <net/sock.h> //for sock structure
#include <linux/tcp.h> //for TCP states
#include <linux/in.h> //for sockaddr_in
#include <linux/in6.h> //for sockaddr_in6
#include <net/inet_sock.h> //for inet_sock
#include "elf_det.h"

MODULE_LICENSE("Dual MIT/GPL"); // module license

static char buff[20] =
	"1"; // the common(global) buffer between kernel and user space
static int user_pid; // the desired pid that we get from user
static int number_opens; // number of opens(writes) to the pid file

// skip these instances (will be described bellow)
static struct proc_dir_entry *elfdet_dir, *elfdet_det_entry, *elfdet_pid_entry,
	*elfdet_threads_entry;

static int procfile_open(struct inode *inode, struct file *file);
static ssize_t procfile_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t
procfile_write(struct file *, const char __user *, size_t, loff_t *);

static void print_memory_layout(struct seq_file *m,
				struct task_struct *task,
				unsigned long bss_start,
				unsigned long bss_end,
				unsigned long heap_start,
				unsigned long heap_end,
				unsigned long stack_start,
				unsigned long stack_end,
				unsigned long elf_base)
{
	seq_puts(m, "\nMemory Layout:\n");
	seq_puts(m,
		 "----------------------------------------------------------");
	seq_puts(m, "----------------------\n");
	seq_printf(m, "  Code Section:    0x%016lx - 0x%016lx\n",
		   task->mm->start_code, task->mm->end_code);
	seq_printf(m, "  Data Section:    0x%016lx - 0x%016lx\n",
		   task->mm->start_data, task->mm->end_data);
	seq_printf(m, "  BSS Section:     0x%016lx - 0x%016lx\n", bss_start,
		   bss_end);
	seq_printf(m, "  Heap:            0x%016lx - 0x%016lx\n", heap_start,
		   heap_end);
	seq_printf(m, "  Stack:           0x%016lx - 0x%016lx\n", stack_start,
		   stack_end);
	seq_printf(m, "  ELF Base:        0x%016lx\n", elf_base);
}

static void print_memory_layout_visualization(struct seq_file *m,
					      struct task_struct *task,
					      unsigned long bss_start,
					      unsigned long bss_end,
					      unsigned long heap_start,
					      unsigned long heap_end,
					      unsigned long stack_start,
					      unsigned long stack_end)
{
	struct memory_region regions[5];
	unsigned long total_size;
	unsigned long lowest_addr = task->mm->start_code;
	unsigned long highest_addr = stack_start;
	const int BAR_WIDTH = 50;
	int widths[5];
	char viz_buf[256];
	int i;

	/* Setup regions */
	regions[0].name = "CODE";
	regions[0].size = task->mm->end_code - task->mm->start_code;
	regions[0].exists = (regions[0].size > 0);

	regions[1].name = "DATA";
	regions[1].size = task->mm->end_data - task->mm->start_data;
	regions[1].exists = (regions[1].size > 0);

	regions[2].name = "BSS";
	regions[2].size = bss_end - bss_start;
	regions[2].exists = (regions[2].size > 0);

	regions[3].name = "HEAP";
	regions[3].size = heap_end - heap_start;
	regions[3].exists = (regions[3].size > 0);

	regions[4].name = "STACK";
	regions[4].size = stack_start - stack_end;
	regions[4].exists = (regions[4].size > 0);

	/* Calculate total size */
	total_size = 0;
	for (i = 0; i < 5; i++) {
		if (regions[i].exists)
			total_size += regions[i].size;
	}

	if (total_size == 0)
		return;

	/* Calculate proportional widths */
	for (i = 0; i < 5; i++) {
		widths[i] = calculate_bar_width(regions[i].size, total_size,
						BAR_WIDTH);
	}

	seq_puts(m, "\n");
	seq_puts(m, "Memory Layout Visualization:\n");
	seq_puts(m, "------------------------------------------");
	seq_puts(m, "----------------");
	seq_puts(m, "----------------------\n");
	seq_printf(m, "Low:  0x%016lx\n\n", lowest_addr);

	/* Generate visualization for each region */
	for (i = 0; i < 5; i++) {
		if (generate_region_visualization(&regions[i], widths[i],
						  BAR_WIDTH, viz_buf,
						  sizeof(viz_buf)) > 0) {
			seq_puts(m, viz_buf);
		}
	}

	seq_printf(m, "High: 0x%016lx\n", highest_addr);
	seq_puts(m, "------------------------------------------");
	seq_puts(m, "----------------");
	seq_puts(m, "----------------------\n");
}

static void print_thread_info_line(struct seq_file *m,
				   struct task_struct *thread)
{
	char state_char;
	char cpu_affinity[32];
	int cpu_mask[8] = {0};
	int i;
	u64 total_ns, delta_ns, usage_permyriad;

	/* Get thread state using kernel helper */
	state_char = task_state_to_char(thread);

	/* CPU usage for this thread */
	total_ns = (u64)thread->utime + (u64)thread->stime;
	delta_ns = ktime_get_ns() - thread->start_time;
	usage_permyriad = compute_usage_permyriad(total_ns, delta_ns);

	/* Build CPU affinity mask array (show first 8 CPUs) */
	for (i = 0; i < 8 && i < nr_cpu_ids; i++)
		cpu_mask[i] = cpumask_test_cpu(i, &thread->cpus_mask) ? 1 : 0;
	build_cpu_affinity_string(cpu_mask, 8, cpu_affinity,
				  sizeof(cpu_affinity));

	seq_printf(m,
		   "%-5d  %-15.15s  %4llu.%02llu   %c      %4d      %4d  %s\n",
		   thread->pid, thread->comm, (usage_permyriad / 100),
		   (usage_permyriad % 100), state_char,
		   thread->prio - 120, /* Convert to nice value */
		   task_nice(thread), cpu_affinity);
}

// det proc file_operations starts

/* Find the lower boundary of the stack VMA
 * Iterates through VMAs to find the one containing start_stack
 * Returns the vm_start (lower bound) of the stack VMA, or 0 if not found
 */
static unsigned long find_stack_vma_end(struct mm_struct *mm,
					unsigned long start_stack)
{
	struct vm_area_struct *vma;
	struct ma_state mas;
	unsigned long stack_end = 0;

	mas_init(&mas, &mm->mm_mt, 0);
	mas_for_each(&mas, vma, ULONG_MAX)
	{
		/* Use helper to check if start_stack is within this VMA */
		if (is_address_in_range(start_stack, vma->vm_start,
					vma->vm_end)) {
			/* Found the stack VMA */
			stack_end = vma->vm_start; /* Stack grows down */
			break;
		}
	}

	return stack_end;
}

/* Calculate and display memory pressure statistics
 * Includes RSS, PSS, swap usage, page faults, and OOM score
 */
static void print_memory_pressure(struct seq_file *m, struct task_struct *task)
{
	struct mm_struct *mm = task->mm;
	unsigned long rss_pages, swap_pages, file_pages, anon_pages;
	unsigned long shmem_pages;
	unsigned long rss_kb, swap_kb;
	unsigned long maj_flt, min_flt;
	long oom_score;

	if (!mm)
		return;

	/* Get RSS (Resident Set Size) - total physical memory used
	 * RSS = file pages + anon pages + shared memory pages
	 */
	file_pages = get_mm_counter(mm, MM_FILEPAGES);
	anon_pages = get_mm_counter(mm, MM_ANONPAGES);
	shmem_pages = get_mm_counter(mm, MM_SHMEMPAGES);
	rss_pages = file_pages + anon_pages + shmem_pages;
	rss_kb = rss_pages << (PAGE_SHIFT - 10); // Convert pages to KB

	/* Get swap usage */
	swap_pages = get_mm_counter(mm, MM_SWAPENTS);
	swap_kb = swap_pages << (PAGE_SHIFT - 10);

	/* Get page faults from task_struct
	 * Major faults: required disk I/O
	 * Minor faults: resolved from memory/cache
	 */
	maj_flt = task->maj_flt;
	min_flt = task->min_flt;

	/* Calculate approximate OOM score
	 * This is a simplified version based on RSS as percentage of total RAM
	 * Real kernel OOM score is more complex but not exported to modules
	 * Range: adjusted by oom_score_adj (-1000 to 1000)
	 */
	oom_score = task->signal->oom_score_adj;

	seq_puts(m, "\nMemory Pressure Statistics:\n");
	seq_puts(m,
		 "----------------------------------------------------------");
	seq_puts(m, "----------------------\n");

	/* Display RSS breakdown */
	seq_printf(m, "  RSS (Resident):  %lu KB\n", rss_kb);
	seq_printf(m, "    - Anonymous:   %lu KB\n",
		   anon_pages << (PAGE_SHIFT - 10));
	seq_printf(m, "    - File-backed: %lu KB\n",
		   file_pages << (PAGE_SHIFT - 10));
	seq_printf(m, "    - Shared Mem:  %lu KB\n",
		   shmem_pages << (PAGE_SHIFT - 10));

	/* Virtual memory size */
	seq_printf(m, "  VSZ (Virtual):   %lu KB\n",
		   mm->total_vm << (PAGE_SHIFT - 10));

	/* Swap usage */
	seq_printf(m, "  Swap Usage:      %lu KB\n", swap_kb);

	/* Page faults */
	seq_puts(m, "  Page Faults:\n");
	seq_printf(m, "    - Major:       %lu\n", maj_flt);
	seq_printf(m, "    - Minor:       %lu\n", min_flt);
	seq_printf(m, "    - Total:       %lu\n", maj_flt + min_flt);

	/* OOM score adjustment
	 * Negative values make process less likely to be OOM killed
	 * Positive values make it more likely
	 * Range: -1000 (never kill) to 1000 (always prefer)
	 */
	seq_printf(m, "  OOM Score Adj:   %ld\n", oom_score);

	seq_puts(m,
		 "----------------------------------------------------------");
	seq_puts(m, "----------------------\n");
}

/* Display brief per-process network statistics
 * Counts are best-effort and primarily reflect TCP socket counters.
 */
static void print_network_stats(struct seq_file *m, struct task_struct *task)
{
	struct files_struct *files;
	struct fdtable *fdt;
	struct file *file;
	struct socket *sock;
	struct sock *sk;
	struct tcp_sock *tp;
	unsigned int fd;
	u64 rx_packets = 0, tx_packets = 0;
	u64 rx_bytes = 0, tx_bytes = 0;
	u64 tcp_retransmits = 0;
	u64 drops = 0;
	int socket_total = 0, tcp_count = 0, udp_count = 0, unix_count = 0;
	struct netdev_count netdevs[ELF_DET_NETDEV_MAX];
	int netdev_len = 0;
	int ifindex;
	struct net_device *dev;
	const char *dev_name;
	int i;

	files = task->files;
	if (!files)
		return;

	seq_puts(m, "\n[network]\n");

	rcu_read_lock();
	fdt = files_fdtable(files);

	for (fd = 0; fd < fdt->max_fds; fd++) {
		file = rcu_dereference(fdt->fd[fd]);
		if (!file)
			continue;

		sock = sock_from_file(file);
		if (!sock)
			continue;

		socket_total++;
		sk = sock->sk;
		if (!sk)
			continue;

		drops += (u64)atomic_read(&sk->sk_drops);

		if (sk->sk_protocol == IPPROTO_TCP) {
			tp = tcp_sk(sk);
			tcp_count++;
			rx_packets += (u64)READ_ONCE(tp->segs_in);
			tx_packets += (u64)READ_ONCE(tp->segs_out);
			rx_bytes += (u64)READ_ONCE(tp->bytes_received);
			tx_bytes += (u64)READ_ONCE(tp->bytes_sent);
			tcp_retransmits += (u64)READ_ONCE(tp->retrans_out);
		} else if (sk->sk_protocol == IPPROTO_UDP) {
			udp_count++;
		}

		if (sk->sk_family == AF_UNIX)
			unix_count++;

		ifindex = READ_ONCE(sk->sk_bound_dev_if);
		if (!ifindex)
			ifindex = READ_ONCE(sk->sk_rx_dst_ifindex);

		if (ifindex > 0) {
			dev = dev_get_by_index_rcu(sock_net(sk), ifindex);
			dev_name = dev ? dev->name : "unknown";
			add_netdev_count(netdevs, &netdev_len,
					 ELF_DET_NETDEV_MAX, ifindex, dev_name);
		}
	}

	rcu_read_unlock();

	seq_printf(m, "sockets_total: %d (tcp: %d, udp: %d, unix: %d)\n",
		   socket_total, tcp_count, udp_count, unix_count);
	seq_printf(m, "rx_packets: %llu\n", rx_packets);
	seq_printf(m, "tx_packets: %llu\n", tx_packets);
	seq_printf(m, "rx_bytes: %llu\n", rx_bytes);
	seq_printf(m, "tx_bytes: %llu\n", tx_bytes);
	seq_printf(m, "tcp_retransmits: %llu\n", tcp_retransmits);
	seq_printf(m, "drops: %llu\n", drops);

	if (netdev_len == 0) {
		seq_puts(m, "net_devices: none\n");
		return;
	}

	seq_puts(m, "net_devices: ");
	for (i = 0; i < netdev_len; i++) {
		seq_printf(m, "%s=%d", netdevs[i].name, netdevs[i].count);
		if (i + 1 < netdev_len)
			seq_puts(m, " ");
	}
	seq_puts(m, "\n");
}

/* Display open socket information for the process
 * Shows socket file descriptors including family, type, state, and addresses
 */
static void print_sockets(struct seq_file *m, struct task_struct *task)
{
	struct files_struct *files;
	struct fdtable *fdt;
	struct file *file;
	struct socket *sock;
	struct sock *sk;
	struct inet_sock *inet;
	struct tcp_sock *tp;
	unsigned int fd;
	int socket_count = 0;
	unsigned short family, type;
	unsigned char state;
	const char *protocol;
	__be32 saddr, daddr;
	__be16 sport, dport;
	u64 rx_packets, tx_packets, rx_bytes, tx_bytes;
	u32 udp_rx_packets, udp_tx_packets;
	char traffic_line[160];
	int traffic_len;
	int i;

	files = task->files;
	if (!files)
		return;

	seq_puts(m, "\nOpen Sockets:\n");
	seq_puts(m,
		 "----------------------------------------------------------");
	seq_puts(m, "----------------------\n");

	rcu_read_lock();
	fdt = files_fdtable(files);

	/* Iterate through file descriptors */
	for (fd = 0; fd < fdt->max_fds; fd++) {
		file = rcu_dereference(fdt->fd[fd]);
		if (!file)
			continue;

		/* Check if this file descriptor is a socket */
		sock = sock_from_file(file);
		if (!sock)
			continue;

		socket_count++;
		sk = sock->sk;
		if (!sk)
			continue;

		family = sk->sk_family;
		type = sk->sk_type;
		state = sk->sk_state;
		protocol = socket_protocol_to_string(sk->sk_protocol);

		seq_printf(m,
			   "  [FD %u] Family: %-10s  Type: %-8s  State: %-12s  "
			   "Proto: %-5s\n",
			   fd, socket_family_to_string(family),
			   socket_type_to_string(type),
			   socket_state_to_string(state), protocol);

		if (sk->sk_protocol == IPPROTO_TCP) {
			tp = tcp_sk(sk);
			rx_packets = (u64)READ_ONCE(tp->segs_in);
			tx_packets = (u64)READ_ONCE(tp->segs_out);
			rx_bytes = (u64)READ_ONCE(tp->bytes_received);
			tx_bytes = (u64)READ_ONCE(tp->bytes_sent);
			traffic_len = format_tcp_traffic_line(
				rx_packets, rx_bytes, tx_packets, tx_bytes,
				traffic_line, sizeof(traffic_line));
			if (traffic_len > 0)
				seq_puts(m, traffic_line);
		} else if (sk->sk_protocol == IPPROTO_UDP) {
			udp_rx_packets = skb_queue_len(&sk->sk_receive_queue);
			udp_tx_packets = skb_queue_len(&sk->sk_write_queue);
			rx_bytes = (u64)sk_rmem_alloc_get(sk);
			tx_bytes = (u64)READ_ONCE(sk->sk_wmem_queued);
			traffic_len = format_udp_traffic_line(
				udp_rx_packets, rx_bytes, udp_tx_packets,
				tx_bytes, traffic_line, sizeof(traffic_line));
			if (traffic_len > 0)
				seq_puts(m, traffic_line);
		}

		/* Display address information for inet sockets */
		if (family == AF_INET && sk->sk_prot) {
			inet = inet_sk(sk);
			if (inet) {
				unsigned int saddr_h, daddr_h;

				saddr = inet->inet_saddr;
				daddr = inet->inet_daddr;
				sport = inet->inet_sport;
				dport = inet->inet_dport;

				/* Convert to host byte order for display */
				saddr_h = ntohl(saddr);
				daddr_h = ntohl(daddr);

				seq_printf(m,
					   "          Local:  %u.%u.%u.%u:%u",
					   (saddr_h >> 24) & 0xFF,
					   (saddr_h >> 16) & 0xFF,
					   (saddr_h >> 8) & 0xFF,
					   saddr_h & 0xFF, ntohs(sport));
				seq_printf(m, "  Remote: %u.%u.%u.%u:%u\n",
					   (daddr_h >> 24) & 0xFF,
					   (daddr_h >> 16) & 0xFF,
					   (daddr_h >> 8) & 0xFF,
					   daddr_h & 0xFF, ntohs(dport));
			}
		} else if (family == AF_INET6 && sk->sk_prot) {
			/* IPv6 addresses */
			struct in6_addr *saddr6 = &sk->sk_v6_rcv_saddr;
			struct in6_addr *daddr6 = &sk->sk_v6_daddr;

			inet = inet_sk(sk);
			if (inet) {
				sport = inet->inet_sport;
				dport = inet->inet_dport;

				seq_puts(m, "          Local:  ");
				for (i = 0; i < 8; i++) {
					if (i > 0)
						seq_puts(m, ":");
					seq_printf(m, "%04x",
						   ntohs(saddr6->s6_addr16[i]));
				}
				seq_printf(m, ":%u", ntohs(sport));

				seq_puts(m, "  Remote: ");
				for (i = 0; i < 8; i++) {
					if (i > 0)
						seq_puts(m, ":");
					seq_printf(m, "%04x",
						   ntohs(daddr6->s6_addr16[i]));
				}
				seq_printf(m, ":%u\n", ntohs(dport));
			}
		}
	}

	rcu_read_unlock();

	if (socket_count == 0)
		seq_puts(m, "  No open sockets\n");

	seq_puts(m,
		 "----------------------------------------------------------");
	seq_puts(m, "----------------------\n");
}

// this function is the base function to gather information from kernel
static int elfdet_show(struct seq_file *m, void *v)
{
	struct task_struct *task;
	unsigned long bss_start = 0, bss_end = 0;
	unsigned long heap_start = 0, heap_end = 0;
	unsigned long stack_start = 0, stack_end = 0;
	unsigned long elf_base = 0;
	u64 delta_ns, total_ns;
	u64 usage_permyriad; // CPU usage in hundredths of a percent (X.XX%)
	const struct vm_area_struct *vma;
	struct ma_state mas;
	int ret;

	ret = kstrtoint(buff, 10, &user_pid);
	if (ret != 0) {
		seq_puts(m, "Failed to parse PID\n");
		return 0;
	}

	task = pid_task(find_vpid(user_pid), PIDTYPE_PID);

	if (!task || !task->mm) {
		seq_puts(m, "Invalid PID or process has no memory context\n");
		return 0;
	}

	/* CPU usage: total CPU time of task since start divided by elapsed wall
	 * time
	 */
	total_ns = (u64)task->utime + (u64)task->stime;
	delta_ns = ktime_get_ns() - task->start_time;
	usage_permyriad = compute_usage_permyriad(total_ns, delta_ns);

	// Access VMA using VMA iterator for kernel 6.8+
	if (mmap_read_lock_killable(task->mm)) {
		seq_puts(m, "Failed to lock mm\n");
		return 0;
	}

	/* Use mm fields directly for ELF, BSS, heap, and stack
	 * Note: Modern ELF binaries may have end_data == start_brk (no BSS)
	 * rodata is typically merged with code section (start_code to end_code)
	 * Heap shown is brk-based; mmap-allocated heap is not tracked here
	 */

	/* ELF base: First VMA is typically the ELF binary base (for PIE) */
	mas_init(&mas, &task->mm->mm_mt, 0);
	vma = mas_find(&mas, ULONG_MAX);
	if (vma)
		elf_base = vma->vm_start;

	/* Stack: Find the [stack] VMA for actual stack boundaries */
	stack_start = task->mm->start_stack;
	stack_end = find_stack_vma_end(task->mm, stack_start);

	/* BSS: uninitialized data between end_data and start_brk
	 * May be zero-length in modern binaries
	 */
	compute_bss_range(task->mm->end_data, task->mm->start_brk, &bss_start,
			  &bss_end);

	/* Heap: brk-based heap from start_brk to current brk
	 * Note: Does not include mmap-based allocations (arena heap)
	 */
	compute_heap_range(task->mm->start_brk, task->mm->brk, &heap_start,
			   &heap_end);

	mmap_read_unlock(task->mm);

	// now print the information we want to the det file
	seq_printf(m, "Process ID:      %d\n", task->pid);
	seq_printf(m, "Name:            %s\n", task->comm);
	seq_printf(m, "CPU Usage:       %llu.%02llu%%\n",
		   (usage_permyriad / 100), (usage_permyriad % 100));
	print_memory_pressure(m, task);
	print_memory_layout(m, task, bss_start, bss_end, heap_start, heap_end,
			    stack_start, stack_end, elf_base);
	print_memory_layout_visualization(m, task, bss_start, bss_end,
					  heap_start, heap_end, stack_start,
					  stack_end);
	print_network_stats(m, task);
	print_sockets(m, task);

	return 0;
}

// this function gathers thread information from kernel
static int elfdet_threads_show(struct seq_file *m, void *v)
{
	struct task_struct *task, *thread;
	int ret, thread_count = 0;

	ret = kstrtoint(buff, 10, &user_pid);
	if (ret != 0) {
		seq_puts(m, "Failed to parse PID\n");
		return 0;
	}

	task = pid_task(find_vpid(user_pid), PIDTYPE_PID);

	if (!task) {
		seq_puts(m, "Invalid PID\n");
		return 0;
	}

	// Print header
	seq_puts(m, "TID    NAME             CPU(%)   STATE  PRIORITY  NICE  ");
	seq_puts(m, "CPU_AFFINITY\n");
	seq_puts(m, "-----  ---------------  -------  -----  --------  ----  ");
	seq_puts(m, "----------------\n");

	// Iterate through all threads in the thread group
	rcu_read_lock();
	// clang-format off
	for_each_thread(task, thread) {
		thread_count++;
		print_thread_info_line(m, thread);
	}
	// clang-format on
	rcu_read_unlock();

	seq_puts(m,
		 "----------------------------------------------------------");
	seq_puts(m, "----------------------\n");
	seq_printf(m, "Total threads: %d\n", thread_count);

	return 0;
}

// runs when opening file
static int elfdet_open(struct inode *inode, struct file *file)
{
	return single_open(file, elfdet_show, NULL); // calling elfdet_show
}

// runs when opening threads file
static int elfdet_threads_open(struct inode *inode, struct file *file)
{
	return single_open(file, elfdet_threads_show, NULL);
}

// file operations of det proc (using proc_ops for kernel 5.6+)
static const struct proc_ops elfdet_det_ops = {
	.proc_open = elfdet_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

// file operations of threads proc
static const struct proc_ops elfdet_threads_ops = {
	.proc_open = elfdet_threads_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

// elf proc file_operations starts

// runs when elf opens
// will be called every time this file is accessed shows number of accessed
// times
static int procfile_open(struct inode *inode, struct file *file)
{
	number_opens++;
	pr_info("procfile opened %d times\n", number_opens);
	return 0;
}

// when we cat elf file this function will be run (this is useless here)
// because our info is in det file not here!
static ssize_t procfile_read(struct file *file,
			     char __user *buffer,
			     size_t length,
			     loff_t *offset)
{
	static int finished;
	char tmp[64];
	int len;

	// normal return value other than '0' will cause loop

	pr_info("procfile read called\n");

	if (procfile_read_should_finish(&finished)) {
		pr_info("procfs read: END\n");
		return 0;
	}

	len = format_procfile_output(buff, tmp, sizeof(tmp));
	if (len < 0)
		return -EFAULT;
	if (len > length)
		len = length;
	if (copy_to_user(buffer, tmp, len))
		return -EFAULT;
	return len;
}

// most important function of elf! called when we write some characters into it
static ssize_t procfile_write(struct file *file,
			      const char __user *buffer,
			      size_t length,
			      loff_t *offset)
{
	char input_buf[sizeof(buff)];
	size_t to_copy;

	to_copy = min(length, sizeof(input_buf));

	if (copy_from_user(input_buf, buffer, to_copy))
		return -EFAULT;

	update_pid_write_buffer(buff, sizeof(buff), input_buf, to_copy);
	pr_info("procfs_write called\n");
	return length;
}

static const struct proc_ops write_pops = {
	.proc_open = procfile_open,
	.proc_read = procfile_read,
	.proc_write = procfile_write, // this is the important part
};

static int elfdet_init(void)
{
	elfdet_dir = proc_mkdir("elf_det", NULL);
	// creating the directory: elf_det in proc

	if (!elfdet_dir)
		return -ENOMEM;

	// 0644 means owner read/write, others read-only
	elfdet_det_entry =
		proc_create("det", 0644, elfdet_dir, &elfdet_det_ops);
	// create proc file det with elfdet_det_ops
	pr_info("det initiated; /proc/elf_det/det created\n");
	elfdet_pid_entry = proc_create("pid", 0644, elfdet_dir, &write_pops);
	// create proc file pid with write_pops
	pr_info("pid initiated; /proc/elf_det/pid created\n");

	elfdet_threads_entry =
		proc_create("threads", 0644, elfdet_dir, &elfdet_threads_ops);
	// create proc file threads with elfdet_threads_ops
	pr_info("threads initiated; /proc/elf_det/threads created\n");

	if (!elfdet_det_entry || !elfdet_threads_entry)
		return -ENOMEM;

	return 0;
}

// the remove operations done by module(cleaning up)
static void elfdet_exit(void)
{
	proc_remove(elfdet_det_entry);
	pr_info("elf_det exited; /proc/elf_det/det deleted\n");
	proc_remove(elfdet_pid_entry);
	pr_info("elf_det exited; /proc/elf_det/pid deleted\n");
	proc_remove(elfdet_threads_entry);
	pr_info("elf_det exited; /proc/elf_det/threads deleted\n");
	proc_remove(elfdet_dir);
}

// macros for init and exit
module_init(elfdet_init);
module_exit(elfdet_exit);
