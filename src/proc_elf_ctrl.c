// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include "proc_elf_ctrl.h"

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

	g_use_color =
		isatty(STDOUT_FILENO) && (!no_color || no_color[0] == '\0');
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

	loaded = is_module_loaded("elf_det");
	if (loaded < 0) {
		perror("open /proc/modules");
		return -1;
	}

	if (loaded == 0) {
		fprintf(stderr,
			"%serror:%s kernel module 'elf_det' is not loaded\n",
			color_code(C_YELLOW), color_code(C_RESET));
		fprintf(stderr, "hint: run 'sudo insmod ./build/elf_det.ko' or "
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
				color_code(C_YELLOW), color_code(C_RESET),
				path);
			fprintf(stderr, "hint: confirm /proc/elf_det is "
					"mounted and initialized\n");
			free(path);
			return -1;
		}

		free(path);
	}

	return 0;
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

	printf("%sCommand line:%s   %s\n", color_code(C_YELLOW),
	       color_code(C_RESET), cmdline);
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

		while (cursor[line_len] && cursor[line_len] != '\n' &&
		       line_len < sizeof(line) - 2)
			line_len++;

		memcpy(line, cursor, line_len);
		if (cursor[line_len] == '\n') {
			line[line_len++] = '\n';
			cursor += line_len;
		} else {
			cursor += line_len;
		}
		line[line_len] = '\0';

		if (is_memory_section_start(line) ||
		    strncmp(line, "[network]", 9) == 0 ||
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

		while (cursor[line_len] && cursor[line_len] != '\n' &&
		       line_len < sizeof(line) - 2)
			line_len++;

		memcpy(line, cursor, line_len);
		if (cursor[line_len] == '\n') {
			line[line_len++] = '\n';
			cursor += line_len;
		} else {
			cursor += line_len;
		}
		line[line_len] = '\0';

		if (strncmp(line, "[network]", 9) == 0 ||
		    strncmp(line, "[io]", 4) == 0)
			break;

		if (is_memory_section_start(line))
			in_memory_section = 1;

		if (!in_memory_section)
			continue;

		if (is_memory_section_start(line)) {
			printf("%s%s%s%s", color_code(C_BLUE),
			       color_code(C_BOLD), line, color_code(C_RESET));
		} else if (strncmp(line,
				   "-------------------------------------------"
				   "-----",
				   48) == 0) {
			printf("%s%s%s", color_code(C_BLUE), line,
			       color_code(C_RESET));
		} else if (strncmp(line, "Low:", 4) == 0 ||
			   strncmp(line, "High:", 5) == 0) {
			printf("%s%s%s", color_code(C_CYAN), line,
			       color_code(C_RESET));
		} else if (strncmp(line, "  RSS", 5) == 0 ||
			   strncmp(line, "  VSZ", 5) == 0 ||
			   strncmp(line, "  Swap", 6) == 0 ||
			   strncmp(line, "  Page Faults", 13) == 0 ||
			   strncmp(line, "  OOM Score", 11) == 0) {
			printf("%s%s%s", color_code(C_YELLOW), line,
			       color_code(C_RESET));
		} else if (strncmp(line, "CODE", 4) == 0 ||
			   strncmp(line, "DATA", 4) == 0 ||
			   strncmp(line, "BSS", 3) == 0 ||
			   strncmp(line, "HEAP", 4) == 0 ||
			   strncmp(line, "STACK", 5) == 0) {
			printf("%s%s%s", color_code(C_GREEN), line,
			       color_code(C_RESET));
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

		while (cursor[line_len] && cursor[line_len] != '\n' &&
		       line_len < sizeof(line) - 2)
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

		if (strncmp(line, "[network]", 9) == 0 ||
		    strncmp(line, "Open Sockets:", 13) == 0 ||
		    strncmp(line, "top_talkers:", 12) == 0) {
			printf("%s%s%s%s", color_code(C_GREEN),
			       color_code(C_BOLD), line, color_code(C_RESET));
		} else if (strncmp(line, "sockets_total:", 14) == 0 ||
			   strncmp(line, "rx_packets:", 11) == 0 ||
			   strncmp(line, "tx_packets:", 11) == 0 ||
			   strncmp(line, "rx_bytes:", 9) == 0 ||
			   strncmp(line, "tx_bytes:", 9) == 0 ||
			   strncmp(line, "tcp_retransmits:", 16) == 0 ||
			   strncmp(line, "drops:", 6) == 0 ||
			   strncmp(line, "net_devices:", 12) == 0) {
			printf("%s%s%s", color_code(C_YELLOW), line,
			       color_code(C_RESET));
		} else if (strncmp(line, " [FD", 4) == 0 ||
			   strncmp(line, "  #", 3) == 0) {
			printf("%s%s%s", color_code(C_MAGENTA), line,
			       color_code(C_RESET));
		} else if (strncmp(line, "   Traffic:", 11) == 0 ||
			   strncmp(line, "         Local:", 15) == 0 ||
			   strncmp(line, "         Remote:", 16) == 0) {
			printf("%s%s%s", color_code(C_CYAN), line,
			       color_code(C_RESET));
		} else if (strncmp(line, "--------------------------------",
				   32) == 0) {
			printf("%s%s%s", color_code(C_BLUE), line,
			       color_code(C_RESET));
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

		while (cursor[line_len] && cursor[line_len] != '\n' &&
		       line_len < sizeof(line) - 2)
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
			printf("%s%s%s%s", color_code(C_GREEN),
			       color_code(C_BOLD), line, color_code(C_RESET));
		} else if (strncmp(line, "rchar:", 6) == 0 ||
			   strncmp(line, "wchar:", 6) == 0 ||
			   strncmp(line, "syscr:", 6) == 0 ||
			   strncmp(line, "syscw:", 6) == 0 ||
			   strncmp(line, "read_bytes:", 11) == 0 ||
			   strncmp(line, "write_bytes:", 12) == 0 ||
			   strncmp(line, "cancelled_write_bytes:", 22) == 0 ||
			   strncmp(line, "avg_read_bytes_per_syscall:", 27) ==
				   0 ||
			   strncmp(line, "avg_write_bytes_per_syscall:", 28) ==
				   0 ||
			   strncmp(line, "io_intensity:", 13) == 0) {
			printf("%s%s%s", color_code(C_YELLOW), line,
			       color_code(C_RESET));
		} else if (strncmp(line, "status:", 7) == 0) {
			printf("%s%s%s", color_code(C_CYAN), line,
			       color_code(C_RESET));
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

	return "Unknown";
}

static void print_live_header(const struct live_snapshot *snap,
			      int browse_offset,
			      int history_count)
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
	printf("%sPID:%s %s\n", color_code(C_YELLOW), color_code(C_RESET),
	       snap->pid);
	printf("%sCurrent section:%s %s\n", color_code(C_YELLOW),
	       color_code(C_RESET), view_name(snap->view));
	printf("%sSnapshot index:%s %d/%d\n", color_code(C_YELLOW),
	       color_code(C_RESET), history_count - browse_offset,
	       history_count);
	puts("Sections: [1] Memory  [2] Network  [3] Threads  [4] I/O");
	puts("History: [Up/k] older  [Down/j] newer  [f] follow live");
	puts("Commands: 1/2/3/4 switch view, 0 change PID, Ctrl+C exit");
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
	snap->view = VIEW_MEMORY;
}

static void clear_snapshot_history(struct live_snapshot *history,
				   int *history_count,
				   int *history_next)
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

static struct live_snapshot *
get_snapshot_by_offset(struct live_snapshot *history,
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
	target_index =
		(newest_index + MAX_SNAPSHOTS - browse_offset) % MAX_SNAPSHOTS;
	return &history[target_index];
}

static int capture_live_snapshot(const char *pid_str,
				 int view,
				 struct live_snapshot *snapshot)
{
	size_t len;

	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->view = view;
	format_current_time(snapshot->captured_at,
			    sizeof(snapshot->captured_at));

	len = strlen(pid_str);
	if (len >= sizeof(snapshot->pid))
		len = sizeof(snapshot->pid) - 1;
	memcpy(snapshot->pid, pid_str, len);
	snapshot->pid[len] = '\0';

	if (write_pid(pid_str) < 0)
		return -1;

	if (read_proc_file_alloc("det", &snapshot->det_content) < 0)
		goto fail;

	if (view == VIEW_THREADS &&
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
	int view = VIEW_MEMORY;
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
			if (capture_live_snapshot(pid_user, view, &snapshot) ==
			    0)
				append_snapshot(history, &history_count,
						&history_next, &snapshot);
		}

		current = get_snapshot_by_offset(history, history_count,
						 history_next, browse_offset);
		if (current) {
			print_live_header(current, browse_offset,
					  history_count);
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
			clear_snapshot_history(history, &history_count,
					       &history_next);
		} else if (key == '2') {
			view = VIEW_NETWORK;
			browse_offset = 0;
			clear_snapshot_history(history, &history_count,
					       &history_next);
		} else if (key == '3') {
			view = VIEW_THREADS;
			browse_offset = 0;
			clear_snapshot_history(history, &history_count,
					       &history_next);
		} else if (key == '4') {
			view = VIEW_IO;
			browse_offset = 0;
			clear_snapshot_history(history, &history_count,
					       &history_next);
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
			clear_snapshot_history(history, &history_count,
					       &history_next);
		}
	}

	clear_snapshot_history(history, &history_count, &history_next);
	restore_terminal();
}

int main(int argc, char **argv)
{
	init_color_output();
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
