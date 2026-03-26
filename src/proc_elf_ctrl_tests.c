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

	return NULL;
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

#define fopen  mock_fopen
#define printf mock_printf
#define puts   mock_puts
#define perror mock_perror
#define main   proc_elf_ctrl_entry
#include "proc_elf_ctrl.c"
#undef main
#undef perror
#undef puts
#undef printf
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

	assert(pid_stream_buf != NULL);
	assert(strcmp(pid_stream_buf, "1234") == 0);
	assert(strstr(output_buf, "PROCESS INFORMATION"));
	assert(strstr(output_buf, "THREAD INFORMATION"));
	assert(strstr(output_buf, "det-line-1"));
	assert(strstr(output_buf, "thread-line-1"));
}

static void test_main_argument_pid_is_bounded(void)
{
	char long_pid[] = "123456789012345678901234567890";
	char *argv[] = {"prog", long_pid};

	reset_mocks();
	cmdline_len = 0;
	proc_elf_ctrl_entry(2, argv);

	assert(pid_stream_buf != NULL);
	assert(pid_stream_len == 19);
	assert(strncmp(pid_stream_buf, "1234567890123456789", 19) == 0);
}

static void test_det_preamble_stops_before_sections(void)
{
	const char *det = "Process ID:     123\n"
			  "Name:            foo\n"
			  "CPU Usage:       1.00%\n"
			  "Memory Pressure Statistics:\n"
			  "  RSS (Resident): 10 KB\n"
			  "[network]\n"
			  "sockets_total: 1\n";

	reset_mocks();
	print_det_preamble(det);

	assert(strstr(output_buf, "Process ID:     123"));
	assert(strstr(output_buf, "Name:            foo"));
	assert(strstr(output_buf, "CPU Usage:       1.00%"));
	assert(!strstr(output_buf, "Memory Pressure Statistics:"));
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
			  "sockets_total: 2\n";

	reset_mocks();
	print_memory_view(det);

	assert(strstr(output_buf, "Memory Pressure Statistics:"));
	assert(strstr(output_buf, "Memory Layout Visualization:"));
	assert(!strstr(output_buf, "[network]"));
	assert(!strstr(output_buf, "sockets_total"));
}

static void test_network_view_starts_from_network_section(void)
{
	const char *det = "Memory Pressure Statistics:\n"
			  "  RSS (Resident): 10 KB\n"
			  "[network]\n"
			  "sockets_total: 2\n"
			  "Open Sockets:\n";

	reset_mocks();
	print_network_view(det);

	assert(!strstr(output_buf, "Memory Pressure Statistics:"));
	assert(strstr(output_buf, "[network]"));
	assert(strstr(output_buf, "sockets_total: 2"));
	assert(strstr(output_buf, "Open Sockets:"));
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
	reset_mocks();
	print_live_header("123", VIEW_MEMORY);
	print_live_footer();

	assert(strstr(output_buf, "Snapshot start: "));
	assert(strstr(output_buf, "Snapshot end:   "));
}

int main(void)
{
	test_build_proc_path_helper();
	test_print_cmdline_replaces_nul_with_space();
	test_print_process_info_happy_path();
	test_main_argument_pid_is_bounded();
	test_det_preamble_stops_before_sections();
	test_memory_view_filters_network_section();
	test_network_view_starts_from_network_section();
	test_format_current_time_layout();
	test_live_header_and_footer_show_timestamps();
	puts("proc_elf_ctrl tests passed");
	reset_mocks();
	return 0;
}
