// SPDX-License-Identifier: MIT
#define _GNU_SOURCE
#include "proc_elf_ctrl.h"
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char output_buf[65536];
static size_t output_len;

static char *pid_stream_buf;
static size_t pid_stream_len;

static const char *det_content = "det-line-1\ndet-line-2\n";
static const char *threads_content = "thread-line-1\n";
static char cmdline_content[256];
static size_t cmdline_len;

static int fail_pid_open;
static int fail_det_open;
static int fail_threads_open;
static int fail_cmdline_open;
static int module_loaded;
static int proc_pid_present;
static int proc_det_present;
static int proc_threads_present;
static uid_t mock_euid;

static void append_output(const char *fmt, ...)
{
	va_list args;
	int n;

	if (output_len >= sizeof(output_buf) - 1)
		return;

	va_start(args, fmt);
	n = vsnprintf(output_buf + output_len, sizeof(output_buf) - output_len,
		      fmt, args);
	va_end(args);

	if (n <= 0)
		return;

	if ((size_t)n >= (sizeof(output_buf) - output_len)) {
		output_len = sizeof(output_buf) - 1;
		output_buf[output_len] = '\0';
		return;
	}

	output_len += (size_t)n;
}

static void reset_mocks(void)
{
	output_buf[0] = '\0';
	output_len = 0;

	if (pid_stream_buf) {
		free(pid_stream_buf);
		pid_stream_buf = NULL;
	}
	pid_stream_len = 0;

	fail_pid_open = 0;
	fail_det_open = 0;
	fail_threads_open = 0;
	fail_cmdline_open = 0;
	module_loaded = 1;
	proc_pid_present = 1;
	proc_det_present = 1;
	proc_threads_present = 1;
	mock_euid = 0;

	memset(cmdline_content, 0, sizeof(cmdline_content));
	cmdline_len = 0;
	setenv("ELF_DET_PROC_DIR", "/fake_proc", 1);
}

static FILE *mock_fopen(const char *path, const char *mode)
{
	if (!strcmp(path, "/fake_proc/pid") && strchr(mode, 'w')) {
		if (fail_pid_open)
			return NULL;
		if (pid_stream_buf) {
			free(pid_stream_buf);
			pid_stream_buf = NULL;
		}
		pid_stream_len = 0;
		return open_memstream(&pid_stream_buf, &pid_stream_len);
	}

	if (!strcmp(path, "/fake_proc/det") && strchr(mode, 'r')) {
		if (fail_det_open)
			return NULL;
		return fmemopen((void *)det_content, strlen(det_content), "r");
	}

	if (!strcmp(path, "/fake_proc/threads") && strchr(mode, 'r')) {
		if (fail_threads_open)
			return NULL;
		return fmemopen((void *)threads_content,
				strlen(threads_content), "r");
	}

	if (strstr(path, "/proc/") == path && strstr(path, "/cmdline")) {
		if (fail_cmdline_open)
			return NULL;
		return fmemopen((void *)cmdline_content, cmdline_len, "r");
	}

	if (!strcmp(path, "/proc/modules") && strchr(mode, 'r')) {
		if (module_loaded)
			return fmemopen((void *)"elf_det 0 0 - Live 0x0\n", 23,
					"r");

		return fmemopen((void *)"", 0, "r");
	}

	return NULL;
}

static int mock_access(const char *path, int mode)
{
	(void)mode;

	if (!strcmp(path, "/fake_proc/pid"))
		return proc_pid_present ? 0 : -1;

	if (!strcmp(path, "/fake_proc/det"))
		return proc_det_present ? 0 : -1;

	if (!strcmp(path, "/fake_proc/threads"))
		return proc_threads_present ? 0 : -1;

	return -1;
}

static int mock_printf(const char *fmt, ...)
{
	va_list args;
	int n;
	char tmp[2048];

	va_start(args, fmt);
	n = vsnprintf(tmp, sizeof(tmp), fmt, args);
	va_end(args);
	if (n > 0)
		append_output("%s", tmp);
	return n;
}

static int mock_puts(const char *s)
{
	append_output("%s\n", s);
	return 0;
}

static void mock_perror(const char *s)
{
	append_output("%s\n", s);
}

static uid_t mock_geteuid(void)
{
	return mock_euid;
}

#define fopen	mock_fopen
#define access	mock_access
#define geteuid mock_geteuid
#define printf	mock_printf
#define puts	mock_puts
#define perror	mock_perror
#define main	proc_elf_ctrl_entry
#include "proc_elf_ctrl.c"
#undef main
#undef perror
#undef puts
#undef printf
#undef geteuid
#undef access
#undef fopen

static void test_build_proc_path_helper(void)
{
	char *p1;
	char *p2;

	unsetenv("ELF_DET_PROC_DIR");
	p1 = build_proc_path("pid");
	assert(p1 && strcmp(p1, "/proc/elf_det/pid") == 0);
	free(p1);

	setenv("ELF_DET_PROC_DIR", "/tmp/fakeproc", 1);
	p2 = build_proc_path("det");
	assert(p2 && strcmp(p2, "/tmp/fakeproc/det") == 0);
	free(p2);
}

static void test_print_cmdline_replaces_nul_with_space(void)
{
	reset_mocks();
	cmdline_content[0] = '/';
	cmdline_content[1] = 's';
	cmdline_content[2] = 'b';
	cmdline_content[3] = 'i';
	cmdline_content[4] = 'n';
	cmdline_content[5] = '/';
	cmdline_content[6] = 'i';
	cmdline_content[7] = 'n';
	cmdline_content[8] = 'i';
	cmdline_content[9] = 't';
	cmdline_content[10] = '\0';
	cmdline_content[11] = 's';
	cmdline_content[12] = 'p';
	cmdline_content[13] = 'l';
	cmdline_content[14] = 'a';
	cmdline_content[15] = 's';
	cmdline_content[16] = 'h';
	cmdline_content[17] = '\0';
	cmdline_len = 18;

	print_cmdline("1");
	assert(strstr(output_buf, "Command line:   /sbin/init splash "));
}

static void test_print_process_info_happy_path(void)
{
	reset_mocks();
	cmdline_content[0] = 'i';
	cmdline_content[1] = 'n';
	cmdline_content[2] = 'i';
	cmdline_content[3] = 't';
	cmdline_content[4] = '\0';
	cmdline_len = 5;

	print_process_info("1234");

	assert(pid_stream_buf);
	assert(strcmp(pid_stream_buf, "1234") == 0);
	assert(strstr(output_buf, "PROCESS INFORMATION"));
	assert(strstr(output_buf, "THREAD INFORMATION"));
	assert(strstr(output_buf, "det-line-1"));
	assert(strstr(output_buf, "thread-line-1"));
}

static void test_main_argument_pid_is_bounded(void)
{
	static const char *const argv[] = {
		"prog",
		"123456789012345678901234567890",
	};
	int rc;

	reset_mocks();
	cmdline_len = 0;
	rc = proc_elf_ctrl_entry(2, (char **)argv);

	assert(rc == 0);
	assert(pid_stream_buf);
	assert(pid_stream_len == 19);
	assert(strncmp(pid_stream_buf, "1234567890123456789", 19) == 0);
}

static void test_main_exits_when_module_not_loaded(void)
{
	static const char *const argv[] = {"prog", "1"};
	int rc;

	reset_mocks();
	module_loaded = 0;
	rc = proc_elf_ctrl_entry(2, (char **)argv);

	assert(rc == -1);
	assert(!pid_stream_buf);
}

static void test_main_exits_when_proc_file_missing(void)
{
	static const char *const argv[] = {"prog", "1"};
	int rc;

	reset_mocks();
	proc_threads_present = 0;
	rc = proc_elf_ctrl_entry(2, (char **)argv);

	assert(rc == -1);
	assert(!pid_stream_buf);
}

static void test_main_exits_without_root_privileges(void)
{
	static const char *const argv[] = {"prog", "1"};
	int rc;

	reset_mocks();
	mock_euid = 1000;
	rc = proc_elf_ctrl_entry(2, (char **)argv);

	assert(rc == -1);
	assert(!pid_stream_buf);
	assert(!strstr(output_buf, "PROCESS INFORMATION"));
}

static void test_det_preamble_stops_before_sections(void)
{
	const char *det = "Process ID:     123\n"
			  "Name:            foo\n"
			  "CPU Usage:       1.00%\n"
			  "Memory Pressure Statistics:\n"
			  "  RSS (Resident): 10 KB\n"
			  "[io]\n"
			  "rchar: 1\n"
			  "[network]\n"
			  "sockets_total: 1\n";

	reset_mocks();
	print_det_preamble(det);

	assert(strstr(output_buf, "Process ID:     123"));
	assert(strstr(output_buf, "Name:            foo"));
	assert(strstr(output_buf, "CPU Usage:       1.00%"));
	assert(!strstr(output_buf, "Memory Pressure Statistics:"));
	assert(!strstr(output_buf, "[io]"));
	assert(!strstr(output_buf, "[network]"));
}

static void test_memory_view_filters_network_section(void)
{
	const char *det = "Memory Pressure Statistics:\n"
			  "  RSS (Resident): 10 KB\n"
			  "Memory Layout:\n"
			  "  Code Section: 0x1 - 0x2\n"
			  "Memory Layout Visualization:\n"
			  "  [== ]\n"
			  "[network]\n"
			  "sockets_total: 2\n"
			  "[io]\n"
			  "rchar: 1\n";

	reset_mocks();
	print_memory_view(det);

	assert(strstr(output_buf, "Memory Pressure Statistics:"));
	assert(strstr(output_buf, "Memory Layout Visualization:"));
	assert(!strstr(output_buf, "[network]"));
	assert(!strstr(output_buf, "sockets_total"));
	assert(!strstr(output_buf, "[io]"));
}

static void test_network_view_starts_from_network_section(void)
{
	const char *det = "Memory Pressure Statistics:\n"
			  "  RSS (Resident): 10 KB\n"
			  "[network]\n"
			  "sockets_total: 2\n"
			  "Open Sockets:\n"
			  "[io]\n"
			  "rchar: 123\n";

	reset_mocks();
	print_network_view(det);

	assert(!strstr(output_buf, "Memory Pressure Statistics:"));
	assert(strstr(output_buf, "[network]"));
	assert(strstr(output_buf, "sockets_total: 2"));
	assert(strstr(output_buf, "Open Sockets:"));
	assert(!strstr(output_buf, "[io]"));
	assert(!strstr(output_buf, "rchar: 123"));
}

static void test_io_view_starts_from_io_section(void)
{
	const char *det = "Memory Pressure Statistics:\n"
			  "  RSS (Resident): 10 KB\n"
			  "[network]\n"
			  "sockets_total: 2\n"
			  "[io]\n"
			  "rchar: 123\n"
			  "io_intensity: 456\n";

	reset_mocks();
	print_io_view(det);

	assert(!strstr(output_buf, "Memory Pressure Statistics:"));
	assert(!strstr(output_buf, "[network]"));
	assert(strstr(output_buf, "[io]"));
	assert(strstr(output_buf, "rchar: 123"));
	assert(strstr(output_buf, "io_intensity: 456"));
}

static void test_format_current_time_layout(void)
{
	char buf[32];

	format_current_time(buf, sizeof(buf));

	assert(strlen(buf) == 17);
	assert(isdigit((unsigned char)buf[0]));
	assert(isdigit((unsigned char)buf[1]));
	assert(buf[2] == '/');
	assert(isdigit((unsigned char)buf[3]));
	assert(isdigit((unsigned char)buf[4]));
	assert(buf[5] == '/');
	assert(isdigit((unsigned char)buf[6]));
	assert(isdigit((unsigned char)buf[7]));
	assert(buf[8] == ' ');
	assert(isdigit((unsigned char)buf[9]));
	assert(isdigit((unsigned char)buf[10]));
	assert(buf[11] == ':');
	assert(isdigit((unsigned char)buf[12]));
	assert(isdigit((unsigned char)buf[13]));
	assert(buf[14] == ':');
	assert(isdigit((unsigned char)buf[15]));
	assert(isdigit((unsigned char)buf[16]));
}

static void test_live_header_and_footer_show_timestamps(void)
{
	struct live_snapshot snap;

	reset_mocks();
	memset(&snap, 0, sizeof(snap));
	snprintf(snap.pid, sizeof(snap.pid), "%s", "123");
	snprintf(snap.captured_at, sizeof(snap.captured_at), "%s",
		 "26/03/26 12:34:56");
	snap.view = VIEW_MEMORY;
	print_live_header(&snap, 0, 1);
	print_live_footer(snap.captured_at);

	assert(strstr(output_buf, "Snapshot start: "));
	assert(strstr(output_buf, "Snapshot end:   "));
}

static void test_snapshot_history_offset_navigation(void)
{
	struct live_snapshot history[MAX_SNAPSHOTS];
	struct live_snapshot snap;
	struct live_snapshot *picked;
	int history_count = 0;
	int history_next = 0;

	memset(history, 0, sizeof(history));
	memset(&snap, 0, sizeof(snap));

	snprintf(snap.pid, sizeof(snap.pid), "%s", "111");
	snprintf(snap.captured_at, sizeof(snap.captured_at), "%s",
		 "26/03/26 12:00:01");
	snap.view = VIEW_MEMORY;
	append_snapshot(history, &history_count, &history_next, &snap);

	snprintf(snap.pid, sizeof(snap.pid), "%s", "222");
	snprintf(snap.captured_at, sizeof(snap.captured_at), "%s",
		 "26/03/26 12:00:02");
	append_snapshot(history, &history_count, &history_next, &snap);

	picked =
		get_snapshot_by_offset(history, history_count, history_next, 0);
	assert(picked);
	assert(strcmp(picked->pid, "222") == 0);

	picked =
		get_snapshot_by_offset(history, history_count, history_next, 1);
	assert(picked);
	assert(strcmp(picked->pid, "111") == 0);

	clear_snapshot_history(history, &history_count, &history_next);
}

int main(void)
{
	test_build_proc_path_helper();
	test_print_cmdline_replaces_nul_with_space();
	test_print_process_info_happy_path();
	test_main_argument_pid_is_bounded();
	test_main_exits_when_module_not_loaded();
	test_main_exits_when_proc_file_missing();
	test_main_exits_without_root_privileges();
	test_det_preamble_stops_before_sections();
	test_memory_view_filters_network_section();
	test_network_view_starts_from_network_section();
	test_io_view_starts_from_io_section();
	test_format_current_time_layout();
	test_live_header_and_footer_show_timestamps();
	test_snapshot_history_offset_navigation();
	puts("proc_elf_ctrl tests passed");
	reset_mocks();
	return 0;
}
