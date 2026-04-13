/**
 * Copyright 2017 - Jahred Love
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may
 * be used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <jello.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void jello_fatal(const char* msg) {
	// Keep behavior test-friendly: unsupported formats should be a hard failure,
	// but we want CTest's WILL_FAIL to observe a non-zero exit status (not a signal).
	fprintf(stderr, "jello_fatal: %s\n", msg ? msg : "(null)");
	exit(1);
}

static size_t utostr_utf8_len(const uchar* str) {
	// Returns number of UTF-8 bytes (not including the final '\0').
	size_t n = 0;
	for(;;) {
		unsigned int c = (unsigned int)(*str++);
		if(c == 0) break;
		if(c < 0x80u) {
			n += 1;
		} else if(c < 0x800u) {
			n += 2;
		} else if(c >= 0xD800u && c <= 0xDFFFu) {
			// surrogate pair -> 4 bytes
			// (Matches `utostr` assumption: input is well-formed UTF-16.)
			(void)(*str++);
			n += 4;
		} else {
			n += 3;
		}
	}
	return n;
}

double utod(const uchar* str, uchar** end) {
	// Same semantics as the legacy Hashlink-style helper:
	// - copies up to 30 code units that look like a numeric prefix
	// - uses `strtod` for final parsing and end pointer
	//
	// Fast-path: if the first code unit can't possibly start a number, avoid
	// calling into libc at all.
	if(!str) {
		if(end) *end = (uchar*)str;
		return 0.0;
	}

	unsigned int c0 = (unsigned int)(*str);
	if(!( (c0 - (unsigned)'0') <= 9u ||
	      c0 == (unsigned)'.' ||
	      c0 == (unsigned)'e' ||
	      c0 == (unsigned)'E' ||
	      c0 == (unsigned)'-' ||
	      c0 == (unsigned)'+')) {
		if(end) *end = (uchar*)str;
		return 0.0;
	}

	char buf[31];
	char* w = buf;
	const char* wend = buf + 30;
	const uchar* p = str;
	while(w < wend) {
		unsigned int c = (unsigned int)(*p);
		if(!((c - (unsigned)'0') <= 9u ||
		     c == (unsigned)'.' ||
		     c == (unsigned)'e' ||
		     c == (unsigned)'E' ||
		     c == (unsigned)'-' ||
		     c == (unsigned)'+')) {
			break;
		}
		*w++ = (char)c;
		p++;
	}
	*w = 0;

	char* bend = NULL;
	double result = strtod(buf, &bend);
	if(end) *end = (uchar*)str + (bend ? (bend - buf) : 0);
	return result;
}

int utoi(const uchar* str, uchar** end) {
	// Fast integer parse from UTF-16 digits (no strtol / no temp buffer).
	// Matches current semantics:
	// - optional leading +/- is allowed
	// - parse up to 16 code units total (including sign if present)
	// - if no digits are consumed, end == str and result == 0
	const uchar* p = str;
	int neg = 0;
	int consumed = 0;

	uchar c0 = *p;
	if(c0 == (uchar)'-' || c0 == (uchar)'+') {
		neg = (c0 == (uchar)'-');
		p++;
		consumed++;
	}

	int64_t acc = 0;
	int ndigits = 0;
	while(consumed < 16) {
		unsigned int c = (unsigned int)(*p);
		if(c < (unsigned)'0' || c > (unsigned)'9') break;
		acc = acc * 10 + (int64_t)(c - (unsigned)'0');
		p++;
		consumed++;
		ndigits++;
	}

	if(ndigits == 0) {
		*end = (uchar*)str;
		return 0;
	}

	if(neg) acc = -acc;
	*end = (uchar*)p;
	return (int)acc;
}

int ustrlen(const uchar* str) {
	const uchar *p = str;
	while( *p ) p++;
	return (int)(p - str);
}

uchar* ustrdup(const uchar* str) {
	int len = ustrlen(str);
	int size = (len + 1) << 1;
	uchar *d = (uchar*)malloc((size_t)size);
	memcpy(d, str, (size_t)size);
	return d;
}

int ucmp(const uchar* a, const uchar* b) {
	while(true) {
		int d = (int)(unsigned)*a - (int)(unsigned)*b;
		if( d ) return d;
		if( !*a ) return 0;
		a++;
		b++;
	}
}

int utostr(char* out, int out_size, const uchar* str) {
	if(out_size <= 0) return 0;
	char* start = out;
	char* end = out + out_size - 1; // final 0

	// Fast ASCII path.
	while(out < end) {
		unsigned int c = (unsigned int)(*str);
		if(c == 0) break;
		if(c >= 0x80u) break;
		*out++ = (char)c;
		str++;
	}

	while(out < end) {
		unsigned int c = (unsigned int)(*str++);
		if( c == 0 ) break;
		if(c < 0x80u) {
			*out++ = (char)c;
		} else if( c < 0x800u ) {
			if( out + 2 > end ) break;
			*out++ = (char)(0xC0|(c>>6));
			*out++ = (char)(0x80|(c&63u));
		} else if( c >= 0xD800u && c <= 0xDFFFu ) { // surrogate pair
			if( out + 4 > end ) break;
			unsigned int full = (((c - 0xD800u) << 10) | (((unsigned int)(*str++)) - 0xDC00u)) + 0x10000u;
			*out++ = (char)(0xF0|(full>>18));
			*out++ = (char)(0x80|((full>>12)&63u));
			*out++ = (char)(0x80|((full>>6)&63u));
			*out++ = (char)(0x80|(full&63u));
		} else {
			if( out + 3 > end ) break;
			*out++ = (char)(0xE0|(c>>12));
			*out++ = (char)(0x80|((c>>6)&63u));
			*out++ = (char)(0x80|(c&63u));
		}
	}
	*out = 0;
	return (int)(out - start);
}

int usprintf(uchar* out, int out_size, const uchar* fmt, ...) {
	va_list args;
	int ret;
	va_start(args, fmt);
	ret = uvszprintf(out, out_size, fmt, args);
	va_end(args);
	return ret;
}

static uchar* append_ascii(uchar* out, const uchar* end, const char* s, int n) {
	if(n <= 0) return out;
	while(n-- > 0 && out < end) {
		*out++ = (uchar)(unsigned char)*s++;
	}
	return out;
}

static int u32_to_dec(char* out, uint32_t v) {
	// Writes digits to out and returns length. out must have >= 10 bytes.
	char tmp[10];
	int n = 0;
	do {
		tmp[n++] = (char)('0' + (v % 10u));
		v /= 10u;
	} while(v != 0u);
	for(int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
	return n;
}

static int u64_to_dec(char* out, uint64_t v) {
	// out must have >= 20 bytes.
	char tmp[20];
	int n = 0;
	do {
		tmp[n++] = (char)('0' + (v % 10u));
		v /= 10u;
	} while(v != 0u);
	for(int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
	return n;
}

static int u32_to_hex(char* out, uint32_t v, int upper) {
	static const char* lo = "0123456789abcdef";
	static const char* hi = "0123456789ABCDEF";
	const char* digs = upper ? hi : lo;
	char tmp[8];
	int n = 0;
	do {
		tmp[n++] = digs[v & 0xFu];
		v >>= 4;
	} while(v != 0u);
	for(int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
	return n;
}

static int uptr_to_hex(char* out, uintptr_t v, int upper) {
	static const char* lo = "0123456789abcdef";
	static const char* hi = "0123456789ABCDEF";
	const char* digs = upper ? hi : lo;
	char tmp[2 * (int)sizeof(uintptr_t)];
	int n = 0;
	do {
		tmp[n++] = digs[v & (uintptr_t)0xFu];
		v >>= 4;
	} while(v != (uintptr_t)0);
	for(int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
	return n;
}

int uvszprintf(uchar* out, int out_size, const uchar* fmt, va_list arglist) {
	if(out_size <= 0) return 0;

	uchar* start = out;
	uchar* end = out + out_size - 1; // final 0

	// Fast, intentionally-limited formatter:
	// - supports: %% %s %d %ld %f %g %x %X (with optional leading 'l')
	// - rejects: width/precision/flags
	while(out < end) {
		uchar c = *fmt++;
		if(c == 0) break;

		if(c != (uchar)'%') {
			*out++ = c;
			continue;
		}

		uchar spec = *fmt++;
		if(spec == 0) break;

		if(spec == (uchar)'%') {
			*out++ = (uchar)'%';
			continue;
		}

		// Reject any flags/width/precision. Keep behavior explicit and fast.
		if(spec == (uchar)'.' || (spec >= (uchar)'0' && spec <= (uchar)'9')) {
			jello_fatal("Unsupported printf format");
		}

		int is_long = 0;
		if(spec == (uchar)'l') {
			is_long = 1;
			spec = *fmt++;
			if(spec == 0) break;
		}

		if(spec == (uchar)'s') {
			uchar* s = va_arg(arglist, uchar*);
			if(!s) s = (uchar*)USTR("(null)");
			while(*s && out < end) *out++ = *s++;
			continue;
		}

		char tmp[64];
		int size = 0;

		switch((char)spec) {
		case 'd':
			if(is_long) {
				int64 v = (int64)va_arg(arglist, int64);
				uint64 u = (uint64)v;
				if(v < 0) {
					u = 0u - u;
					tmp[0] = '-';
					size = 1 + u64_to_dec(tmp + 1, u);
				} else {
					size = u64_to_dec(tmp, u);
				}
			} else {
				int v = va_arg(arglist, int);
				uint32_t u = (uint32_t)v;
				if(v < 0) {
					u = 0u - u;
					tmp[0] = '-';
					size = 1 + u32_to_dec(tmp + 1, u);
				} else {
					size = u32_to_dec(tmp, u);
				}
			}
			break;
		case 'f':
			// Keep libc formatting for floats (rare in our usage); it is slower but correct.
			size = snprintf(tmp, sizeof(tmp), "%f", va_arg(arglist, double));
			break;
		case 'g':
			size = snprintf(tmp, sizeof(tmp), "%g", va_arg(arglist, double));
			break;
		case 'x':
		case 'X':
			if(is_long) {
				void* p = va_arg(arglist, void*);
				uintptr_t v = (uintptr_t)p;
				size = uptr_to_hex(tmp, v, spec == (uchar)'X');
			} else {
				uint32_t v = (uint32_t)va_arg(arglist, unsigned int);
				size = u32_to_hex(tmp, v, spec == (uchar)'X');
			}
			break;
		default:
			jello_fatal("Unsupported printf format");
			break;
		}

		// Copy C string -> UTF-16 string (ASCII only).
		if(size < 0) size = 0;
		out = append_ascii(out, end, tmp, size);
	}

	*out = 0;
	return (int)(out - start);
}

void uprintf(const uchar* fmt, const uchar* str) {
	// Avoid heap for common small strings; fall back to malloc for large ones.
	char fmt_stack[512];
	char str_stack[512];

	size_t fmt_len = fmt ? utostr_utf8_len(fmt) : 0;
	size_t str_len = str ? utostr_utf8_len(str) : 0;

	char* cfmt = NULL;
	char* cstr = NULL;

	if(fmt_len < sizeof(fmt_stack)) {
		cfmt = fmt_stack;
	} else {
		cfmt = (char*)malloc(fmt_len + 1u);
	}

	if(str_len < sizeof(str_stack)) {
		cstr = str_stack;
	} else {
		cstr = (char*)malloc(str_len + 1u);
	}

	if(cfmt && cstr) {
		if(fmt) utostr(cfmt, (int)(fmt_len + 1u), fmt); else cfmt[0] = 0;
		if(str) utostr(cstr, (int)(str_len + 1u), str); else cstr[0] = 0;
		printf(cfmt, cstr);
	}

	if(cfmt != fmt_stack) free(cfmt);
	if(cstr != str_stack) free(cstr);
}
