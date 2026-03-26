// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/select.h>
#include <termios.h>
#include "proc_elf_ctrl.h"

static volatile sig_atomic_t g_restore_terminal;
static struct termios g_saved_termios;

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

#define PID_INPUT_MAX 20
#define PROC_BUF_SIZE 262144

enum view_mode {
	VIEW_MEMORY = 1,
	VIEW_NETWORK = 2,
	VIEW_THREADS = 3,
};

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

	printf("Command line:   %s\n", cmdline);
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
	puts("===============================================================");
	puts("PROCESS INFORMATION");
	puts("===============================================================");
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
	puts("===============================================================");
	puts("THREAD INFORMATION");
	puts("===============================================================");
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
		    strncmp(line, "[network]", 9) == 0)
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

		if (strncmp(line, "[network]", 9) == 0)
			break;

		if (is_memory_section_start(line))
			in_memory_section = 1;

		if (in_memory_section)
			printf("%s", line);
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

		if (!in_network_section && strncmp(line, "[network]", 9) == 0)
			in_network_section = 1;

		if (in_network_section)
			printf("%s", line);
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

static void print_live_header(const char *pid_str, int view)
{
	printf("\033[H\033[2J");
	fflush(stdout);
	puts("===============================================================");
	puts("PROC LENS - LIVE VIEW (refresh: 1s)");
	puts("===============================================================");
	printf("PID: %s\n", pid_str);
	printf("Current section: %d\n", view);
	puts("Sections: [1] Memory  [2] Network  [3] Threads");
	puts("Commands: press 1/2/3 to switch view, 0 to change PID, Ctrl+C to "
	     "exit");
	puts("---------------------------------------------------------------");
}

static void print_live_view(const char *pid_str, int view)
{
	char proc_buf[PROC_BUF_SIZE];

	if (write_pid(pid_str) < 0)
		return;

	if (read_proc_file("det", proc_buf, sizeof(proc_buf)) < 0)
		return;

	/* Always show the process header block for every view */
	puts("===============================================================");
	puts("PROCESS INFORMATION");
	puts("===============================================================");
	print_cmdline(pid_str);
	print_det_preamble(proc_buf);

	if (view == VIEW_MEMORY) {
		print_memory_view(proc_buf);
		return;
	}

	if (view == VIEW_NETWORK) {
		print_network_view(proc_buf);
		return;
	}

	/* VIEW_THREADS: reuse buffer to avoid a second large stack frame */
	if (read_proc_file("threads", proc_buf, sizeof(proc_buf)) < 0)
		return;
	puts("===============================================================");
	puts("THREAD INFORMATION");
	puts("===============================================================");
	printf("%s", proc_buf);
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
	char pid_user[PID_INPUT_MAX];
	char ch;
	ssize_t n;
	int view = VIEW_MEMORY;

	if (prompt_for_pid(pid_user, sizeof(pid_user)) < 0) {
		fprintf(stderr, "invalid input\n");
		return;
	}

	if (set_raw_mode() < 0) {
		fprintf(stderr, "failed to set terminal raw mode\n");
		return;
	}

	while (1) {
		print_live_header(pid_user, view);
		print_live_view(pid_user, view);

		fflush(stdout);
		if (wait_for_input_or_timeout(1) <= 0)
			continue;

		n = read(STDIN_FILENO, &ch, 1);
		if (n <= 0)
			break;

		if (ch == '1')
			view = VIEW_MEMORY;
		else if (ch == '2')
			view = VIEW_NETWORK;
		else if (ch == '3')
			view = VIEW_THREADS;
		else if (ch == '0') {
			/* temporarily restore cooked mode for PID entry */
			tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_saved_termios);
			if (prompt_for_pid(pid_user, sizeof(pid_user)) < 0)
				fprintf(stderr, "invalid PID\n");
			apply_raw_mode();
		}
	}

	restore_terminal();
}

int main(int argc, char **argv)
{
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
