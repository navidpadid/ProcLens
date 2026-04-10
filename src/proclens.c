// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include "proclens.h"

#ifndef PROCLENS_VERSION
#define PROCLENS_VERSION "dev"
#endif

#define C_RESET	  "\033[0m"
#define C_BOLD	  "\033[1m"
#define C_CYAN	  "\033[36m"
#define C_GREEN	  "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_MAGENTA "\033[35m"
#define C_BLUE	  "\033[34m"

static volatile sig_atomic_t g_restore_terminal;
static struct termios g_saved_termios;
static int g_use_color;

static const char *color_code(const char *code)
{
	return g_use_color ? code : "";
}

static void init_color_output(void)
{
	const char *no_color = getenv("NO_COLOR");

	g_use_color = isatty(STDOUT_FILENO) && (!no_color || no_color[0] == '\0');
}

static void restore_terminal(void)
{
	if (g_restore_terminal) {
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios);
		g_restore_terminal = 0;
	}
}

static void sigint_handler(int sig)
{
	(void)sig;
	restore_terminal();
	_exit(0);
}

static void apply_raw_mode(void)
{
	struct termios raw;

	raw = g_saved_termios;
	raw.c_lflag &= ~(unsigned int)(ICANON | ECHO);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 0;
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
	g_restore_terminal = 1;
}

static int set_raw_mode(void)
{
	struct sigaction sa;

	if (tcgetattr(STDIN_FILENO, &g_saved_termios) < 0)
		return -1;

	apply_raw_mode();

	sa.sa_handler = sigint_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
	return 0;
}

#define PID_INPUT_MAX	20
#define PROC_BUF_SIZE	262144
#define MAX_SNAPSHOTS	120
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

enum view_mode {
	VIEW_MEMORY = 1,
	VIEW_NETWORK = 2,
	VIEW_THREADS = 3,
	VIEW_IO = 4,
	VIEW_OVERVIEW = 5,
};

struct overview_thread_entry {
	char line[256];
	unsigned long long cpu_permyriad;
	int valid;
};

struct overview_talker_entry {
	int rank;
	unsigned int fd;
	char proto[16];
	char family[16];
	unsigned long long rx_bytes;
	unsigned long long tx_bytes;
	unsigned long long total_bytes;
	int valid;
};

struct live_snapshot {
	char pid[PID_INPUT_MAX];
	int view;
	char captured_at[32];
	char *det_content;
	char *threads_content;
};

static void print_logo_text(void)
{
	printf("%s%s\n", color_code(C_CYAN), color_code(C_BOLD));
	/* clang-format off */
	puts(" /$$$$$$$                               /$$                                    ");
	puts("| $$__  $$                             | $$                                    ");
	puts("| $$  \\ $$ /$$$$$$   /$$$$$$   /$$$$$$$| $$        /$$$$$$  /$$$$$$$   /$$$$$$$");
	puts("| $$$$$$$//$$__  $$ /$$__  $$ /$$_____/| $$       /$$__  $$| $$__  $$ /$$_____/");
	puts("| $$____/| $$  \\__/| $$  \\ $$| $$      | $$      | $$$$$$$$| $$  \\ $$|  $$$$$$ ");
	puts("| $$     | $$      | $$  | $$| $$      | $$      | $$_____/| $$  | $$ \\____  $$");
	puts("| $$     | $$      |  $$$$$$/|  $$$$$$$| $$$$$$$$|  $$$$$$$| $$  | $$ /$$$$$$$/");
	puts("|__/     |__/       \\______/  \\_______/|________/ \\_______/|__/  |__/|_______/ ");
	puts("                                                                               ");
	puts("                                                                               ");
	puts("                                                                               ");
	/* clang-format on */
	printf("%s\n", color_code(C_RESET));
}

static int is_module_loaded(const char *module_name)
{
	FILE *fp;
	char line[256];
	size_t module_name_len;

	fp = fopen("/proc/modules", "r");
	if (!fp)
		return -1;

	module_name_len = strlen(module_name);
	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, module_name, module_name_len) == 0 &&
		    line[module_name_len] == ' ') {
			fclose(fp);
			return 1;
		}
	}

	fclose(fp);
	return 0;
}

static int ensure_module_loaded(void)
{
	int loaded;

	loaded = is_module_loaded("proclens_module");
	if (loaded < 0) {
		perror("open /proc/modules");
		return -1;
	}

	if (loaded == 0) {
		fprintf(stderr,
			"%serror:%s kernel module 'proclens_module' is not "
			"loaded\n",
			color_code(C_YELLOW), color_code(C_RESET));
		fprintf(stderr, "hint: run 'sudo insmod ./build/proclens_module.ko', "
				"'sudo modprobe proclens_module' (if installed), or "
				"'sudo make install'\n");
		return -1;
	}

	return 0;
}

static int ensure_proc_files_present(void)
{
	static const char *const required_files[] = {"pid", "det", "threads"};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(required_files); i++) {
		char *path;

		path = build_proc_path(required_files[i]);
		if (!path) {
			fprintf(stderr, "failed to allocate path for proc "
					"interface check\n");
			return -1;
		}

		if (access(path, F_OK) != 0) {
			fprintf(stderr,
				"%serror:%s required proc file is missing: "
				"%s\n",
				color_code(C_YELLOW), color_code(C_RESET), path);
			fprintf(stderr, "hint: confirm /proc/proclens_module is "
					"mounted and initialized\n");
			free(path);
			return -1;
		}

		free(path);
	}

	return 0;
}

static int ensure_root_privileges(void)
{
	if (geteuid() == 0)
		return 0;

	fprintf(stderr, "%serror:%s proclens requires root privileges\n", color_code(C_YELLOW),
		color_code(C_RESET));
	fprintf(stderr, "hint: run with sudo, e.g. 'sudo proclens'\n");
	return -1;
}

static void print_cmdline(const char *pid_str)
{
	char path[64];
	char cmdline[1024];
	FILE *fp;
	size_t len;
	size_t i;

	snprintf(path, sizeof(path), "/proc/%s/cmdline", pid_str);
	fp = fopen(path, "r");
	if (!fp)
		return;

	len = fread(cmdline, 1, sizeof(cmdline) - 1, fp);
	fclose(fp);
	if (len == 0)
		return;

	cmdline[len] = '\0';
	for (i = 0; i < len; i++) {
		if (cmdline[i] == '\0')
			cmdline[i] = ' ';
	}

	printf("%sCommand line:%s   %s\n", color_code(C_YELLOW), color_code(C_RESET), cmdline);
}

static void print_process_info(const char *pid_str)
{
	FILE *fp;
	char buff[2048];
	char *pid_path;
	char *det_path;
	char *threads_path;

	/* Write PID to proc file */
	pid_path = build_proc_path("pid");
	fp = fopen(pid_path, "w");
	if (!fp) {
		perror("open pid");
		free(pid_path);
		return;
	}
	fprintf(fp, "%s", pid_str);
	fclose(fp);
	free(pid_path);

	/* Read and display process info */
	printf("\n");
	printf("%s%s==========================================================="
	       "====\n",
	       color_code(C_CYAN), color_code(C_BOLD));
	printf("PROCESS INFORMATION\n");
	printf("==============================================================="
	       "%s\n",
	       color_code(C_RESET));
	print_cmdline(pid_str);
	det_path = build_proc_path("det");
	fp = fopen(det_path, "r");
	if (!fp) {
		perror("open det");
		free(det_path);
		return;
	}
	while (fgets(buff, sizeof(buff), fp))
		printf("%s", buff);
	fclose(fp);
	free(det_path);

	/* Read and display thread info */
	printf("\n");
	printf("%s%s==========================================================="
	       "====\n",
	       color_code(C_MAGENTA), color_code(C_BOLD));
	printf("THREAD INFORMATION\n");
	printf("==============================================================="
	       "%s\n",
	       color_code(C_RESET));
	threads_path = build_proc_path("threads");
	fp = fopen(threads_path, "r");
	if (!fp) {
		perror("open threads");
		free(threads_path);
		return;
	}
	while (fgets(buff, sizeof(buff), fp))
		printf("%s", buff);
	fclose(fp);
	free(threads_path);
	puts("===============================================================");
}

static int write_pid(const char *pid_str)
{
	FILE *fp;
	char *pid_path;

	pid_path = build_proc_path("pid");
	if (!pid_path) {
		fprintf(stderr, "failed to allocate pid path\n");
		return -1;
	}

	fp = fopen(pid_path, "w");
	if (!fp) {
		perror("open pid");
		free(pid_path);
		return -1;
	}

	fprintf(fp, "%s", pid_str);
	fclose(fp);
	free(pid_path);
	return 0;
}

static int is_memory_section_start(const char *line)
{
	return strncmp(line, "Memory Pressure Statistics:", 27) == 0 ||
	       strncmp(line, "Memory Layout:", 14) == 0 ||
	       strncmp(line, "Memory Layout Visualization:", 28) == 0;
}

static int read_content_line(const char **cursor, char *line, size_t line_size)
{
	size_t line_len = 0;

	if (!cursor || !*cursor || !**cursor || !line || line_size < 2)
		return 0;

	while ((*cursor)[line_len] && (*cursor)[line_len] != '\n' && line_len < line_size - 2)
		line_len++;

	memcpy(line, *cursor, line_len);
	if ((*cursor)[line_len] == '\n') {
		line[line_len++] = '\n';
		*cursor += line_len;
	} else {
		*cursor += line_len;
	}
	line[line_len] = '\0';
	return 1;
}

static void trim_newline(char *line)
{
	size_t len;

	if (!line)
		return;

	len = strlen(line);
	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
		line[len - 1] = '\0';
		len--;
	}
}

static void copy_truncated_string(char *dst, size_t dst_size, const char *src)
{
	size_t len;

	if (!dst || dst_size == 0)
		return;

	if (!src) {
		dst[0] = '\0';
		return;
	}

	len = strlen(src);
	if (len >= dst_size)
		len = dst_size - 1;

	memcpy(dst, src, len);
	dst[len] = '\0';
}

static int
copy_line_with_prefix(const char *content, const char *prefix, char *out, size_t out_size)
{
	char line[2048];
	const char *cursor = content;
	size_t prefix_len;

	if (!content || !prefix || !out || out_size == 0)
		return 0;

	prefix_len = strlen(prefix);
	while (read_content_line(&cursor, line, sizeof(line))) {
		if (strncmp(line, prefix, prefix_len) == 0) {
			snprintf(out, out_size, "%s", line);
			return 1;
		}
	}

	return 0;
}

static int collect_top_talkers(const char *det_content,
			       struct overview_talker_entry *talkers,
			       int max_talkers,
			       int *saw_none)
{
	char line[2048];
	const char *cursor = det_content;
	int in_top_talkers = 0;
	int count = 0;

	if (saw_none)
		*saw_none = 0;

	if (!det_content || !talkers || max_talkers <= 0)
		return 0;

	while (read_content_line(&cursor, line, sizeof(line))) {
		if (!in_top_talkers) {
			if (strncmp(line, "top_talkers:", 12) == 0)
				in_top_talkers = 1;
			continue;
		}

		if (strncmp(line, "  #", 3) == 0) {
			if (count < max_talkers) {
				int parsed;
				struct overview_talker_entry *entry = &talkers[count];

				memset(entry, 0, sizeof(*entry));
				parsed = sscanf(
					line,
					"  #%d FD %u Proto: %15s Family: %15s RX bytes=%llu TX bytes=%llu Total bytes=%llu",
					&entry->rank, &entry->fd, entry->proto, entry->family,
					&entry->rx_bytes, &entry->tx_bytes, &entry->total_bytes);
				if (parsed == 7)
					entry->valid = 1;
			}
			count++;
			continue;
		}

		if (strncmp(line, "  none", 6) == 0) {
			if (saw_none)
				*saw_none = 1;
			break;
		}

		if (strncmp(line, "Open Sockets:", 13) == 0 || strncmp(line, "[io]", 4) == 0)
			break;
	}

	return count;
}

static void maybe_insert_top_thread(struct overview_thread_entry *top_threads,
				    int top_len,
				    const char *line,
				    unsigned long long cpu_permyriad)
{
	int i;
	int insert_at = -1;

	for (i = 0; i < top_len; i++) {
		if (!top_threads[i].valid || cpu_permyriad > top_threads[i].cpu_permyriad) {
			insert_at = i;
			break;
		}
	}

	if (insert_at < 0)
		return;

	for (i = top_len - 1; i > insert_at; i--)
		top_threads[i] = top_threads[i - 1];

	top_threads[insert_at].cpu_permyriad = cpu_permyriad;
	top_threads[insert_at].valid = 1;
	copy_truncated_string(top_threads[insert_at].line, sizeof(top_threads[insert_at].line),
			      line);
	trim_newline(top_threads[insert_at].line);
}

static int collect_top_threads(const char *threads_content,
			       struct overview_thread_entry *top_threads,
			       int top_len)
{
	char line[2048];
	const char *cursor = threads_content;
	int count = 0;

	if (!threads_content || !top_threads || top_len <= 0)
		return 0;

	while (read_content_line(&cursor, line, sizeof(line))) {
		int tid;
		char name[32];
		unsigned long long cpu_whole;
		unsigned long long cpu_frac;
		char state;
		int priority;
		int nice_value;
		char affinity[64];
		int parsed;

		if (strncmp(line, "TID", 3) == 0 || strncmp(line, "-----", 5) == 0 ||
		    strncmp(line, "Total threads:", 14) == 0 ||
		    strncmp(line, "--------------------------------", 32) == 0)
			continue;

		parsed = sscanf(line, "%d %31s %llu.%llu %c %d %d %63s", &tid, name, &cpu_whole,
				&cpu_frac, &state, &priority, &nice_value, affinity);
		if (parsed != 8)
			continue;

		maybe_insert_top_thread(top_threads, top_len, line,
					(cpu_whole * 100ULL) + cpu_frac);
		count++;
	}

	return count;
}

static void print_overview_view(const struct live_snapshot *snapshot)
{
	char rss_line[256];
	char vsz_line[256];
	char swap_line[256];
	char major_faults[256];
	char minor_faults[256];
	char sockets_total[256];
	char rx_bytes[256];
	char tx_bytes[256];
	char retransmits[256];
	char drops[256];
	char read_bytes[256];
	char write_bytes[256];
	char syscr[256];
	char syscw[256];
	char io_intensity[256];
	char io_status[256];
	char total_threads[256];
	struct overview_talker_entry top_talkers[2] = {0};
	struct overview_thread_entry top_threads[3] = {0};
	int top_talker_count;
	int top_thread_count;
	int saw_no_talkers = 0;
	int i;

	memset(rss_line, 0, sizeof(rss_line));
	memset(vsz_line, 0, sizeof(vsz_line));
	memset(swap_line, 0, sizeof(swap_line));
	memset(major_faults, 0, sizeof(major_faults));
	memset(minor_faults, 0, sizeof(minor_faults));
	memset(sockets_total, 0, sizeof(sockets_total));
	memset(rx_bytes, 0, sizeof(rx_bytes));
	memset(tx_bytes, 0, sizeof(tx_bytes));
	memset(retransmits, 0, sizeof(retransmits));
	memset(drops, 0, sizeof(drops));
	memset(read_bytes, 0, sizeof(read_bytes));
	memset(write_bytes, 0, sizeof(write_bytes));
	memset(syscr, 0, sizeof(syscr));
	memset(syscw, 0, sizeof(syscw));
	memset(io_intensity, 0, sizeof(io_intensity));
	memset(io_status, 0, sizeof(io_status));
	memset(total_threads, 0, sizeof(total_threads));

	copy_line_with_prefix(snapshot->det_content, "  RSS (Resident):", rss_line,
			      sizeof(rss_line));
	copy_line_with_prefix(snapshot->det_content, "  VSZ (Virtual):", vsz_line,
			      sizeof(vsz_line));
	copy_line_with_prefix(snapshot->det_content, "  Swap Usage:", swap_line, sizeof(swap_line));
	copy_line_with_prefix(snapshot->det_content, "    - Major:", major_faults,
			      sizeof(major_faults));
	copy_line_with_prefix(snapshot->det_content, "    - Minor:", minor_faults,
			      sizeof(minor_faults));
	copy_line_with_prefix(snapshot->det_content, "sockets_total:", sockets_total,
			      sizeof(sockets_total));
	copy_line_with_prefix(snapshot->det_content, "rx_bytes:", rx_bytes, sizeof(rx_bytes));
	copy_line_with_prefix(snapshot->det_content, "tx_bytes:", tx_bytes, sizeof(tx_bytes));
	copy_line_with_prefix(snapshot->det_content, "tcp_retransmits:", retransmits,
			      sizeof(retransmits));
	copy_line_with_prefix(snapshot->det_content, "drops:", drops, sizeof(drops));
	copy_line_with_prefix(snapshot->det_content, "read_bytes:", read_bytes, sizeof(read_bytes));
	copy_line_with_prefix(snapshot->det_content, "write_bytes:", write_bytes,
			      sizeof(write_bytes));
	copy_line_with_prefix(snapshot->det_content, "syscr:", syscr, sizeof(syscr));
	copy_line_with_prefix(snapshot->det_content, "syscw:", syscw, sizeof(syscw));
	copy_line_with_prefix(snapshot->det_content, "io_intensity:", io_intensity,
			      sizeof(io_intensity));
	copy_line_with_prefix(snapshot->det_content, "status:", io_status, sizeof(io_status));
	copy_line_with_prefix(snapshot->threads_content, "Total threads:", total_threads,
			      sizeof(total_threads));

	trim_newline(rss_line);
	trim_newline(vsz_line);
	trim_newline(swap_line);
	trim_newline(major_faults);
	trim_newline(minor_faults);
	trim_newline(sockets_total);
	trim_newline(rx_bytes);
	trim_newline(tx_bytes);
	trim_newline(retransmits);
	trim_newline(drops);
	trim_newline(read_bytes);
	trim_newline(write_bytes);
	trim_newline(syscr);
	trim_newline(syscw);
	trim_newline(io_intensity);
	trim_newline(io_status);
	trim_newline(total_threads);

	top_talker_count = collect_top_talkers(snapshot->det_content, top_talkers,
					       ARRAY_SIZE(top_talkers), &saw_no_talkers);
	top_thread_count = collect_top_threads(snapshot->threads_content, top_threads,
					       ARRAY_SIZE(top_threads));

	printf("%s%sOVERVIEW%s\n", color_code(C_GREEN), color_code(C_BOLD), color_code(C_RESET));
	puts("---------------------------------------------------------------");

	printf("%s%sMEMORY SNAPSHOT%s\n", color_code(C_BLUE), color_code(C_BOLD),
	       color_code(C_RESET));
	if (rss_line[0] != '\0')
		printf("%s%s%s\n", color_code(C_YELLOW), rss_line, color_code(C_RESET));
	if (vsz_line[0] != '\0')
		printf("%s%s%s\n", color_code(C_YELLOW), vsz_line, color_code(C_RESET));
	if (swap_line[0] != '\0')
		printf("%s%s%s\n", color_code(C_YELLOW), swap_line, color_code(C_RESET));
	if (major_faults[0] != '\0' || minor_faults[0] != '\0') {
		puts("  Page Faults:");
		if (major_faults[0] != '\0')
			printf("%s%s%s\n", color_code(C_CYAN), major_faults, color_code(C_RESET));
		if (minor_faults[0] != '\0')
			printf("%s%s%s\n", color_code(C_CYAN), minor_faults, color_code(C_RESET));
	}
	puts("");

	printf("%s%sNETWORK SNAPSHOT%s\n", color_code(C_GREEN), color_code(C_BOLD),
	       color_code(C_RESET));
	if (sockets_total[0] != '\0')
		printf("%s%s%s\n", color_code(C_YELLOW), sockets_total, color_code(C_RESET));
	if (rx_bytes[0] != '\0')
		printf("%s%s%s\n", color_code(C_YELLOW), rx_bytes, color_code(C_RESET));
	if (tx_bytes[0] != '\0')
		printf("%s%s%s\n", color_code(C_YELLOW), tx_bytes, color_code(C_RESET));
	if (retransmits[0] != '\0')
		printf("%s%s%s\n", color_code(C_YELLOW), retransmits, color_code(C_RESET));
	if (drops[0] != '\0')
		printf("%s%s%s\n", color_code(C_YELLOW), drops, color_code(C_RESET));
	puts("  Top talkers:");
	if (top_talker_count > 0) {
		printf("%s  RANK  FD   PROTO   FAMILY      RX_BYTES   TX_BYTES   TOTAL_BYTES%s\n",
		       color_code(C_CYAN), color_code(C_RESET));
		printf("%s  ----  ---  ------  ----------  ---------  ---------  -----------%s\n",
		       color_code(C_CYAN), color_code(C_RESET));
		for (i = 0; i < top_talker_count && i < (int)ARRAY_SIZE(top_talkers); i++) {
			if (!top_talkers[i].valid)
				continue;
			printf("%s  #%-3d  %-3u  %-6s  %-10s  %-9llu  %-9llu  %-9llu%s\n",
			       color_code(C_MAGENTA), top_talkers[i].rank, top_talkers[i].fd,
			       top_talkers[i].proto, top_talkers[i].family, top_talkers[i].rx_bytes,
			       top_talkers[i].tx_bytes, top_talkers[i].total_bytes,
			       color_code(C_RESET));
		}
	} else if (saw_no_talkers) {
		puts("  none");
	} else {
		puts("  unavailable");
	}
	puts("");

	printf("%s%sI/O SNAPSHOT%s\n", color_code(C_GREEN), color_code(C_BOLD),
	       color_code(C_RESET));
	if (io_status[0] != '\0') {
		printf("%s%s%s\n", color_code(C_CYAN), io_status, color_code(C_RESET));
	} else {
		if (read_bytes[0] != '\0')
			printf("%s%s%s\n", color_code(C_YELLOW), read_bytes, color_code(C_RESET));
		if (write_bytes[0] != '\0')
			printf("%s%s%s\n", color_code(C_YELLOW), write_bytes, color_code(C_RESET));
		if (syscr[0] != '\0')
			printf("%s%s%s\n", color_code(C_YELLOW), syscr, color_code(C_RESET));
		if (syscw[0] != '\0')
			printf("%s%s%s\n", color_code(C_YELLOW), syscw, color_code(C_RESET));
		if (io_intensity[0] != '\0')
			printf("%s%s%s\n", color_code(C_YELLOW), io_intensity, color_code(C_RESET));
	}
	puts("");

	printf("%s%sTHREAD HOTSPOTS%s\n", color_code(C_MAGENTA), color_code(C_BOLD),
	       color_code(C_RESET));
	if (total_threads[0] != '\0')
		printf("%s%s%s\n", color_code(C_YELLOW), total_threads, color_code(C_RESET));
	if (top_thread_count > 0) {
		puts("  Top threads by CPU:");
		printf("%s  TID    NAME             CPU(%%)   STATE  PRIORITY  NICE  CPU_AFFINITY%s\n",
		       color_code(C_CYAN), color_code(C_RESET));
		printf("%s  -----  ---------------  -------  -----  --------  ----  ----------------%s\n",
		       color_code(C_CYAN), color_code(C_RESET));
		for (i = 0; i < (int)ARRAY_SIZE(top_threads); i++) {
			if (!top_threads[i].valid)
				continue;
			printf("%s  %s%s\n", color_code(C_MAGENTA), top_threads[i].line,
			       color_code(C_RESET));
		}
	} else {
		puts("  Top threads by CPU: unavailable");
	}
}

/*
 * Print leading lines of det output (PID, Name, CPU Usage) that appear
 * before any memory section or network section header.
 */
static void print_det_preamble(const char *det_content)
{
	char line[2048];
	const char *cursor = det_content;

	while (*cursor) {
		size_t line_len = 0;

		while (cursor[line_len] && cursor[line_len] != '\n' && line_len < sizeof(line) - 2)
			line_len++;

		memcpy(line, cursor, line_len);
		if (cursor[line_len] == '\n') {
			line[line_len++] = '\n';
			cursor += line_len;
		} else {
			cursor += line_len;
		}
		line[line_len] = '\0';

		if (is_memory_section_start(line) || strncmp(line, "[network]", 9) == 0 ||
		    strncmp(line, "[io]", 4) == 0)
			break;

		printf("%s", line);
	}
}

static void print_memory_view(const char *det_content)
{
	char line[2048];
	const char *cursor = det_content;
	int in_memory_section = 0;

	while (*cursor) {
		size_t line_len = 0;

		while (cursor[line_len] && cursor[line_len] != '\n' && line_len < sizeof(line) - 2)
			line_len++;

		memcpy(line, cursor, line_len);
		if (cursor[line_len] == '\n') {
			line[line_len++] = '\n';
			cursor += line_len;
		} else {
			cursor += line_len;
		}
		line[line_len] = '\0';

		if (strncmp(line, "[network]", 9) == 0 || strncmp(line, "[io]", 4) == 0)
			break;

		if (is_memory_section_start(line))
			in_memory_section = 1;

		if (!in_memory_section)
			continue;

		if (is_memory_section_start(line)) {
			printf("%s%s%s%s", color_code(C_BLUE), color_code(C_BOLD), line,
			       color_code(C_RESET));
		} else if (strncmp(line,
				   "-------------------------------------------"
				   "-----",
				   48) == 0) {
			printf("%s%s%s", color_code(C_BLUE), line, color_code(C_RESET));
		} else if (strncmp(line, "Low:", 4) == 0 || strncmp(line, "High:", 5) == 0) {
			printf("%s%s%s", color_code(C_CYAN), line, color_code(C_RESET));
		} else if (strncmp(line, "  RSS", 5) == 0 || strncmp(line, "  VSZ", 5) == 0 ||
			   strncmp(line, "  Swap", 6) == 0 ||
			   strncmp(line, "  Page Faults", 13) == 0 ||
			   strncmp(line, "  OOM Score", 11) == 0) {
			printf("%s%s%s", color_code(C_YELLOW), line, color_code(C_RESET));
		} else if (strncmp(line, "CODE", 4) == 0 || strncmp(line, "DATA", 4) == 0 ||
			   strncmp(line, "BSS", 3) == 0 || strncmp(line, "HEAP", 4) == 0 ||
			   strncmp(line, "STACK", 5) == 0) {
			printf("%s%s%s", color_code(C_GREEN), line, color_code(C_RESET));
		} else {
			printf("%s", line);
		}
	}
}

static void print_network_view(const char *det_content)
{
	char line[2048];
	const char *cursor = det_content;
	int in_network_section = 0;

	while (*cursor) {
		size_t line_len = 0;

		while (cursor[line_len] && cursor[line_len] != '\n' && line_len < sizeof(line) - 2)
			line_len++;

		memcpy(line, cursor, line_len);
		if (cursor[line_len] == '\n') {
			line[line_len++] = '\n';
			cursor += line_len;
		} else {
			cursor += line_len;
		}
		line[line_len] = '\0';

		if (strncmp(line, "[io]", 4) == 0)
			break;

		if (!in_network_section && strncmp(line, "[network]", 9) == 0)
			in_network_section = 1;

		if (!in_network_section)
			continue;

		if (strncmp(line, "[network]", 9) == 0 || strncmp(line, "Open Sockets:", 13) == 0 ||
		    strncmp(line, "top_talkers:", 12) == 0) {
			printf("%s%s%s%s", color_code(C_GREEN), color_code(C_BOLD), line,
			       color_code(C_RESET));
		} else if (strncmp(line, "sockets_total:", 14) == 0 ||
			   strncmp(line, "rx_packets:", 11) == 0 ||
			   strncmp(line, "tx_packets:", 11) == 0 ||
			   strncmp(line, "rx_bytes:", 9) == 0 ||
			   strncmp(line, "tx_bytes:", 9) == 0 ||
			   strncmp(line, "tcp_retransmits:", 16) == 0 ||
			   strncmp(line, "drops:", 6) == 0 ||
			   strncmp(line, "net_devices:", 12) == 0) {
			printf("%s%s%s", color_code(C_YELLOW), line, color_code(C_RESET));
		} else if (strncmp(line, " [FD", 4) == 0 || strncmp(line, "  #", 3) == 0) {
			printf("%s%s%s", color_code(C_MAGENTA), line, color_code(C_RESET));
		} else if (strncmp(line, "   Traffic:", 11) == 0 ||
			   strncmp(line, "         Local:", 15) == 0 ||
			   strncmp(line, "         Remote:", 16) == 0) {
			printf("%s%s%s", color_code(C_CYAN), line, color_code(C_RESET));
		} else if (strncmp(line, "--------------------------------", 32) == 0) {
			printf("%s%s%s", color_code(C_BLUE), line, color_code(C_RESET));
		} else {
			printf("%s", line);
		}
	}
}

static void print_io_view(const char *det_content)
{
	char line[2048];
	const char *cursor = det_content;
	int in_io_section = 0;

	while (*cursor) {
		size_t line_len = 0;

		while (cursor[line_len] && cursor[line_len] != '\n' && line_len < sizeof(line) - 2)
			line_len++;

		memcpy(line, cursor, line_len);
		if (cursor[line_len] == '\n') {
			line[line_len++] = '\n';
			cursor += line_len;
		} else {
			cursor += line_len;
		}
		line[line_len] = '\0';

		if (!in_io_section && strncmp(line, "[io]", 4) == 0)
			in_io_section = 1;

		if (!in_io_section)
			continue;

		if (strncmp(line, "[io]", 4) == 0) {
			printf("%s%s%s%s", color_code(C_GREEN), color_code(C_BOLD), line,
			       color_code(C_RESET));
		} else if (strncmp(line, "rchar:", 6) == 0 || strncmp(line, "wchar:", 6) == 0 ||
			   strncmp(line, "syscr:", 6) == 0 || strncmp(line, "syscw:", 6) == 0 ||
			   strncmp(line, "read_bytes:", 11) == 0 ||
			   strncmp(line, "write_bytes:", 12) == 0 ||
			   strncmp(line, "cancelled_write_bytes:", 22) == 0 ||
			   strncmp(line, "avg_read_bytes_per_syscall:", 27) == 0 ||
			   strncmp(line, "avg_write_bytes_per_syscall:", 28) == 0 ||
			   strncmp(line, "io_intensity:", 13) == 0) {
			printf("%s%s%s", color_code(C_YELLOW), line, color_code(C_RESET));
		} else if (strncmp(line, "status:", 7) == 0) {
			printf("%s%s%s", color_code(C_CYAN), line, color_code(C_RESET));
		} else {
			printf("%s", line);
		}
	}
}

static int read_proc_file(const char *name, char *buf, size_t buf_size)
{
	FILE *fp;
	char *path;
	size_t bytes;

	path = build_proc_path(name);
	if (!path) {
		fprintf(stderr, "failed to allocate proc path\n");
		return -1;
	}

	fp = fopen(path, "r");
	if (!fp) {
		perror(path);
		free(path);
		return -1;
	}

	bytes = fread(buf, 1, buf_size - 1, fp);
	buf[bytes] = '\0';
	fclose(fp);
	free(path);
	return 0;
}

static int read_proc_file_alloc(const char *name, char **out)
{
	char *buf;

	buf = malloc(PROC_BUF_SIZE);
	if (!buf)
		return -1;

	if (read_proc_file(name, buf, PROC_BUF_SIZE) < 0) {
		free(buf);
		return -1;
	}

	*out = buf;
	return 0;
}

static int read_line(char *buf, size_t buf_size)
{
	size_t len;

	if (!fgets(buf, buf_size, stdin))
		return -1;

	len = strlen(buf);
	if (len > 0 && buf[len - 1] == '\n')
		buf[len - 1] = '\0';

	return 0;
}

static int wait_for_input_or_timeout(int timeout_sec)
{
	fd_set readfds;
	struct timeval timeout;
	int ret;

	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);

	timeout.tv_sec = timeout_sec;
	timeout.tv_usec = 0;

	ret = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
	if (ret < 0)
		return -1;

	return ret;
}

static void format_current_time(char *buf, size_t buf_size)
{
	time_t now;
	struct tm tm_now;

	now = time(NULL);
	if (!localtime_r(&now, &tm_now)) {
		snprintf(buf, buf_size, "00/00/00 00:00:00");
		return;
	}

	strftime(buf, buf_size, "%y/%m/%d %H:%M:%S", &tm_now);
}

static const char *view_name(int view)
{
	if (view == VIEW_MEMORY)
		return "Memory";

	if (view == VIEW_NETWORK)
		return "Network";

	if (view == VIEW_THREADS)
		return "Threads";

	if (view == VIEW_IO)
		return "I/O";

	if (view == VIEW_OVERVIEW)
		return "Overview";

	return "Unknown";
}

static void
print_live_header(const struct live_snapshot *snap, int browse_offset, int history_count)
{
	printf("\033[H\033[2J");
	fflush(stdout);
	printf("%s%s==========================================================="
	       "====\n",
	       color_code(C_GREEN), color_code(C_BOLD));
	printf("PROC LENS - LIVE VIEW (refresh: 1s)\n");
	printf("Snapshot start: %s\n", snap->captured_at);
	printf("==============================================================="
	       "%s\n",
	       color_code(C_RESET));
	printf("%sPID:%s %s\n", color_code(C_YELLOW), color_code(C_RESET), snap->pid);
	printf("%sCurrent section:%s %s\n", color_code(C_YELLOW), color_code(C_RESET),
	       view_name(snap->view));
	printf("%sSnapshot index:%s %d/%d\n", color_code(C_YELLOW), color_code(C_RESET),
	       history_count - browse_offset, history_count);
	puts("Sections: [1] Memory  [2] Network  [3] Threads  [4] I/O  [5] Overview");
	puts("History: [Up/k] older  [Down/j] newer  [f] follow live");
	puts("Commands: 1/2/3/4/5 switch view, 0 change PID, Ctrl+C exit");
	if (browse_offset > 0)
		puts("Mode: browsing history (auto-refresh paused)");
	else
		puts("Mode: live follow");
	puts("---------------------------------------------------------------");
}

static void print_live_footer(const char *captured_at)
{
	puts("---------------------------------------------------------------");
	printf("Snapshot end:   %s\n", captured_at);
	printf("%s============================================================="
	       "=="
	       "%s\n",
	       color_code(C_GREEN), color_code(C_RESET));
}

static void free_snapshot(struct live_snapshot *snap)
{
	free(snap->det_content);
	free(snap->threads_content);
	snap->det_content = NULL;
	snap->threads_content = NULL;
	snap->pid[0] = '\0';
	snap->captured_at[0] = '\0';
	snap->view = VIEW_OVERVIEW;
}

static void
clear_snapshot_history(struct live_snapshot *history, int *history_count, int *history_next)
{
	int i;

	for (i = 0; i < MAX_SNAPSHOTS; i++)
		free_snapshot(&history[i]);

	*history_count = 0;
	*history_next = 0;
}

static void append_snapshot(struct live_snapshot *history,
			    int *history_count,
			    int *history_next,
			    const struct live_snapshot *snapshot)
{
	struct live_snapshot *dst;

	dst = &history[*history_next];
	free_snapshot(dst);
	memcpy(dst, snapshot, sizeof(*dst));

	*history_next = (*history_next + 1) % MAX_SNAPSHOTS;
	if (*history_count < MAX_SNAPSHOTS)
		(*history_count)++;
}

static struct live_snapshot *get_snapshot_by_offset(struct live_snapshot *history,
						    int history_count,
						    int history_next,
						    int browse_offset)
{
	int newest_index;
	int target_index;

	if (history_count <= 0)
		return NULL;

	if (browse_offset < 0 || browse_offset >= history_count)
		return NULL;

	newest_index = (history_next + MAX_SNAPSHOTS - 1) % MAX_SNAPSHOTS;
	target_index = (newest_index + MAX_SNAPSHOTS - browse_offset) % MAX_SNAPSHOTS;
	return &history[target_index];
}

static int capture_live_snapshot(const char *pid_str, int view, struct live_snapshot *snapshot)
{
	size_t len;

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->view = view;
	format_current_time(snapshot->captured_at, sizeof(snapshot->captured_at));

	len = strlen(pid_str);
	if (len >= sizeof(snapshot->pid))
		len = sizeof(snapshot->pid) - 1;
	memcpy(snapshot->pid, pid_str, len);
	snapshot->pid[len] = '\0';

	if (write_pid(pid_str) < 0)
		return -1;

	if (read_proc_file_alloc("det", &snapshot->det_content) < 0)
		goto fail;

	if ((view == VIEW_THREADS || view == VIEW_OVERVIEW) &&
	    read_proc_file_alloc("threads", &snapshot->threads_content) < 0)
		goto fail;

	return 0;

fail:
	free_snapshot(snapshot);
	return -1;
}

static void print_live_snapshot(const struct live_snapshot *snapshot)
{
	if (!snapshot->det_content)
		return;

	printf("%s%s==========================================================="
	       "====\n",
	       color_code(C_CYAN), color_code(C_BOLD));
	printf("PROCESS INFORMATION\n");
	printf("==============================================================="
	       "%s\n",
	       color_code(C_RESET));
	print_cmdline(snapshot->pid);
	print_det_preamble(snapshot->det_content);

	if (snapshot->view == VIEW_OVERVIEW) {
		print_overview_view(snapshot);
		print_live_footer(snapshot->captured_at);
		return;
	}

	if (snapshot->view == VIEW_MEMORY) {
		print_memory_view(snapshot->det_content);
		print_live_footer(snapshot->captured_at);
		return;
	}

	if (snapshot->view == VIEW_NETWORK) {
		print_network_view(snapshot->det_content);
		print_live_footer(snapshot->captured_at);
		return;
	}

	if (snapshot->view == VIEW_IO) {
		print_io_view(snapshot->det_content);
		print_live_footer(snapshot->captured_at);
		return;
	}

	printf("%s%s==========================================================="
	       "====\n",
	       color_code(C_MAGENTA), color_code(C_BOLD));
	printf("THREAD INFORMATION\n");
	printf("==============================================================="
	       "%s\n",
	       color_code(C_RESET));
	if (snapshot->threads_content)
		printf("%s", snapshot->threads_content);
	print_live_footer(snapshot->captured_at);
}

static int read_live_key(void)
{
	char ch;
	char seq0;
	char seq1;
	ssize_t n;

	n = read(STDIN_FILENO, &ch, 1);
	if (n <= 0)
		return -1;

	if (ch != '\033')
		return (unsigned char)ch;

	n = read(STDIN_FILENO, &seq0, 1);
	if (n <= 0)
		return (unsigned char)ch;
	n = read(STDIN_FILENO, &seq1, 1);
	if (n <= 0)
		return (unsigned char)ch;

	if (seq0 == '[' && seq1 == 'A')
		return 'k';

	if (seq0 == '[' && seq1 == 'B')
		return 'j';

	return (unsigned char)ch;
}

static int prompt_for_pid(char *pid_user, size_t pid_user_size)
{
	printf(">> Enter process ID: ");
	fflush(stdout);
	if (read_line(pid_user, pid_user_size) < 0)
		return -1;

	if (pid_user[0] == '\0')
		return -1;

	return 0;
}

static void run_live_mode(void)
{
	struct live_snapshot history[MAX_SNAPSHOTS];
	struct live_snapshot snapshot;
	char pid_user[PID_INPUT_MAX];
	int key;
	int view = VIEW_OVERVIEW;
	int browse_offset = 0;
	int history_count = 0;
	int history_next = 0;

	memset(history, 0, sizeof(history));

	if (prompt_for_pid(pid_user, sizeof(pid_user)) < 0) {
		fprintf(stderr, "invalid input\n");
		return;
	}

	if (set_raw_mode() < 0) {
		fprintf(stderr, "failed to set terminal raw mode\n");
		return;
	}

	while (1) {
		const struct live_snapshot *current;

		if (browse_offset == 0) {
			if (capture_live_snapshot(pid_user, view, &snapshot) == 0)
				append_snapshot(history, &history_count, &history_next, &snapshot);
		}

		current =
			get_snapshot_by_offset(history, history_count, history_next, browse_offset);
		if (current) {
			print_live_header(current, browse_offset, history_count);
			print_live_snapshot(current);
		}

		fflush(stdout);
		if (wait_for_input_or_timeout(1) <= 0)
			continue;

		key = read_live_key();
		if (key < 0)
			break;

		if (key == '1') {
			view = VIEW_MEMORY;
			browse_offset = 0;
			clear_snapshot_history(history, &history_count, &history_next);
		} else if (key == '2') {
			view = VIEW_NETWORK;
			browse_offset = 0;
			clear_snapshot_history(history, &history_count, &history_next);
		} else if (key == '3') {
			view = VIEW_THREADS;
			browse_offset = 0;
			clear_snapshot_history(history, &history_count, &history_next);
		} else if (key == '4') {
			view = VIEW_IO;
			browse_offset = 0;
			clear_snapshot_history(history, &history_count, &history_next);
		} else if (key == '5') {
			view = VIEW_OVERVIEW;
			browse_offset = 0;
			clear_snapshot_history(history, &history_count, &history_next);
		} else if (key == 'k') {
			if (browse_offset + 1 < history_count)
				browse_offset++;
		} else if (key == 'j') {
			if (browse_offset > 0)
				browse_offset--;
		} else if (key == 'f') {
			browse_offset = 0;
		} else if (key == '0') {
			/* temporarily restore cooked mode for PID entry */
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios);
			if (prompt_for_pid(pid_user, sizeof(pid_user)) < 0)
				fprintf(stderr, "invalid PID\n");
			apply_raw_mode();
			browse_offset = 0;
			clear_snapshot_history(history, &history_count, &history_next);
		}
	}

	clear_snapshot_history(history, &history_count, &history_next);
	restore_terminal();
}

static void print_usage(void)
{
	printf("Usage: proclens [OPTIONS] [PID]\n");
	printf("\n");
	printf("Options:\n");
	printf("  -h, --help       Show this help message and exit\n");
	printf("  -v, --version    Show version and exit\n");
	printf("\n");
	printf("Arguments:\n");
	printf("  PID              Show one-shot process info for the given "
	       "PID\n");
	printf("\n");
	printf("Live mode (no arguments):\n");
	printf("  Auto-refreshes every 1s. Controls shown in-app:\n");
	printf("    1  Memory section\n");
	printf("    2  Network section\n");
	printf("    3  Threads section\n");
	printf("    4  I/O section\n");
	printf("    5  Overview section\n");
	printf("    0  Change PID\n");
	printf("    Up/k  Older snapshot   Down/j  Newer snapshot\n");
	printf("    f     Resume live follow\n");
	printf("    Ctrl+C  Exit\n");
}

int main(int argc, char **argv)
{
	init_color_output();

	if (argc > 1 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
		print_usage();
		return 0;
	}

	if (argc > 1 && (strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0)) {
		printf("proclens %s\n", PROCLENS_VERSION);
		return 0;
	}

	if (ensure_root_privileges() < 0)
		return -1;

	print_logo_text();
	if (ensure_module_loaded() < 0)
		return -1;
	if (ensure_proc_files_present() < 0)
		return -1;

	if (argc > 1) {
		char pid_user[20];
		size_t len;

		/* Safe string copy with explicit bounds checking */
		len = strlen(argv[1]);
		if (len >= sizeof(pid_user))
			len = sizeof(pid_user) - 1;
		memcpy(pid_user, argv[1], len);
		pid_user[len] = '\0';

		print_process_info(pid_user);
		return 0;
	}

	run_live_mode();
	return 0;
}
