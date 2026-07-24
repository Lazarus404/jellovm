// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) Jahred Love. All rights reserved.

#if defined(__linux__) && !defined(_POSIX_C_SOURCE) && !defined(_GNU_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <jello.h>
#include <jello/internal/vm_internal.h>
#include <jello/internal/boot_internal.h>

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__)
#  define JELLO_HAVE_CLOCK_GETTIME 1
#endif

#ifdef JELLO_HAVE_CLOCK_GETTIME
#  include <time.h>
#endif

static double now_ms(void) {
#ifdef JELLO_HAVE_CLOCK_GETTIME
	struct timespec ts;
	if(clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0.0;
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#else
	return 0.0;
#endif
}

static uint64_t parse_u64_env(const char* key, uint64_t def) {
	const char* s = getenv(key);
	if(!s || !*s) return def;
	char* end = NULL;
	unsigned long long v = strtoull(s, &end, 10);
	if(end == s) return def;
	return (uint64_t)v;
}

static uint8_t* read_file(const char* path, size_t* out_size) {
	if(out_size) *out_size = 0;
	FILE* f = fopen(path, "rb");
	if(!f) return NULL;

	if(fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	long sz = ftell(f);
	if(sz < 0) {
		fclose(f);
		return NULL;
	}
	if(fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}

	uint8_t* data = (uint8_t*)malloc((size_t)sz);
	if(!data) {
		fclose(f);
		return NULL;
	}
	size_t got = fread(data, 1, (size_t)sz, f);
	fclose(f);
	if(got != (size_t)sz) {
		free(data);
		return NULL;
	}
	if(out_size) *out_size = (size_t)sz;
	return data;
}

static void usage(const char* argv0) {
	fprintf(stderr, "usage: %s [--no-jit] <module.jlo> [args...]\n", argv0 ? argv0 : "jellovm");
	fprintf(stderr, "       %s --boot <module.jlo>\n", argv0 ? argv0 : "jellovm");
}

static int strip_no_jit_flag(int* argc, char*** argv, int* no_jit) {
	int* argcp = argc;
	char** argvv = *argv;
	int out = 0;
	int w = 1;
	for(int r = 1; r < *argcp; r++) {
		if(strcmp(argvv[r], "--no-jit") == 0) {
			if(no_jit) *no_jit = 1;
			continue;
		}
		argvv[w++] = argvv[r];
	}
	argvv[w] = NULL;
	*argcp = w;
	return out;
}

static int run_module(
    const char* entry_path,
    const uint8_t* data,
    size_t size,
    int prog_argc,
    char** prog_argv,
    int no_jit
) {
	const uint8_t profile = (getenv("JELLO_PROFILE") != NULL);
	double t0 = 0.0, t_read = 0.0, t_bc = 0.0, t_vm_create = 0.0, t_exec = 0.0;

	if(profile) t0 = now_ms();
	if(profile) t_read = now_ms() - t0;

	if(profile) t0 = now_ms();
	jello_bc_module* m = NULL;
	jello_bc_result r = jello_bc_read(data, size, &m);
	if(profile) t_bc = now_ms() - t0;
	if(r.err != JELLO_BC_OK) {
		fprintf(stderr, "error: bytecode load failed: err=%d msg=%s offset=%zu\n",
		        (int)r.err,
		        r.msg ? r.msg : "(null)",
		        r.offset);
		return 2;
	}
	if(!jello_jdll_preflight_module(m, jello_discovery_entry_path(entry_path))) {
		jello_bc_free(m);
		return 2;
	}

	if(profile) t0 = now_ms();
	jello_vm* vm = jello_vm_create();
	if(profile) t_vm_create = now_ms() - t0;
	if(!vm) {
		fprintf(stderr, "error: failed to create VM\n");
		jello_bc_free(m);
		return 2;
	}
	if(no_jit) jello_vm_set_jit_enabled(vm, 0);
	if(profile) jello_vm_set_profile(vm, 1);
	jello_vm_set_entry_path(vm, jello_discovery_entry_path(entry_path));
	if(prog_argc > 0) {
		jello_vm_set_program_args(vm, prog_argc, prog_argv);
	}

	jello_vm_set_fuel(vm, parse_u64_env("JELLO_FUEL", 0));
	{
		uint64_t v = parse_u64_env("JELLO_MAX_BYTES", (uint64_t)(64u * 1024u * 1024u));
		if(v > 0xffffffffull) v = 0xffffffffull;
		jello_vm_set_max_bytes_len(vm, (uint32_t)v);
	}
	{
		uint64_t v = parse_u64_env("JELLO_MAX_ARRAY", (uint64_t)(8u * 1024u * 1024u));
		if(v > 0xffffffffull) v = 0xffffffffull;
		jello_vm_set_max_array_len(vm, (uint32_t)v);
	}

	if(profile) t0 = now_ms();
	jello_value out = jello_make_null();
	jello_exec_status st = jello_vm_exec_status(vm, m, &out);
	if(profile) t_exec = now_ms() - t0;
	if(profile) {
		fprintf(stderr, "JELLO_PROFILE read_file=%.2f bc_read=%.2f vm_create=%.2f exec=%.2f (ms)\n",
		        t_read, t_bc, t_vm_create, t_exec);
		fprintf(stderr,
		        "JELLO_PROFILE gc_collections=%llu gc_freed=%llu freelist_hits=%llu freelist_misses=%llu live=%zu\n",
		        (unsigned long long)vm->gc_collections,
		        (unsigned long long)vm->gc_freed_objects,
		        (unsigned long long)vm->gc_freelist_hits,
		        (unsigned long long)vm->gc_freelist_misses,
		        vm->gc_bytes_live);
		jello_vm_profile_dump(vm, stderr);
	}

	if(st == JELLO_EXEC_TRAP) {
		fprintf(stderr, "trap: code=%d msg=%s\n",
		        (int)jello_vm_last_trap_code(vm),
		        jello_vm_last_trap_msg(vm) ? jello_vm_last_trap_msg(vm) : "(null)");
		jello_vm_print_stack_trace(vm, stderr);
		jello_bc_free(m);
		jello_vm_destroy(vm);
		return 1;
	}

	if(jello_is_null(out)) {
		puts("null");
	} else if(jello_is_bool(out)) {
		puts(jello_as_bool(out) ? "true" : "false");
	} else if(jello_is_i32(out)) {
		printf("%d\n", jello_as_i32(out));
	} else if(jello_is_atom(out)) {
		printf("atom(%u)\n", jello_as_atom(out));
	} else if(jello_is_box_i64(out)) {
		printf("%" PRId64 "\n", jello_as_box_i64(out));
	} else if(jello_is_box_f64(out)) {
		printf("%g\n", jello_as_box_f64(out));
	} else if(jello_is_box_f32(out)) {
		printf("%g\n", (double)jello_as_box_f32(out));
	} else if(jello_is_box_f16(out)) {
		printf("%g\n", (double)vm_f16_bits_to_f32(jello_as_box_f16(out)));
	} else if(jello_is_ptr(out)) {
		const jello_obj_header* h = jello_obj_header_of(out);
		if(h && h->kind == (uint32_t)JELLO_OBJ_BYTES) {
			const jello_bytes* b = (const jello_bytes*)jello_as_ptr(out);
			fwrite(b->data, 1, b->length, stdout);
			fputc('\n', stdout);
		} else {
			printf("ptr(" JELLO_PTR_FMT ")\n", (uintptr_t)jello_as_ptr(out));
		}
	} else {
		puts("<value>");
	}

	jello_bc_free(m);
	jello_vm_destroy(vm);
	return 0;
}

static int handle_boot_create(int argc, char** argv) {
	if(argc < 3 || !argv[2] || argv[2][0] == '\0') {
		fprintf(stderr, "error: --boot requires a path to a .jlo file\n");
		usage(argv[0]);
		return 2;
	}

	char self_path[4096];
	if(jello_boot_self_exe_path(self_path, sizeof(self_path)) != 0) {
		fprintf(stderr, "error: failed to resolve VM executable path\n");
		return 2;
	}

	char out_path[4096];
	if(jello_boot_output_path(argv[2], out_path, sizeof(out_path)) != 0) {
		fprintf(stderr, "error: failed to derive boot executable path from '%s'\n", argv[2]);
		return 2;
	}

	if(jello_boot_create(self_path, argv[2], out_path) != 0) {
		fprintf(stderr, "error: failed to create boot executable '%s': %s\n", out_path, strerror(errno));
		return 2;
	}

	printf("%s\n", out_path);
	return 0;
}

int main(int argc, char** argv) {
	if(argc >= 2 && strcmp(argv[1], "--boot") == 0) {
		return handle_boot_create(argc, argv);
	}

	int no_jit = 0;
	strip_no_jit_flag(&argc, &argv, &no_jit);

	char self_path[4096];
	const char* self_resolved = NULL;
	if(jello_boot_self_exe_path(self_path, sizeof(self_path)) == 0) {
		self_resolved = self_path;
	}

	jello_boot_image boot = {0};
	int booted = self_resolved ? jello_boot_probe(self_resolved, &boot) : 0;
	if(booted < 0) {
		fprintf(stderr, "error: failed to inspect boot image\n");
		return 2;
	}
	if(booted > 0) {
		int rc = run_module(self_resolved, boot.data, boot.size, argc - 1, argc > 1 ? argv + 1 : NULL, no_jit);
		jello_boot_image_free(&boot);
		return rc;
	}

	if(argc < 2) {
		usage(argv[0]);
		return 2;
	}

	const char* path = argv[1];
	size_t size = 0;
	uint8_t* data = read_file(path, &size);
	if(!data) {
		fprintf(stderr, "error: failed to read '%s': %s\n", path, strerror(errno));
		return 2;
	}

	int rc = run_module(path, data, size, argc - 2, argc > 2 ? argv + 2 : NULL, no_jit);
	free(data);
	return rc;
}
