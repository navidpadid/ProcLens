/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Build path to /proc/elf_det/<name> with optional override via env. */
static inline char *build_proc_path(const char *name)
{
	const char *base = getenv("ELF_DET_PROC_DIR");

	if (!base || !*base)
		base = "/proc/elf_det";

	size_t len = strlen(base) + 1 + strlen(name) + 1;
	char *p = (char *)malloc(len);

	if (!p)
		return NULL;
	snprintf(p, len, "%s/%s", base, name);
	return p;
}
