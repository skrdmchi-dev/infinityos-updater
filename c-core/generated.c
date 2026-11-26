#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <math.h>

typedef enum { ST_NULL, ST_BOOL, ST_NUM, ST_STR, ST_ARR, ST_MAP, ST_FN } SType;
typedef struct SVal SVal;
typedef struct SArr SArr;
typedef struct SMap SMap;
typedef SVal (*SCFn)(SVal __st_this, int argc, SVal* argv);
struct SVal { SType type; SMap* env; union { double num; const char* str; SArr* arr; SMap* map; SCFn fn; }; };
struct SArr { SVal* data; int len; int cap; };
struct SMap { const char** keys; SVal* vals; int len; int cap; };
static void** __steel_arena = NULL; static int __steel_arena_len = 0, __steel_arena_cap = 0;
static void* sv_alloc(size_t sz) {
    void* p = calloc(1, sz);
    if (__steel_arena_len >= __steel_arena_cap) {
        __steel_arena_cap = __steel_arena_cap ? __steel_arena_cap * 2 : 256;
        __steel_arena = (void**)realloc(__steel_arena, sizeof(void*) * __steel_arena_cap);
    }
    __steel_arena[__steel_arena_len++] = p; return p;
}
static void sv_cleanup(void) { for (int i = 0; i < __steel_arena_len; i++) free(__steel_arena[i]); free(__steel_arena); }
static inline SVal sv_null(void) { SVal v; v.type = ST_NULL; v.num = 0; return v; }
static inline SVal sv_bool(bool b) { SVal v; v.type = ST_BOOL; v.num = b ? 1.0 : 0.0; return v; }
static inline SVal sv_num(double n) { SVal v; v.type = ST_NUM; v.num = n; return v; }
static inline SVal sv_str(const char* s) { SVal v; v.type = ST_STR; v.str = s; return v; }
static inline SVal sv_fn(SCFn f) { SVal v; v.type = ST_FN; v.env = NULL; v.fn = f; return v; }
static inline SVal sv_call_fn(SVal f, int argc, SVal* argv) {
    if (f.type != ST_FN || !f.fn) return sv_null();
    SVal env; env.type = f.env ? ST_MAP : ST_NULL; env.env = NULL; env.map = f.env;
    return f.fn(env, argc, argv);
}
static SVal sv_arr_new(int count, SVal* init_items) {
    SArr* a = (SArr*)sv_alloc(sizeof(SArr)); a->cap = count > 4 ? count * 2 : 8; a->len = count;
    a->data = (SVal*)sv_alloc(sizeof(SVal) * a->cap);
    for (int i = 0; i < count; i++) a->data[i] = init_items[i];
    SVal v; v.type = ST_ARR; v.arr = a; return v;
}
static SVal sv_push(SVal arr, SVal item) {
    if (arr.type != ST_ARR) return arr;
    SArr* a = arr.arr;
    if (a->len >= a->cap) { int nc = a->cap * 2; SVal* nd = (SVal*)sv_alloc(sizeof(SVal) * nc); memcpy(nd, a->data, sizeof(SVal) * a->len); a->data = nd; a->cap = nc; }
    a->data[a->len++] = item; return arr;
}
static SVal sv_pop(SVal arr) { return (arr.type != ST_ARR || arr.arr->len == 0) ? sv_null() : arr.arr->data[--arr.arr->len]; }
static SVal sv_range(SVal a, SVal b) { int s = (int)a.num, e = (int)b.num; SVal r = sv_arr_new(0, NULL); for (int i = s; i < e; i++) sv_push(r, sv_num(i)); return r; }
static SVal sv_map_new(void) {
    SMap* m = (SMap*)sv_alloc(sizeof(SMap)); m->cap = 16; m->len = 0;
    m->keys = (const char**)sv_alloc(sizeof(char*) * m->cap); m->vals = (SVal*)sv_alloc(sizeof(SVal) * m->cap);
    SVal v; v.type = ST_MAP; v.map = m; return v;
}
static void sv_map_set(SVal obj, const char* key, SVal val) {
    if (obj.type != ST_MAP) return;
    SMap* m = obj.map;
    for (int i = 0; i < m->len; i++) if (strcmp(m->keys[i], key) == 0) { m->vals[i] = val; return; }
    if (m->len >= m->cap) { int nc = m->cap * 2; const char** nk = (const char**)sv_alloc(sizeof(char*) * nc); SVal* nv = (SVal*)sv_alloc(sizeof(SVal) * nc); memcpy(nk, m->keys, sizeof(char*) * m->len); memcpy(nv, m->vals, sizeof(SVal) * m->len); m->keys = nk; m->vals = nv; m->cap = nc; }
    m->keys[m->len] = key; m->vals[m->len] = val; m->len++;
}
static SVal sv_map_get(SVal obj, const char* key) {
    if (obj.type != ST_MAP) return sv_null();
    for (int i = 0; i < obj.map->len; i++) if (strcmp(obj.map->keys[i], key) == 0) return obj.map->vals[i];
    return sv_null();
}
static SVal sv_call_method(SVal obj, const char* name, int argc, ...) {
    SVal m = sv_map_get(obj, name);
    if (m.type != ST_FN || !m.fn) return sv_null();
    SVal argv[16]; va_list ap; va_start(ap, argc);
    for (int i = 0; i < argc && i < 16; i++) argv[i] = va_arg(ap, SVal);
    va_end(ap); return m.fn(obj, argc, argv);
}
static SVal sv_closure(SCFn fn, int ncap, const char** ks, SVal* vs) {
    SVal env = sv_map_new();
    for (int i = 0; i < ncap; i++) sv_map_set(env, ks[i], vs[i]);
    SVal v = sv_fn(fn); v.env = env.map; return v;
}
static SVal sv_get(SVal t, SVal idx) {
    if (t.type == ST_MAP && idx.type == ST_STR) return sv_map_get(t, idx.str);
    if (t.type == ST_ARR && idx.type == ST_NUM) { int i = (int)idx.num; if (i < 0) i += t.arr->len; return (i >= 0 && i < t.arr->len) ? t.arr->data[i] : sv_null(); }
    return sv_null();
}
static void sv_set(SVal t, SVal idx, SVal val) { if (t.type == ST_MAP && idx.type == ST_STR) { sv_map_set(t, idx.str, val); return; } if (t.type == ST_ARR && idx.type == ST_NUM) { int i = (int)idx.num; if (i < 0) i += t.arr->len; if (i >= 0 && i < t.arr->len) t.arr->data[i] = val; } }
static inline bool sv_truthy(SVal v) {
    if (v.type == ST_NULL) return false;
    if (v.type == ST_BOOL || v.type == ST_NUM) return v.num != 0.0;
    if (v.type == ST_STR) return v.str && v.str[0] != '\0';
    return true;
}
static bool sv_eq(SVal a, SVal b) {
    if (a.type != b.type) return false;
    return a.type == ST_STR ? strcmp(a.str, b.str) == 0 : a.num == b.num;
}
static SVal sv_str_cast(SVal v) {
    if (v.type == ST_STR) return v;
    char* buf = (char*)sv_alloc(128);
    if (v.type == ST_NULL) snprintf(buf, 128, "null");
    else if (v.type == ST_BOOL) snprintf(buf, 128, "%s", v.num != 0.0 ? "true" : "false");
    else snprintf(buf, 128, floor(v.num) == v.num ? "%.0f" : "%g", v.num);
    return sv_str(buf);
}
static SVal sv_add(SVal a, SVal b) {
    if (a.type == ST_NUM && b.type == ST_NUM) return sv_num(a.num + b.num);
    SVal sa = sv_str_cast(a), sb = sv_str_cast(b);
    char* res = (char*)sv_alloc(strlen(sa.str) + strlen(sb.str) + 1);
    strcpy(res, sa.str); strcat(res, sb.str);
    return sv_str(res);
}
static SVal sv_len(SVal v) {
    if (v.type == ST_ARR) return sv_num(v.arr->len);
    if (v.type == ST_STR) return sv_num((double)strlen(v.str));
    if (v.type == ST_MAP) return sv_num(v.map->len);
    return sv_num(0);
}
static void sv_print_one(SVal v) {
    if (v.type == ST_ARR) { printf("["); for (int i = 0; i < v.arr->len; i++) { if (i > 0) printf(", "); sv_print_one(v.arr->data[i]); } printf("]"); }
    else printf("%s", sv_str_cast(v).str);
}

#include <ctype.h>
static inline SVal sv_read_file(SVal p) {
    FILE* f = fopen(p.type == ST_STR ? p.str : "", "rb"); if (!f) return sv_str("");
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    char* buf = (char*)calloc(1, (size_t)sz + 1); if (sz > 0) fread(buf, 1, (size_t)sz, f);
    fclose(f); return sv_str(buf);
}
static inline SVal sv_write_file(SVal p, SVal c) {
    FILE* f = fopen(p.type == ST_STR ? p.str : "", "wb"); if (!f) return sv_bool(false);
    const char* txt = c.type == ST_STR ? c.str : "";
    fwrite(txt, 1, strlen(txt), f); fclose(f); return sv_bool(true);
}
static inline SVal sv_file_exists(SVal p) {
    FILE* f = fopen(p.type == ST_STR ? p.str : "", "rb"); if (f) { fclose(f); return sv_bool(true); } return sv_bool(false);
}
static inline SVal sv_env(SVal k) {
    const char* v = getenv(k.type == ST_STR ? k.str : ""); return v ? sv_str(v) : sv_null();
}
static inline SVal sv_num_cast(SVal v) {
    if (v.type == ST_NUM || v.type == ST_BOOL) return sv_num(v.num);
    if (v.type == ST_STR && v.str) return sv_num(strtod(v.str, NULL));
    return sv_num(0.0);
}
static inline SVal sv_upper(SVal v) {
    const char* src = v.type == ST_STR ? v.str : ""; size_t n = strlen(src);
    char* b = (char*)calloc(1, n + 1); for (size_t i = 0; i < n; i++) b[i] = (char)toupper((unsigned char)src[i]); return sv_str(b);
}
static inline SVal sv_lower(SVal v) {
    const char* src = v.type == ST_STR ? v.str : ""; size_t n = strlen(src);
    char* b = (char*)calloc(1, n + 1); for (size_t i = 0; i < n; i++) b[i] = (char)tolower((unsigned char)src[i]); return sv_str(b);
}
static inline SVal sv_trim(SVal v) {
    const char* src = v.type == ST_STR ? v.str : ""; while (*src && isspace((unsigned char)*src)) src++;
    size_t n = strlen(src); while (n > 0 && isspace((unsigned char)src[n - 1])) n--;
    char* b = (char*)calloc(1, n + 1); memcpy(b, src, n); return sv_str(b);
}
static inline SVal sv_contains(SVal h, SVal n) {
    if (h.type == ST_STR && n.type == ST_STR) return sv_bool(strstr(h.str, n.str) != NULL);
    if (h.type == ST_MAP && n.type == ST_STR) { for (int i = 0; i < h.map->len; i++) if (strcmp(h.map->keys[i], n.str) == 0) return sv_bool(true); return sv_bool(false); }
    if (h.type == ST_ARR) { for (int i = 0; i < h.arr->len; i++) if (sv_eq(h.arr->data[i], n)) return sv_bool(true); return sv_bool(false); }
    return sv_bool(false);
}
static inline SVal sv_starts_with(SVal h, SVal p) {
    if (h.type != ST_STR || p.type != ST_STR) return sv_bool(false);
    size_t pl = strlen(p.str);
    return sv_bool(strncmp(h.str, p.str, pl) == 0);
}
static inline SVal sv_split(SVal s, SVal sep) {
    SVal arr = sv_arr_new(0, NULL);
    const char* src = s.type == ST_STR ? s.str : "";
    const char* sp = sep.type == ST_STR ? sep.str : "";
    size_t sl = strlen(sp);
    if (sl == 0) { for (size_t i = 0; src[i]; i++) { char* c = (char*)calloc(1, 2); c[0] = src[i]; sv_push(arr, sv_str(c)); } return arr; }
    const char* cur = src;
    const char* hit;
    while ((hit = strstr(cur, sp)) != NULL) {
        size_t len = (size_t)(hit - cur);
        char* part = (char*)calloc(1, len + 1); memcpy(part, cur, len);
        sv_push(arr, sv_str(part)); cur = hit + sl;
    }
    size_t rem = strlen(cur); char* last = (char*)calloc(1, rem + 1); memcpy(last, cur, rem);
    sv_push(arr, sv_str(last)); return arr;
}
static inline SVal sv_join(SVal arr, SVal sep) {
    if (arr.type != ST_ARR) return sv_str("");
    const char* sp = sep.type == ST_STR ? sep.str : "";
    size_t sl = strlen(sp), cap = 64, len = 0;
    char* buf = (char*)calloc(1, cap);
    for (int i = 0; i < arr.arr->len; i++) {
        char tmp[64]; const char* part = "";
        SVal v = arr.arr->data[i];
        if (v.type == ST_STR) part = v.str;
        else if (v.type == ST_NUM) { snprintf(tmp, sizeof(tmp), "%g", v.num); part = tmp; }
        else if (v.type == ST_BOOL) part = v.num != 0.0 ? "true" : "false";
        else if (v.type == ST_NULL) part = "null";
        size_t pl = strlen(part), need = len + (i > 0 ? sl : 0) + pl + 1;
        while (need >= cap) { cap *= 2; buf = (char*)realloc(buf, cap); }
        if (i > 0) { memcpy(buf + len, sp, sl); len += sl; }
        memcpy(buf + len, part, pl); len += pl; buf[len] = '\0';
    }
    return sv_str(buf);
}
static inline SVal sv_keys(SVal v) {
    SVal out = sv_arr_new(0, NULL);
    if (v.type == ST_MAP) { for (int i = 0; i < v.map->len; i++) sv_push(out, sv_str(v.map->keys[i])); }
    return out;
}
static inline SVal sv_slice(SVal v, SVal s, SVal e) {
    int si = (int)s.num;
    if (v.type == ST_ARR) {
        int ei = (e.type == ST_NUM) ? (int)e.num : v.arr->len;
        if (si < 0) si = 0; if (ei > v.arr->len) ei = v.arr->len; if (si > ei) si = ei;
        SVal out = sv_arr_new(0, NULL);
        for (int i = si; i < ei; i++) sv_push(out, v.arr->data[i]);
        return out;
    }
    if (v.type == ST_STR) {
        int n = (int)strlen(v.str); int ei = (e.type == ST_NUM) ? (int)e.num : n;
        if (si < 0) si = 0; if (ei > n) ei = n; if (si > ei) si = ei;
        char* b = (char*)calloc(1, (size_t)(ei - si) + 1); memcpy(b, v.str + si, (size_t)(ei - si));
        return sv_str(b);
    }
    return sv_null();
}
static inline const char* __sv_sort_key(SVal v, char* buf, size_t n) {
    if (v.type == ST_STR && v.str) return v.str;
    if (v.type == ST_NUM) { snprintf(buf, n, "%g", v.num); return buf; }
    if (v.type == ST_BOOL) return v.num != 0.0 ? "true" : "false";
    return "null";
}
static inline SVal sv_sort(SVal v) {
    SVal out = sv_arr_new(0, NULL);
    if (v.type != ST_ARR) return out;
    for (int i = 0; i < v.arr->len; i++) sv_push(out, v.arr->data[i]);
    for (int i = 0; i < out.arr->len; i++) {
        for (int j = i + 1; j < out.arr->len; j++) {
            char bi[64], bj[64];
            if (strcmp(__sv_sort_key(out.arr->data[j], bj, sizeof(bj)), __sv_sort_key(out.arr->data[i], bi, sizeof(bi))) < 0) {
                SVal tmp = out.arr->data[i]; out.arr->data[i] = out.arr->data[j]; out.arr->data[j] = tmp;
            }
        }
    }
    return out;
}
static inline SVal sv_type(SVal v) {
    switch (v.type) {
        case ST_NULL: return sv_str("null");
        case ST_BOOL: return sv_str("boolean");
        case ST_NUM: return sv_str("number");
        case ST_STR: return sv_str("string");
        case ST_ARR: return sv_str("array");
        case ST_MAP: return sv_str("map");
        default: return sv_str("function");
    }
}
static inline SVal sv_int_cast(SVal v) {
    double d = (v.type == ST_STR && v.str) ? strtod(v.str, NULL) : ((v.type == ST_NUM || v.type == ST_BOOL) ? v.num : 0.0);
    return sv_num(trunc(d));
}
static inline SVal sv_assert(SVal c, SVal m) {
    if (!sv_truthy(c)) { fprintf(stderr, "Assertion failed: %s\n", (m.type == ST_STR && m.str) ? m.str : ""); exit(1); }
    return sv_bool(true);
}
static inline SVal sv_has_key(SVal m, SVal k) {
    if (m.type == ST_MAP && k.type == ST_STR) { for (int i = 0; i < m.map->len; i++) if (strcmp(m.map->keys[i], k.str) == 0) return sv_bool(true); }
    return sv_bool(false);
}
static inline SVal sv_delete_key(SVal m, SVal k) {
    if (m.type == ST_MAP && k.type == ST_STR) {
        for (int i = 0; i < m.map->len; i++) {
            if (strcmp(m.map->keys[i], k.str) == 0) {
                for (int j = i; j + 1 < m.map->len; j++) { m.map->keys[j] = m.map->keys[j + 1]; m.map->vals[j] = m.map->vals[j + 1]; }
                m.map->len--; return sv_bool(true);
            }
        }
    }
    return sv_bool(false);
}
static inline SVal sv_index_of(SVal h, SVal n) {
    if (h.type == ST_STR && n.type == ST_STR) { const char* p = strstr(h.str, n.str); return sv_num(p ? (double)(p - h.str) : -1.0); }
    if (h.type == ST_ARR) { for (int i = 0; i < h.arr->len; i++) if (sv_eq(h.arr->data[i], n)) return sv_num(i); }
    return sv_num(-1.0);
}
static inline SVal sv_pad_end(SVal v, SVal w, SVal ch) {
    const char* src = (v.type == ST_STR && v.str) ? v.str : ""; const char* p = (ch.type == ST_STR && ch.str) ? ch.str : " ";
    size_t sl = strlen(src), pl = strlen(p), ww = (w.num > 0) ? (size_t)w.num : 0;
    if (sl >= ww || pl == 0) return sv_str(src);
    size_t need = ww - sl; char* b = (char*)calloc(1, ww + 1);
    memcpy(b, src, sl); for (size_t i = 0; i < need; i++) b[sl + i] = p[i % pl]; return sv_str(b);
}
static inline SVal sv_char_code(SVal v) { return (v.type == ST_STR && v.str && v.str[0]) ? sv_num((unsigned char)v.str[0]) : sv_num(0); }
static int __st_argc = 0; static char** __st_argv = NULL;
static inline SVal sv_args(void) { SVal a = sv_arr_new(0, NULL); for (int i = 1; i < __st_argc; i++) sv_push(a, sv_str(__st_argv[i])); return a; }
static inline SVal sv_mkdir(SVal p) {
    const char* path = (p.type == ST_STR && p.str) ? p.str : "";
    char cmd[2048];
#ifdef _WIN32
    char wp[1024]; size_t i = 0; for (; path[i] && i < 1023; i++) wp[i] = (path[i] == '/') ? '\\' : path[i]; wp[i] = '\0';
    snprintf(cmd, sizeof(cmd), "mkdir \"%s\" 2>NUL", wp);
#else
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\" 2>/dev/null", path);
#endif
    (void)system(cmd); return sv_bool(true);
}
static inline SVal sv_remove_dir(SVal p) {
    const char* path = (p.type == ST_STR && p.str) ? p.str : "";
    char cmd[2048];
#ifdef _WIN32
    char wp[1024]; size_t i = 0; for (; path[i] && i < 1023; i++) wp[i] = (path[i] == '/') ? '\\' : path[i]; wp[i] = '\0';
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" 2>NUL", wp);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\" 2>/dev/null", path);
#endif
    (void)system(cmd); return sv_bool(true);
}
static inline SVal sv_rename(SVal a, SVal b) { return sv_bool(rename(a.type == ST_STR ? a.str : "", b.type == ST_STR ? b.str : "") == 0); }
static inline SVal sv_exec(SVal cmd, SVal argv, int has_argv) {
    char full[4096]; snprintf(full, sizeof(full), "%s", (cmd.type == ST_STR && cmd.str) ? cmd.str : "");
    if (has_argv && argv.type == ST_ARR && argv.arr) { for (int i = 0; i < argv.arr->len; i++) { strncat(full, " \"", sizeof(full)-strlen(full)-1); strncat(full, argv.arr->data[i].type == ST_STR ? argv.arr->data[i].str : "", sizeof(full)-strlen(full)-1); strncat(full, "\"", sizeof(full)-strlen(full)-1); } }
#ifdef _WIN32
    FILE* fp = _popen(full, "r");
#else
    FILE* fp = popen(full, "r");
#endif
    size_t cap = 4096, len = 0; char* out = (char*)calloc(1, cap); int rc = -1;
    if (fp) { char buf[1024]; size_t r; while ((r = fread(buf, 1, sizeof(buf), fp)) > 0) { while (len + r + 1 >= cap) { cap *= 2; out = (char*)realloc(out, cap); } memcpy(out + len, buf, r); len += r; out[len] = '\0'; }
#ifdef _WIN32
    rc = _pclose(fp);
#else
    rc = pclose(fp);
#endif
    }
    if (has_argv) { SVal m = sv_map_new(); sv_map_set(m, "code", sv_num((double)rc)); sv_map_set(m, "stdout", sv_str(out)); sv_map_set(m, "stderr", sv_str("")); return m; }
    while (len > 0 && (out[len-1]=='\n'||out[len-1]=='\r')) out[--len] = '\0';
    return sv_str(out);
}
#include <stdint.h>
static char* __sv_sha256_raw(const unsigned char* data, size_t len) {
    static const uint32_t primes[64] = {2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,73,79,83,89,97,101,103,107,109,113,127,131,137,139,149,151,157,163,167,173,179,181,191,193,197,199,211,223,227,229,233,239,241,251,257,263,269,271,277,281,283,293,307,311};
    uint32_t k[64], h[8];
    for (int i = 0; i < 64; i++) { double r = cbrt((double)primes[i]); k[i] = (uint32_t)((r - floor(r)) * 4294967296.0); }
    for (int i = 0; i < 8; i++) { double r = sqrt((double)primes[i]); h[i] = (uint32_t)((r - floor(r)) * 4294967296.0); }
    size_t pad_len = ((len + 9 + 63) / 64) * 64; unsigned char* msg = (unsigned char*)calloc(1, pad_len);
    memcpy(msg, data, len); msg[len] = 0x80; uint64_t bl = (uint64_t)len * 8ULL;
    for (int i = 0; i < 8; i++) msg[pad_len - 1 - i] = (unsigned char)((bl >> (i * 8)) & 0xff);
    for (size_t off = 0; off < pad_len; off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; i++) w[i] = ((uint32_t)msg[off+4*i]<<24)|((uint32_t)msg[off+4*i+1]<<16)|((uint32_t)msg[off+4*i+2]<<8)|((uint32_t)msg[off+4*i+3]);
        for (int i = 16; i < 64; i++) { uint32_t s0 = ((w[i-15]>>7)|(w[i-15]<<25))^((w[i-15]>>18)|(w[i-15]<<14))^(w[i-15]>>3); uint32_t s1 = ((w[i-2]>>17)|(w[i-2]<<15))^((w[i-2]>>19)|(w[i-2]<<13))^(w[i-2]>>10); w[i] = w[i-16]+s0+w[i-7]+s1; }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int i = 0; i < 64; i++) { uint32_t s1=((e>>6)|(e<<26))^((e>>11)|(e<<21))^((e>>25)|(e<<7)); uint32_t ch=(e&f)^((~e)&g); uint32_t t1=hh+s1+ch+k[i]+w[i]; uint32_t s0=((a>>2)|(a<<30))^((a>>13)|(a<<19))^((a>>22)|(a<<10)); uint32_t maj=(a&b)^(a&c)^(b&c); uint32_t t2=s0+maj; hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2; }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    free(msg); char* out = (char*)calloc(1, 65);
    snprintf(out, 65, "%08x%08x%08x%08x%08x%08x%08x%08x", h[0],h[1],h[2],h[3],h[4],h[5],h[6],h[7]);
    return out;
}
static inline SVal sv_sha256(SVal v) { const char* s = (v.type == ST_STR && v.str) ? v.str : ""; return sv_str(__sv_sha256_raw((const unsigned char*)s, strlen(s))); }
static inline SVal sv_sha256_file(SVal p) { const char* path = (p.type == ST_STR && p.str) ? p.str : ""; FILE* f = fopen(path, "rb"); if (!f) return sv_str(""); fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); unsigned char* b = (unsigned char*)malloc((size_t)(sz > 0 ? sz : 1)); size_t r = sz > 0 ? fread(b, 1, (size_t)sz, f) : 0; fclose(f); char* hx = __sv_sha256_raw(b, r); free(b); return sv_str(hx); }
#include <time.h>
static inline SVal sv_clock(void) { struct timespec ts; timespec_get(&ts, TIME_UTC); return sv_num((double)ts.tv_sec + (double)ts.tv_nsec * 1e-9); }
static void __sv_js_app(char** buf, size_t* len, size_t* cap, const char* s) {
    size_t sl = strlen(s); while (*len + sl + 1 >= *cap) { *cap *= 2; *buf = (char*)realloc(*buf, *cap); }
    memcpy(*buf + *len, s, sl); *len += sl; (*buf)[*len] = '\0';
}
static void __sv_js_esc(char** buf, size_t* len, size_t* cap, const char* s) {
    __sv_js_app(buf, len, cap, "\"");
    for (; *s; s++) { char c = *s; if (c == '\\') __sv_js_app(buf, len, cap, "\\\\"); else if (c == '"') __sv_js_app(buf, len, cap, "\\\""); else if (c == '\n') __sv_js_app(buf, len, cap, "\\n"); else if (c == '\r') __sv_js_app(buf, len, cap, "\\r"); else if (c == '\t') __sv_js_app(buf, len, cap, "\\t"); else { char t[2] = {c, 0}; __sv_js_app(buf, len, cap, t); } }
    __sv_js_app(buf, len, cap, "\"");
}
static void __sv_js_val(char** buf, size_t* len, size_t* cap, SVal v) {
    if (v.type == ST_NULL) __sv_js_app(buf, len, cap, "null");
    else if (v.type == ST_BOOL) __sv_js_app(buf, len, cap, v.num != 0.0 ? "true" : "false");
    else if (v.type == ST_NUM) { char t[64]; snprintf(t, sizeof(t), "%g", v.num); __sv_js_app(buf, len, cap, t); }
    else if (v.type == ST_STR) __sv_js_esc(buf, len, cap, v.str ? v.str : "");
    else if (v.type == ST_ARR) { __sv_js_app(buf, len, cap, "["); for (int i = 0; i < v.arr->len; i++) { if (i > 0) __sv_js_app(buf, len, cap, ","); __sv_js_val(buf, len, cap, v.arr->data[i]); } __sv_js_app(buf, len, cap, "]"); }
    else if (v.type == ST_MAP) { __sv_js_app(buf, len, cap, "{"); for (int i = 0; i < v.map->len; i++) { if (i > 0) __sv_js_app(buf, len, cap, ","); __sv_js_esc(buf, len, cap, v.map->keys[i]); __sv_js_app(buf, len, cap, ":"); __sv_js_val(buf, len, cap, v.map->vals[i]); } __sv_js_app(buf, len, cap, "}"); }
    else __sv_js_app(buf, len, cap, "null");
}
static inline SVal sv_json_stringify(SVal v) { size_t cap = 64, len = 0; char* buf = (char*)calloc(1, cap); __sv_js_val(&buf, &len, &cap, v); return sv_str(buf); }
static void __sv_jp_ws(const char* s, size_t* i) { while (s[*i] && (s[*i]==' '||s[*i]=='\t'||s[*i]=='\n'||s[*i]=='\r')) (*i)++; }
static SVal __sv_jp_val(const char* s, size_t* i);
static char* __sv_jp_str(const char* s, size_t* i) {
    (*i)++; size_t cap = 32, len = 0; char* out = (char*)calloc(1, cap);
    while (s[*i]) { char c = s[(*i)++]; if (c == '"') break; if (c == '\\' && s[*i]) { char e = s[(*i)++]; if (e=='n') c='\n'; else if (e=='r') c='\r'; else if (e=='t') c='\t'; else c=e; } if (len + 2 >= cap) { cap *= 2; out = (char*)realloc(out, cap); } out[len++] = c; out[len] = '\0'; }
    return out;
}
static SVal __sv_jp_val(const char* s, size_t* i) {
    __sv_jp_ws(s, i); char c = s[*i]; if (!c) return sv_null();
    if (c == '"') return sv_str(__sv_jp_str(s, i));
    if (c == '[') { (*i)++; SVal arr = sv_arr_new(0, NULL); __sv_jp_ws(s, i); if (s[*i]==']') { (*i)++; return arr; } while (s[*i]) { sv_push(arr, __sv_jp_val(s, i)); __sv_jp_ws(s, i); if (s[*i]==',') { (*i)++; continue; } if (s[*i]==']') (*i)++; break; } return arr; }
    if (c == '{') { (*i)++; SVal mp = sv_map_new(); __sv_jp_ws(s, i); if (s[*i]=='}') { (*i)++; return mp; } while (s[*i]) { __sv_jp_ws(s, i); char* k = __sv_jp_str(s, i); __sv_jp_ws(s, i); if (s[*i]==':') (*i)++; sv_map_set(mp, k, __sv_jp_val(s, i)); __sv_jp_ws(s, i); if (s[*i]==',') { (*i)++; continue; } if (s[*i]=='}') (*i)++; break; } return mp; }
    if (strncmp(s + *i, "true", 4) == 0) { *i += 4; return sv_bool(true); }
    if (strncmp(s + *i, "false", 5) == 0) { *i += 5; return sv_bool(false); }
    if (strncmp(s + *i, "null", 4) == 0) { *i += 4; return sv_null(); }
    char* end = NULL; double d = strtod(s + *i, &end); if (end && end > s + *i) { *i = (size_t)(end - s); return sv_num(d); } return sv_null();
}
static inline SVal sv_json_parse(SVal v) { const char* src = (v.type == ST_STR && v.str) ? v.str : "null"; size_t i = 0; return __sv_jp_val(src, &i); }
SVal strip_v_prefix(SVal raw);
SVal __st_fn_strip_v_prefix(SVal, int, SVal*);
SVal is_all_digits(SVal s);
SVal __st_fn_is_all_digits(SVal, int, SVal*);
SVal parse_semver(SVal raw);
SVal __st_fn_parse_semver(SVal, int, SVal*);
SVal format_semver(SVal v);
SVal __st_fn_format_semver(SVal, int, SVal*);
SVal compare_prerelease(SVal a, SVal b);
SVal __st_fn_compare_prerelease(SVal, int, SVal*);
SVal compare_semver(SVal a_raw, SVal b_raw);
SVal __st_fn_compare_semver(SVal, int, SVal*);
SVal satisfies_constraint(SVal version_raw, SVal constraint_raw);
SVal __st_fn_satisfies_constraint(SVal, int, SVal*);
SVal classify_transition(SVal from_ver, SVal to_ver);
SVal __st_fn_classify_transition(SVal, int, SVal*);
SVal is_protected_system_prefix(SVal path);
SVal __st_fn_is_protected_system_prefix(SVal, int, SVal*);
SVal validate_install_path(SVal path, SVal scope);
SVal __st_fn_validate_install_path(SVal, int, SVal*);
SVal validate_archive_entry(SVal entry_path);
SVal __st_fn_validate_archive_entry(SVal, int, SVal*);
SVal validate_archive_manifest(SVal entries);
SVal __st_fn_validate_archive_manifest(SVal, int, SVal*);
SVal requires_privilege_escalation(SVal scope);
SVal __st_fn_requires_privilege_escalation(SVal, int, SVal*);
SVal is_valid_hex_string(SVal s, SVal expected_len);
SVal __st_fn_is_valid_hex_string(SVal, int, SVal*);
SVal validate_sha256(SVal digest);
SVal __st_fn_validate_sha256(SVal, int, SVal*);
SVal validate_ed25519_sig(SVal sig);
SVal __st_fn_validate_ed25519_sig(SVal, int, SVal*);
SVal verify_component_integrity(SVal entry, SVal computed_sha256, SVal sig_verified);
SVal __st_fn_verify_component_integrity(SVal, int, SVal*);
SVal is_valid_component_id(SVal id);
SVal __st_fn_is_valid_component_id(SVal, int, SVal*);
SVal validate_catalogue_entry(SVal id, SVal entry);
SVal __st_fn_validate_catalogue_entry(SVal, int, SVal*);
SVal validate_catalogue(SVal catalogue);
SVal __st_fn_validate_catalogue(SVal, int, SVal*);
SVal matches_channel(SVal entry, SVal channel);
SVal __st_fn_matches_channel(SVal, int, SVal*);
SVal search_catalogue(SVal catalogue, SVal query, SVal channel);
SVal __st_fn_search_catalogue(SVal, int, SVal*);
SVal dfs_visit(SVal id, SVal catalogue, SVal visiting, SVal visited, SVal order, SVal err_box);
SVal __st_fn_dfs_visit(SVal, int, SVal*);
SVal resolve_install_order(SVal catalogue, SVal targets);
SVal __st_fn_resolve_install_order(SVal, int, SVal*);
SVal find_reverse_dependents(SVal catalogue, SVal installed, SVal target_id);
SVal __st_fn_find_reverse_dependents(SVal, int, SVal*);
SVal check_file_conflicts(SVal ownership, SVal component_id, SVal files);
SVal __st_fn_check_file_conflicts(SVal, int, SVal*);
SVal plan_reconcile(SVal installed, SVal catalogue, SVal targets);
SVal __st_fn_plan_reconcile(SVal, int, SVal*);
SVal plan_check_updates(SVal installed, SVal catalogue, SVal channel);
SVal __st_fn_plan_check_updates(SVal, int, SVal*);
SVal plan_remove(SVal installed, SVal catalogue, SVal target_id, SVal force);
SVal __st_fn_plan_remove(SVal, int, SVal*);
SVal plan_system_snapshot_transition(SVal installed, SVal catalogue, SVal target_snapshot);
SVal __st_fn_plan_system_snapshot_transition(SVal, int, SVal*);
SVal db_default_state(void);
SVal __st_fn_db_default_state(SVal, int, SVal*);
SVal db_load_from_path(SVal db_path);
SVal __st_fn_db_load_from_path(SVal, int, SVal*);
SVal db_save_to_path(SVal db_path, SVal state);
SVal __st_fn_db_save_to_path(SVal, int, SVal*);
SVal db_apply_transaction(SVal state, SVal actions, SVal timestamp);
SVal __st_fn_db_apply_transaction(SVal, int, SVal*);
SVal db_rollback_last(SVal state, SVal timestamp);
SVal __st_fn_db_rollback_last(SVal, int, SVal*);
SVal db_set_config(SVal state, SVal key, SVal value);
SVal __st_fn_db_set_config(SVal, int, SVal*);
SVal parse_cli_flags(SVal args);
SVal __st_fn_parse_cli_flags(SVal, int, SVal*);
SVal format_help_text(void);
SVal __st_fn_format_help_text(SVal, int, SVal*);
SVal format_plan_lines(SVal plan);
SVal __st_fn_format_plan_lines(SVal, int, SVal*);
SVal cli_dispatch(SVal args, SVal state, SVal catalogue, SVal timestamp);
SVal __st_fn_cli_dispatch(SVal, int, SVal*);
SVal ui_initial_state(void);
SVal __st_fn_ui_initial_state(SVal, int, SVal*);
SVal ui_toggle_selection(SVal selected, SVal id);
SVal __st_fn_ui_toggle_selection(SVal, int, SVal*);
SVal ui_reduce(SVal ui_state, SVal event, SVal db_state, SVal catalogue);
SVal __st_fn_ui_reduce(SVal, int, SVal*);
SVal build_package_manifest(SVal id, SVal version, SVal scope, SVal install_path, SVal sha256, SVal signature, SVal files);
SVal __st_fn_build_package_manifest(SVal, int, SVal*);
SVal publish_catalogue_entry(SVal catalogue, SVal id, SVal entry);
SVal __st_fn_publish_catalogue_entry(SVal, int, SVal*);
SVal validate_helper_request(SVal req);
SVal __st_fn_validate_helper_request(SVal, int, SVal*);
SVal helper_check_dpkg_owner(SVal target_path);
SVal __st_fn_helper_check_dpkg_owner(SVal, int, SVal*);
SVal helper_backup_path(SVal backup_root, SVal tx_id, SVal component_id);
SVal __st_fn_helper_backup_path(SVal, int, SVal*);
SVal helper_create_backup(SVal dest, SVal backup_path);
SVal __st_fn_helper_create_backup(SVal, int, SVal*);
SVal helper_restore_backup(SVal dest, SVal backup_path);
SVal __st_fn_helper_restore_backup(SVal, int, SVal*);
SVal helper_execute_request(SVal req_json);
SVal __st_fn_helper_execute_request(SVal, int, SVal*);
SVal runner_atomic_write(SVal path, SVal content);
SVal __st_fn_runner_atomic_write(SVal, int, SVal*);
SVal runner_cli_main(void);
SVal __st_fn_runner_cli_main(SVal, int, SVal*);
SVal runner_publish_catalogue(SVal cat_path, SVal id, SVal entry_path);
SVal __st_fn_runner_publish_catalogue(SVal, int, SVal*);
SVal test_sample_catalogue(void);
SVal __st_fn_test_sample_catalogue(SVal, int, SVal*);
SVal test_semver_suite(void);
SVal __st_fn_test_semver_suite(SVal, int, SVal*);
SVal test_security_policy_suite(void);
SVal __st_fn_test_security_policy_suite(SVal, int, SVal*);
SVal test_solver_and_reconciler_suite(void);
SVal __st_fn_test_solver_and_reconciler_suite(SVal, int, SVal*);
SVal test_cli_db_and_ui_suite(void);
SVal __st_fn_test_cli_db_and_ui_suite(SVal, int, SVal*);
SVal test_helper_policy_suite(void);
SVal __st_fn_test_helper_policy_suite(SVal, int, SVal*);
SVal run_all_steel_self_tests(void);
SVal __st_fn_run_all_steel_self_tests(SVal, int, SVal*);
SVal steel_compare_semver(SVal a, SVal b);
SVal __st_fn_steel_compare_semver(SVal, int, SVal*);
SVal steel_satisfies_constraint(SVal ver, SVal constraint);
SVal __st_fn_steel_satisfies_constraint(SVal, int, SVal*);
SVal steel_validate_path_json(SVal path, SVal scope);
SVal __st_fn_steel_validate_path_json(SVal, int, SVal*);
SVal steel_validate_archive_json(SVal entries_json);
SVal __st_fn_steel_validate_archive_json(SVal, int, SVal*);
SVal steel_validate_catalogue_json(SVal catalogue_json);
SVal __st_fn_steel_validate_catalogue_json(SVal, int, SVal*);
SVal steel_plan_reconcile(SVal installed_json, SVal catalogue_json, SVal targets_json);
SVal __st_fn_steel_plan_reconcile(SVal, int, SVal*);
SVal steel_plan_remove(SVal installed_json, SVal catalogue_json, SVal target_id, SVal force);
SVal __st_fn_steel_plan_remove(SVal, int, SVal*);
SVal steel_cli_dispatch(SVal args_json, SVal state_json, SVal catalogue_json, SVal timestamp);
SVal __st_fn_steel_cli_dispatch(SVal, int, SVal*);
SVal steel_ui_reduce(SVal ui_json, SVal event_json, SVal state_json, SVal catalogue_json);
SVal __st_fn_steel_ui_reduce(SVal, int, SVal*);
SVal steel_build_manifest_json(SVal id, SVal version, SVal scope, SVal install_path, SVal sha256, SVal signature, SVal files_json);
SVal __st_fn_steel_build_manifest_json(SVal, int, SVal*);
SVal steel_publish_entry_json(SVal catalogue_json, SVal id, SVal entry_json);
SVal __st_fn_steel_publish_entry_json(SVal, int, SVal*);
SVal steel_validate_helper_request_json(SVal req_json);
SVal __st_fn_steel_validate_helper_request_json(SVal, int, SVal*);
SVal steel_helper_run_json(SVal req_json);
SVal __st_fn_steel_helper_run_json(SVal, int, SVal*);
SVal steel_runner_cli_json(void);
SVal __st_fn_steel_runner_cli_json(SVal, int, SVal*);
SVal steel_runner_publish_json(SVal cat_path, SVal id, SVal entry_path);
SVal __st_fn_steel_runner_publish_json(SVal, int, SVal*);
SVal steel_plan_snapshot_json(SVal installed_json, SVal catalogue_json, SVal snapshot_json);
SVal __st_fn_steel_plan_snapshot_json(SVal, int, SVal*);
SVal steel_run_self_tests(void);
SVal __st_fn_steel_run_self_tests(SVal, int, SVal*);
SVal strip_v_prefix(SVal raw) {
    SVal s = sv_trim(raw);
    if ((sv_truthy(sv_starts_with(s, sv_str("v"))) || sv_truthy(sv_starts_with(s, sv_str("V"))))) {
        return sv_slice(s, sv_num(1), sv_len(s));
    }
    return s;
}
SVal __st_fn_strip_v_prefix(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return strip_v_prefix((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal is_all_digits(SVal s) {
    if (sv_eq(sv_len(s), sv_num(0))) {
        return sv_bool(false);
    }
    SVal i = sv_num(0);
    while ((i.num < (sv_len(s)).num)) {
        SVal ch = sv_slice(s, i, sv_add(i, sv_num(1)));
        SVal code = sv_char_code(ch);
        if (((code.num < 48) || (code.num > 57))) {
            return sv_bool(false);
        }
        i = sv_add(i, sv_num(1));
    }
    return sv_bool(true);
}
SVal __st_fn_is_all_digits(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return is_all_digits((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal parse_semver(SVal raw) {
    SVal s = strip_v_prefix(raw);
    SVal plus_idx = sv_index_of(s, sv_str("+"));
    SVal build = sv_str("");
    if ((plus_idx.num >= 0)) {
        build = sv_slice(s, sv_add(plus_idx, sv_num(1)), sv_len(s));
        s = sv_slice(s, sv_num(0), plus_idx);
    }
    SVal dash_idx = sv_index_of(s, sv_str("-"));
    SVal pre = sv_str("");
    if ((dash_idx.num >= 0)) {
        pre = sv_slice(s, sv_add(dash_idx, sv_num(1)), sv_len(s));
        s = sv_slice(s, sv_num(0), dash_idx);
    }
    SVal parts = sv_split(s, sv_str("."));
    if ((((sv_len(parts)).num < 2) || ((sv_len(parts)).num > 3))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "valid", sv_bool(false)); sv_map_set(__m, "major", sv_num(0)); sv_map_set(__m, "minor", sv_num(0)); sv_map_set(__m, "patch", sv_num(0)); sv_map_set(__m, "pre", sv_str("")); sv_map_set(__m, "build", sv_str("")); __m; });
    }
    SVal maj_s = sv_get(parts, sv_num(0));
    SVal min_s = sv_get(parts, sv_num(1));
    SVal pat_s = sv_str("0");
    if (sv_eq(sv_len(parts), sv_num(3))) {
        pat_s = sv_get(parts, sv_num(2));
    }
    if ((((!sv_truthy(is_all_digits(maj_s))) || (!sv_truthy(is_all_digits(min_s)))) || (!sv_truthy(is_all_digits(pat_s))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "valid", sv_bool(false)); sv_map_set(__m, "major", sv_num(0)); sv_map_set(__m, "minor", sv_num(0)); sv_map_set(__m, "patch", sv_num(0)); sv_map_set(__m, "pre", sv_str("")); sv_map_set(__m, "build", sv_str("")); __m; });
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "valid", sv_bool(true)); sv_map_set(__m, "major", sv_int_cast(sv_num_cast(maj_s))); sv_map_set(__m, "minor", sv_int_cast(sv_num_cast(min_s))); sv_map_set(__m, "patch", sv_int_cast(sv_num_cast(pat_s))); sv_map_set(__m, "pre", pre); sv_map_set(__m, "build", build); __m; });
}
SVal __st_fn_parse_semver(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return parse_semver((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal format_semver(SVal v) {
    SVal base = sv_add(sv_add(sv_add(sv_add(sv_str_cast(sv_get(v, sv_str("major"))), sv_str(".")), sv_str_cast(sv_get(v, sv_str("minor")))), sv_str(".")), sv_str_cast(sv_get(v, sv_str("patch"))));
    if (((sv_len(sv_get(v, sv_str("pre")))).num > 0)) {
        base = sv_add(sv_add(base, sv_str("-")), sv_get(v, sv_str("pre")));
    }
    if (((sv_len(sv_get(v, sv_str("build")))).num > 0)) {
        base = sv_add(sv_add(base, sv_str("+")), sv_get(v, sv_str("build")));
    }
    return base;
}
SVal __st_fn_format_semver(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return format_semver((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal compare_prerelease(SVal a, SVal b) {
    if (sv_eq(a, b)) {
        return sv_num(0);
    }
    if (sv_eq(sv_len(a), sv_num(0))) {
        return sv_num(1);
    }
    if (sv_eq(sv_len(b), sv_num(0))) {
        return sv_num(-1);
    }
    SVal pa = sv_split(a, sv_str("."));
    SVal pb = sv_split(b, sv_str("."));
    SVal limit = sv_len(pa);
    if (((sv_len(pb)).num < limit.num)) {
        limit = sv_len(pb);
    }
    SVal i = sv_num(0);
    while ((i.num < limit.num)) {
        SVal xa = sv_get(pa, i);
        SVal xb = sv_get(pb, i);
        if ((!sv_eq(xa, xb))) {
            SVal na = is_all_digits(xa);
            SVal nb = is_all_digits(xb);
            if ((sv_truthy(na) && sv_truthy(nb))) {
                SVal va = sv_num_cast(xa);
                SVal vb = sv_num_cast(xb);
                if ((va.num < vb.num)) {
                    return sv_num(-1);
                }
                if ((va.num > vb.num)) {
                    return sv_num(1);
                }
            } else {
                if ((sv_truthy(na) && (!sv_truthy(nb)))) {
                    return sv_num(-1);
                } else {
                    if (((!sv_truthy(na)) && sv_truthy(nb))) {
                        return sv_num(1);
                    } else {
                        if ((xa.num < xb.num)) {
                            return sv_num(-1);
                        }
                        return sv_num(1);
                    }
                }
            }
        }
        i = sv_add(i, sv_num(1));
    }
    if (((sv_len(pa)).num < (sv_len(pb)).num)) {
        return sv_num(-1);
    }
    if (((sv_len(pa)).num > (sv_len(pb)).num)) {
        return sv_num(1);
    }
    return sv_num(0);
}
SVal __st_fn_compare_prerelease(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return compare_prerelease((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal compare_semver(SVal a_raw, SVal b_raw) {
    SVal a = parse_semver(a_raw);
    SVal b = parse_semver(b_raw);
    if ((!sv_eq(sv_get(a, sv_str("major")), sv_get(b, sv_str("major"))))) {
        if (((sv_get(a, sv_str("major"))).num < (sv_get(b, sv_str("major"))).num)) {
            return sv_num(-1);
        }
        return sv_num(1);
    }
    if ((!sv_eq(sv_get(a, sv_str("minor")), sv_get(b, sv_str("minor"))))) {
        if (((sv_get(a, sv_str("minor"))).num < (sv_get(b, sv_str("minor"))).num)) {
            return sv_num(-1);
        }
        return sv_num(1);
    }
    if ((!sv_eq(sv_get(a, sv_str("patch")), sv_get(b, sv_str("patch"))))) {
        if (((sv_get(a, sv_str("patch"))).num < (sv_get(b, sv_str("patch"))).num)) {
            return sv_num(-1);
        }
        return sv_num(1);
    }
    return compare_prerelease(sv_get(a, sv_str("pre")), sv_get(b, sv_str("pre")));
}
SVal __st_fn_compare_semver(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return compare_semver((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal satisfies_constraint(SVal version_raw, SVal constraint_raw) {
    SVal c = sv_trim(constraint_raw);
    if ((sv_eq(sv_len(c), sv_num(0)) || sv_eq(c, sv_str("*")))) {
        return sv_bool(true);
    }
    SVal v = parse_semver(version_raw);
    if ((!sv_truthy(sv_get(v, sv_str("valid"))))) {
        return sv_bool(false);
    }
    if (sv_truthy(sv_starts_with(c, sv_str(">=")))) {
        return sv_bool(((compare_semver(version_raw, sv_trim(sv_slice(c, sv_num(2), sv_len(c))))).num >= 0));
    }
    if (sv_truthy(sv_starts_with(c, sv_str("<=")))) {
        return sv_bool(((compare_semver(version_raw, sv_trim(sv_slice(c, sv_num(2), sv_len(c))))).num <= 0));
    }
    if (sv_truthy(sv_starts_with(c, sv_str(">")))) {
        return sv_bool(((compare_semver(version_raw, sv_trim(sv_slice(c, sv_num(1), sv_len(c))))).num > 0));
    }
    if (sv_truthy(sv_starts_with(c, sv_str("<")))) {
        return sv_bool(((compare_semver(version_raw, sv_trim(sv_slice(c, sv_num(1), sv_len(c))))).num < 0));
    }
    if (sv_truthy(sv_starts_with(c, sv_str("=")))) {
        return sv_bool(sv_eq(compare_semver(version_raw, sv_trim(sv_slice(c, sv_num(1), sv_len(c)))), sv_num(0)));
    }
    if (sv_truthy(sv_starts_with(c, sv_str("^")))) {
        SVal base_s = sv_trim(sv_slice(c, sv_num(1), sv_len(c)));
        SVal base = parse_semver(base_s);
        if ((!sv_truthy(sv_get(base, sv_str("valid"))))) {
            return sv_bool(false);
        }
        if (((compare_semver(version_raw, base_s)).num < 0)) {
            return sv_bool(false);
        }
        if (((sv_get(base, sv_str("major"))).num > 0)) {
            return sv_bool(sv_eq(sv_get(v, sv_str("major")), sv_get(base, sv_str("major"))));
        }
        if (((sv_get(base, sv_str("minor"))).num > 0)) {
            return sv_bool((sv_eq(sv_get(v, sv_str("major")), sv_num(0)) && sv_eq(sv_get(v, sv_str("minor")), sv_get(base, sv_str("minor")))));
        }
        return sv_bool(((sv_eq(sv_get(v, sv_str("major")), sv_num(0)) && sv_eq(sv_get(v, sv_str("minor")), sv_num(0))) && sv_eq(sv_get(v, sv_str("patch")), sv_get(base, sv_str("patch")))));
    }
    if (sv_truthy(sv_starts_with(c, sv_str("~")))) {
        SVal base_s = sv_trim(sv_slice(c, sv_num(1), sv_len(c)));
        SVal base = parse_semver(base_s);
        if ((!sv_truthy(sv_get(base, sv_str("valid"))))) {
            return sv_bool(false);
        }
        if (((compare_semver(version_raw, base_s)).num < 0)) {
            return sv_bool(false);
        }
        return sv_bool((sv_eq(sv_get(v, sv_str("major")), sv_get(base, sv_str("major"))) && sv_eq(sv_get(v, sv_str("minor")), sv_get(base, sv_str("minor")))));
    }
    return sv_bool(((compare_semver(version_raw, c)).num >= 0));
}
SVal __st_fn_satisfies_constraint(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return satisfies_constraint((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal classify_transition(SVal from_ver, SVal to_ver) {
    if ((sv_eq(from_ver, sv_null()) || sv_eq(sv_len(sv_str_cast(from_ver)), sv_num(0)))) {
        return sv_str("install");
    }
    SVal cmp = compare_semver(from_ver, to_ver);
    if ((cmp.num < 0)) {
        return sv_str("upgrade");
    }
    if ((cmp.num > 0)) {
        return sv_str("downgrade");
    }
    return sv_str("noop");
}
SVal __st_fn_classify_transition(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return classify_transition((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal is_protected_system_prefix(SVal path) {
    SVal blocked = sv_arr_new(13, (SVal[]){sv_str("/etc"), sv_str("/boot"), sv_str("/proc"), sv_str("/sys"), sv_str("/dev"), sv_str("/bin"), sv_str("/sbin"), sv_str("/lib"), sv_str("/lib64"), sv_str("/root"), sv_str("/var/lib/dpkg"), sv_str("/var/lib/apt"), sv_str("/usr/bin/sudo")});
    SVal i = sv_num(0);
    while ((i.num < (sv_len(blocked)).num)) {
        SVal prefix = sv_get(blocked, i);
        if ((sv_eq(path, prefix) || sv_truthy(sv_starts_with(path, sv_add(prefix, sv_str("/")))))) {
            return sv_bool(true);
        }
        i = sv_add(i, sv_num(1));
    }
    return sv_bool(false);
}
SVal __st_fn_is_protected_system_prefix(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return is_protected_system_prefix((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal validate_install_path(SVal path, SVal scope) {
    if ((sv_eq(path, sv_null()) || sv_eq(sv_len(path), sv_num(0)))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("install path must not be empty")); __m; });
    }
    if (sv_truthy(sv_contains(path, sv_str("//")))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("duplicate slash in install path")); __m; });
    }
    if (sv_truthy(sv_contains(path, sv_str("\\")))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("backslash not allowed in unix install path")); __m; });
    }
    SVal segs = sv_split(path, sv_str("/"));
    SVal i = sv_num(0);
    while ((i.num < (sv_len(segs)).num)) {
        if ((sv_eq(sv_get(segs, i), sv_str("..")) || sv_eq(sv_get(segs, i), sv_str(".")))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("path traversal segment not allowed")); __m; });
        }
        i = sv_add(i, sv_num(1));
    }
    if (sv_truthy(is_protected_system_prefix(path))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("protected system or dpkg directory")); __m; });
    }
    if (sv_eq(scope, sv_str("system"))) {
        if (((!sv_truthy(sv_starts_with(path, sv_str("/usr/share/")))) && (!sv_truthy(sv_starts_with(path, sv_str("/opt/infinityos/")))))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("system scope must target /usr/share/ or /opt/infinityos/")); __m; });
        }
        if (((sv_len(segs)).num < 4)) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("system install path is too shallow")); __m; });
        }
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "reason", sv_str("")); __m; });
    }
    if (sv_eq(scope, sv_str("user"))) {
        if (((!sv_truthy(sv_starts_with(path, sv_str("~/.local/share/")))) && (!sv_truthy(sv_starts_with(path, sv_str("/home/")))))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("user scope must target ~/.local/share/ or /home/")); __m; });
        }
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "reason", sv_str("")); __m; });
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(sv_str("unknown install scope: "), sv_str_cast(scope))); __m; });
}
SVal __st_fn_validate_install_path(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return validate_install_path((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal validate_archive_entry(SVal entry_path) {
    if ((sv_eq(entry_path, sv_null()) || sv_eq(sv_len(entry_path), sv_num(0)))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("empty archive entry")); __m; });
    }
    if (sv_truthy(sv_starts_with(entry_path, sv_str("/")))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(sv_str("absolute path in archive: "), entry_path)); __m; });
    }
    if (sv_truthy(sv_contains(entry_path, sv_str("\\")))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(sv_str("backslash in archive entry: "), entry_path)); __m; });
    }
    SVal segs = sv_split(entry_path, sv_str("/"));
    SVal i = sv_num(0);
    while ((i.num < (sv_len(segs)).num)) {
        if (sv_eq(sv_get(segs, i), sv_str(".."))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(sv_str("zip-slip traversal in archive: "), entry_path)); __m; });
        }
        i = sv_add(i, sv_num(1));
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "reason", sv_str("")); __m; });
}
SVal __st_fn_validate_archive_entry(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return validate_archive_entry((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal validate_archive_manifest(SVal entries) {
    SVal i = sv_num(0);
    while ((i.num < (sv_len(entries)).num)) {
        SVal chk = validate_archive_entry(sv_get(entries, i));
        if ((!sv_truthy(sv_get(chk, sv_str("ok"))))) {
            return chk;
        }
        i = sv_add(i, sv_num(1));
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "reason", sv_str("")); __m; });
}
SVal __st_fn_validate_archive_manifest(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return validate_archive_manifest((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal requires_privilege_escalation(SVal scope) {
    return sv_bool(sv_eq(scope, sv_str("system")));
}
SVal __st_fn_requires_privilege_escalation(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return requires_privilege_escalation((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal is_valid_hex_string(SVal s, SVal expected_len) {
    if ((sv_eq(s, sv_null()) || (!sv_eq(sv_len(s), expected_len)))) {
        return sv_bool(false);
    }
    SVal low = sv_lower(s);
    SVal i = sv_num(0);
    while ((i.num < (sv_len(low)).num)) {
        SVal ch = sv_slice(low, i, sv_add(i, sv_num(1)));
        SVal code = sv_char_code(ch);
        SVal is_digit = sv_bool(((code.num >= 48) && (code.num <= 57)));
        SVal is_hex = sv_bool(((code.num >= 97) && (code.num <= 102)));
        if (((!sv_truthy(is_digit)) && (!sv_truthy(is_hex)))) {
            return sv_bool(false);
        }
        i = sv_add(i, sv_num(1));
    }
    return sv_bool(true);
}
SVal __st_fn_is_valid_hex_string(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return is_valid_hex_string((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal validate_sha256(SVal digest) {
    return is_valid_hex_string(digest, sv_num(64));
}
SVal __st_fn_validate_sha256(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return validate_sha256((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal validate_ed25519_sig(SVal sig) {
    return is_valid_hex_string(sig, sv_num(128));
}
SVal __st_fn_validate_ed25519_sig(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return validate_ed25519_sig((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal verify_component_integrity(SVal entry, SVal computed_sha256, SVal sig_verified) {
    SVal expected = sv_get(entry, sv_str("sha256"));
    if (((!sv_eq(expected, sv_null())) && ((sv_len(sv_str_cast(expected))).num > 0))) {
        if ((!sv_truthy(validate_sha256(sv_str_cast(expected))))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("malformed expected sha256 digest")); __m; });
        }
        if ((!sv_eq(sv_lower(sv_str_cast(computed_sha256)), sv_lower(sv_str_cast(expected))))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(sv_add(sv_add(sv_str("sha256 mismatch: expected "), sv_str_cast(expected)), sv_str(", got ")), sv_str_cast(computed_sha256))); __m; });
        }
    }
    if ((sv_truthy(sv_has_key(entry, sv_str("signature"))) && ((sv_len(sv_str_cast(sv_get(entry, sv_str("signature"))))).num > 0))) {
        if ((!sv_truthy(sig_verified))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("ed25519 signature verification failed")); __m; });
        }
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "reason", sv_str("")); __m; });
}
SVal __st_fn_verify_component_integrity(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return verify_component_integrity((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal is_valid_component_id(SVal id) {
    if (((sv_eq(id, sv_null()) || ((sv_len(id)).num < 2)) || ((sv_len(id)).num > 64))) {
        return sv_bool(false);
    }
    SVal i = sv_num(0);
    while ((i.num < (sv_len(id)).num)) {
        SVal c = sv_char_code(sv_slice(id, i, sv_add(i, sv_num(1))));
        SVal ok_low = sv_bool(((c.num >= 97) && (c.num <= 122)));
        SVal ok_dig = sv_bool(((c.num >= 48) && (c.num <= 57)));
        SVal ok_sym = sv_bool(((sv_eq(c, sv_num(45)) || sv_eq(c, sv_num(95))) || sv_eq(c, sv_num(46))));
        if ((((!sv_truthy(ok_low)) && (!sv_truthy(ok_dig))) && (!sv_truthy(ok_sym)))) {
            return sv_bool(false);
        }
        i = sv_add(i, sv_num(1));
    }
    return sv_bool(true);
}
SVal __st_fn_is_valid_component_id(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return is_valid_component_id((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal validate_catalogue_entry(SVal id, SVal entry) {
    if ((!sv_truthy(is_valid_component_id(id)))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(sv_str("invalid component id: "), sv_str_cast(id))); __m; });
    }
    if ((!sv_truthy(sv_has_key(entry, sv_str("version"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(id, sv_str(": missing version"))); __m; });
    }
    SVal v = parse_semver(sv_get(entry, sv_str("version")));
    if ((!sv_truthy(sv_get(v, sv_str("valid"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(sv_add(id, sv_str(": invalid semver: ")), sv_str_cast(sv_get(entry, sv_str("version"))))); __m; });
    }
    if ((!sv_truthy(sv_has_key(entry, sv_str("install_path"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(id, sv_str(": missing install_path"))); __m; });
    }
    SVal scope = sv_str("system");
    if (((sv_truthy(sv_has_key(entry, sv_str("scope"))) && (!sv_eq(sv_get(entry, sv_str("scope")), sv_null()))) && ((sv_len(sv_str_cast(sv_get(entry, sv_str("scope"))))).num > 0))) {
        scope = sv_get(entry, sv_str("scope"));
    }
    SVal pchk = validate_install_path(sv_get(entry, sv_str("install_path")), scope);
    if ((!sv_truthy(sv_get(pchk, sv_str("ok"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(sv_add(id, sv_str(": ")), sv_get(pchk, sv_str("reason")))); __m; });
    }
    if (((sv_truthy(sv_has_key(entry, sv_str("sha256"))) && (!sv_eq(sv_get(entry, sv_str("sha256")), sv_null()))) && ((sv_len(sv_str_cast(sv_get(entry, sv_str("sha256"))))).num > 0))) {
        if ((!sv_truthy(validate_sha256(sv_str_cast(sv_get(entry, sv_str("sha256"))))))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(id, sv_str(": invalid sha256 hex"))); __m; });
        }
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "reason", sv_str("")); __m; });
}
SVal __st_fn_validate_catalogue_entry(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return validate_catalogue_entry((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal validate_catalogue(SVal catalogue) {
    SVal ks = sv_keys(catalogue);
    SVal errors = sv_arr_new(0, NULL);
    SVal i = sv_num(0);
    while ((i.num < (sv_len(ks)).num)) {
        SVal id = sv_get(ks, i);
        SVal chk = validate_catalogue_entry(id, sv_get(catalogue, id));
        if ((!sv_truthy(sv_get(chk, sv_str("ok"))))) {
            sv_push(errors, sv_get(chk, sv_str("reason")));
        }
        i = sv_add(i, sv_num(1));
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(sv_eq(sv_len(errors), sv_num(0)))); sv_map_set(__m, "count", sv_len(ks)); sv_map_set(__m, "errors", errors); __m; });
}
SVal __st_fn_validate_catalogue(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return validate_catalogue((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal matches_channel(SVal entry, SVal channel) {
    if (((sv_eq(channel, sv_null()) || sv_eq(sv_len(channel), sv_num(0))) || sv_eq(channel, sv_str("all")))) {
        return sv_bool(true);
    }
    if ((((!sv_truthy(sv_has_key(entry, sv_str("channel")))) || sv_eq(sv_get(entry, sv_str("channel")), sv_null())) || sv_eq(sv_len(sv_str_cast(sv_get(entry, sv_str("channel")))), sv_num(0)))) {
        return sv_bool(sv_eq(channel, sv_str("stable")));
    }
    SVal ec = sv_get(entry, sv_str("channel"));
    if (sv_eq(channel, sv_str("nightly"))) {
        return sv_bool(true);
    }
    if (sv_eq(channel, sv_str("beta"))) {
        return sv_bool((sv_eq(ec, sv_str("stable")) || sv_eq(ec, sv_str("beta"))));
    }
    return sv_bool(sv_eq(ec, sv_str("stable")));
}
SVal __st_fn_matches_channel(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return matches_channel((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal search_catalogue(SVal catalogue, SVal query, SVal channel) {
    SVal q = sv_lower(sv_trim(query));
    SVal ks = sv_sort(sv_keys(catalogue));
    SVal matches = sv_arr_new(0, NULL);
    SVal i = sv_num(0);
    while ((i.num < (sv_len(ks)).num)) {
        SVal id = sv_get(ks, i);
        SVal entry = sv_get(catalogue, id);
        if (sv_truthy(matches_channel(entry, channel))) {
            SVal desc = sv_str("");
            if ((sv_truthy(sv_has_key(entry, sv_str("description"))) && (!sv_eq(sv_get(entry, sv_str("description")), sv_null())))) {
                desc = sv_lower(sv_str_cast(sv_get(entry, sv_str("description"))));
            }
            SVal cat = sv_str("");
            if ((sv_truthy(sv_has_key(entry, sv_str("category"))) && (!sv_eq(sv_get(entry, sv_str("category")), sv_null())))) {
                cat = sv_lower(sv_str_cast(sv_get(entry, sv_str("category"))));
            }
            if ((((sv_eq(sv_len(q), sv_num(0)) || sv_truthy(sv_contains(sv_lower(id), q))) || sv_truthy(sv_contains(desc, q))) || sv_truthy(sv_contains(cat, q)))) {
                sv_push(matches, ({ SVal __m = sv_map_new(); sv_map_set(__m, "id", id); sv_map_set(__m, "version", sv_get(entry, sv_str("version"))); sv_map_set(__m, "scope", (sv_truthy(sv_has_key(entry, sv_str("scope"))) ? (sv_get(entry, sv_str("scope"))) : (sv_str("system")))); sv_map_set(__m, "description", (sv_truthy(sv_has_key(entry, sv_str("description"))) ? (sv_get(entry, sv_str("description"))) : (sv_str("")))); sv_map_set(__m, "category", (sv_truthy(sv_has_key(entry, sv_str("category"))) ? (sv_get(entry, sv_str("category"))) : (sv_str("general")))); __m; }));
            }
        }
        i = sv_add(i, sv_num(1));
    }
    return matches;
}
SVal __st_fn_search_catalogue(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return search_catalogue((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal dfs_visit(SVal id, SVal catalogue, SVal visiting, SVal visited, SVal order, SVal err_box) {
    if (((sv_len(sv_get(err_box, sv_str("msg")))).num > 0)) {
        return sv_null();
    }
    if (sv_truthy(sv_has_key(visited, id))) {
        return sv_null();
    }
    if (sv_truthy(sv_has_key(visiting, id))) {
        sv_set(err_box, sv_str("msg"), sv_add(sv_str("dependency cycle detected at "), id));
        return sv_null();
    }
    if ((!sv_truthy(sv_has_key(catalogue, id)))) {
        sv_set(err_box, sv_str("msg"), sv_add(sv_str("unknown component: "), id));
        return sv_null();
    }
    sv_set(visiting, id, sv_bool(true));
    SVal entry = sv_get(catalogue, id);
    if ((sv_truthy(sv_has_key(entry, sv_str("depends"))) && (!sv_eq(sv_get(entry, sv_str("depends")), sv_null())))) {
        SVal deps = sv_get(entry, sv_str("depends"));
        SVal i = sv_num(0);
        while ((i.num < (sv_len(deps)).num)) {
            SVal dep = sv_get(deps, i);
            SVal dep_id = sv_get(dep, sv_str("id"));
            if ((!sv_truthy(sv_has_key(catalogue, dep_id)))) {
                sv_set(err_box, sv_str("msg"), sv_add(sv_add(id, sv_str(" depends on missing component: ")), dep_id));
                return sv_null();
            }
            if (((sv_truthy(sv_has_key(dep, sv_str("min_version"))) && (!sv_eq(sv_get(dep, sv_str("min_version")), sv_null()))) && ((sv_len(sv_str_cast(sv_get(dep, sv_str("min_version"))))).num > 0))) {
                SVal avail = sv_get(sv_get(catalogue, dep_id), sv_str("version"));
                if ((!sv_truthy(satisfies_constraint(avail, sv_str_cast(sv_get(dep, sv_str("min_version"))))))) {
                    sv_set(err_box, sv_str("msg"), sv_add(sv_add(sv_add(sv_add(sv_add(sv_add(id, sv_str(" requires ")), dep_id), sv_str(" ")), sv_str_cast(sv_get(dep, sv_str("min_version")))), sv_str(" but catalogue has ")), sv_str_cast(avail)));
                    return sv_null();
                }
            }
            dfs_visit(dep_id, catalogue, visiting, visited, order, err_box);
            if (((sv_len(sv_get(err_box, sv_str("msg")))).num > 0)) {
                return sv_null();
            }
            i = sv_add(i, sv_num(1));
        }
    }
    sv_delete_key(visiting, id);
    sv_set(visited, id, sv_bool(true));
    sv_push(order, id);
    return sv_null();
}
SVal __st_fn_dfs_visit(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return dfs_visit((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null()), (_st_argc > 3 ? _st_argv[3] : sv_null()), (_st_argc > 4 ? _st_argv[4] : sv_null()), (_st_argc > 5 ? _st_argv[5] : sv_null())); }

SVal resolve_install_order(SVal catalogue, SVal targets) {
    SVal visiting = sv_map_new();
    SVal visited = sv_map_new();
    SVal order = sv_arr_new(0, NULL);
    SVal err_box = ({ SVal __m = sv_map_new(); sv_map_set(__m, "msg", sv_str("")); __m; });
    SVal i = sv_num(0);
    while ((i.num < (sv_len(targets)).num)) {
        dfs_visit(sv_get(targets, i), catalogue, visiting, visited, order, err_box);
        if (((sv_len(sv_get(err_box, sv_str("msg")))).num > 0)) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "order", sv_arr_new(0, NULL)); sv_map_set(__m, "error", sv_get(err_box, sv_str("msg"))); __m; });
        }
        i = sv_add(i, sv_num(1));
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "order", order); sv_map_set(__m, "error", sv_str("")); __m; });
}
SVal __st_fn_resolve_install_order(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return resolve_install_order((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal find_reverse_dependents(SVal catalogue, SVal installed, SVal target_id) {
    SVal dependents = sv_arr_new(0, NULL);
    SVal ks = sv_sort(sv_keys(installed));
    SVal i = sv_num(0);
    while ((i.num < (sv_len(ks)).num)) {
        SVal id = sv_get(ks, i);
        if (((!sv_eq(id, target_id)) && sv_truthy(sv_has_key(catalogue, id)))) {
            SVal entry = sv_get(catalogue, id);
            if ((sv_truthy(sv_has_key(entry, sv_str("depends"))) && (!sv_eq(sv_get(entry, sv_str("depends")), sv_null())))) {
                SVal deps = sv_get(entry, sv_str("depends"));
                SVal j = sv_num(0);
                while ((j.num < (sv_len(deps)).num)) {
                    if (sv_eq(sv_get(sv_get(deps, j), sv_str("id")), target_id)) {
                        sv_push(dependents, id);
                    }
                    j = sv_add(j, sv_num(1));
                }
            }
        }
        i = sv_add(i, sv_num(1));
    }
    return dependents;
}
SVal __st_fn_find_reverse_dependents(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return find_reverse_dependents((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal check_file_conflicts(SVal ownership, SVal component_id, SVal files) {
    if ((sv_eq(ownership, sv_null()) || sv_eq(files, sv_null()))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "conflict", sv_str("")); __m; });
    }
    SVal i = sv_num(0);
    while ((i.num < (sv_len(files)).num)) {
        SVal f = sv_get(files, i);
        if ((sv_truthy(sv_has_key(ownership, f)) && (!sv_eq(sv_get(ownership, f), component_id)))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "conflict", sv_add(sv_add(sv_add(sv_str("file "), f), sv_str(" is already owned by ")), sv_str_cast(sv_get(ownership, f)))); __m; });
        }
        i = sv_add(i, sv_num(1));
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "conflict", sv_str("")); __m; });
}
SVal __st_fn_check_file_conflicts(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return check_file_conflicts((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal plan_reconcile(SVal installed, SVal catalogue, SVal targets) {
    SVal resolved = resolve_install_order(catalogue, targets);
    if ((!sv_truthy(sv_get(resolved, sv_str("ok"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "skipped", sv_arr_new(0, NULL)); sv_map_set(__m, "requires_root", sv_bool(false)); sv_map_set(__m, "error", sv_get(resolved, sv_str("error"))); __m; });
    }
    SVal actions = sv_arr_new(0, NULL);
    SVal skipped = sv_arr_new(0, NULL);
    SVal needs_root = sv_bool(false);
    SVal order = sv_get(resolved, sv_str("order"));
    SVal i = sv_num(0);
    while ((i.num < (sv_len(order)).num)) {
        SVal id = sv_get(order, i);
        SVal entry = sv_get(catalogue, id);
        SVal scope = sv_str("system");
        if (((sv_truthy(sv_has_key(entry, sv_str("scope"))) && (!sv_eq(sv_get(entry, sv_str("scope")), sv_null()))) && ((sv_len(sv_str_cast(sv_get(entry, sv_str("scope"))))).num > 0))) {
            scope = sv_get(entry, sv_str("scope"));
        }
        SVal pchk = validate_install_path(sv_get(entry, sv_str("install_path")), scope);
        if ((!sv_truthy(sv_get(pchk, sv_str("ok"))))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "skipped", sv_arr_new(0, NULL)); sv_map_set(__m, "requires_root", sv_bool(false)); sv_map_set(__m, "error", sv_add(sv_add(id, sv_str(": ")), sv_get(pchk, sv_str("reason")))); __m; });
        }
        SVal cur = sv_str("");
        if ((sv_truthy(sv_has_key(installed, id)) && (!sv_eq(sv_get(installed, id), sv_null())))) {
            cur = sv_str_cast(sv_get(installed, id));
        }
        SVal target_ver = sv_str_cast(sv_get(entry, sv_str("version")));
        SVal kind = classify_transition(cur, target_ver);
        if (sv_eq(kind, sv_str("noop"))) {
            sv_push(skipped, id);
        } else {
            if (sv_truthy(requires_privilege_escalation(scope))) {
                needs_root = sv_bool(true);
            }
            sv_push(actions, ({ SVal __m = sv_map_new(); sv_map_set(__m, "id", id); sv_map_set(__m, "action", kind); sv_map_set(__m, "from", cur); sv_map_set(__m, "to", target_ver); sv_map_set(__m, "scope", scope); sv_map_set(__m, "install_path", sv_get(entry, sv_str("install_path"))); sv_map_set(__m, "url", (sv_truthy(sv_has_key(entry, sv_str("url"))) ? (sv_get(entry, sv_str("url"))) : (sv_str("")))); sv_map_set(__m, "sha256", (sv_truthy(sv_has_key(entry, sv_str("sha256"))) ? (sv_get(entry, sv_str("sha256"))) : (sv_str("")))); __m; }));
        }
        i = sv_add(i, sv_num(1));
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "actions", actions); sv_map_set(__m, "skipped", skipped); sv_map_set(__m, "requires_root", needs_root); sv_map_set(__m, "error", sv_str("")); __m; });
}
SVal __st_fn_plan_reconcile(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return plan_reconcile((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal plan_check_updates(SVal installed, SVal catalogue, SVal channel) {
    SVal updates = sv_arr_new(0, NULL);
    SVal up_to_date = sv_arr_new(0, NULL);
    SVal ks = sv_sort(sv_keys(installed));
    SVal i = sv_num(0);
    while ((i.num < (sv_len(ks)).num)) {
        SVal id = sv_get(ks, i);
        if (sv_truthy(sv_has_key(catalogue, id))) {
            SVal entry = sv_get(catalogue, id);
            if (sv_truthy(matches_channel(entry, channel))) {
                SVal cur = sv_str_cast(sv_get(installed, id));
                SVal latest = sv_str_cast(sv_get(entry, sv_str("version")));
                if (((compare_semver(cur, latest)).num < 0)) {
                    sv_push(updates, ({ SVal __m = sv_map_new(); sv_map_set(__m, "id", id); sv_map_set(__m, "from", cur); sv_map_set(__m, "to", latest); sv_map_set(__m, "scope", (sv_truthy(sv_has_key(entry, sv_str("scope"))) ? (sv_get(entry, sv_str("scope"))) : (sv_str("system")))); __m; }));
                } else {
                    sv_push(up_to_date, id);
                }
            }
        }
        i = sv_add(i, sv_num(1));
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "updates", updates); sv_map_set(__m, "up_to_date", up_to_date); __m; });
}
SVal __st_fn_plan_check_updates(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return plan_check_updates((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal plan_remove(SVal installed, SVal catalogue, SVal target_id, SVal force) {
    if ((!sv_truthy(sv_has_key(installed, target_id)))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "blocked_by", sv_arr_new(0, NULL)); sv_map_set(__m, "requires_root", sv_bool(false)); sv_map_set(__m, "error", sv_add(sv_str("component is not installed: "), target_id)); __m; });
    }
    SVal deps = find_reverse_dependents(catalogue, installed, target_id);
    if ((((sv_len(deps)).num > 0) && (!sv_truthy(force)))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "blocked_by", deps); sv_map_set(__m, "requires_root", sv_bool(false)); sv_map_set(__m, "error", sv_add(sv_add(sv_add(sv_str("cannot remove "), target_id), sv_str(": required by ")), sv_join(deps, sv_str(", ")))); __m; });
    }
    SVal scope = sv_str("system");
    SVal path = sv_str("");
    if (sv_truthy(sv_has_key(catalogue, target_id))) {
        SVal entry = sv_get(catalogue, target_id);
        if ((sv_truthy(sv_has_key(entry, sv_str("scope"))) && (!sv_eq(sv_get(entry, sv_str("scope")), sv_null())))) {
            scope = sv_get(entry, sv_str("scope"));
        }
        if ((sv_truthy(sv_has_key(entry, sv_str("install_path"))) && (!sv_eq(sv_get(entry, sv_str("install_path")), sv_null())))) {
            path = sv_get(entry, sv_str("install_path"));
        }
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "actions", sv_arr_new(1, (SVal[]){({ SVal __m = sv_map_new(); sv_map_set(__m, "id", target_id); sv_map_set(__m, "action", sv_str("remove")); sv_map_set(__m, "from", sv_str_cast(sv_get(installed, target_id))); sv_map_set(__m, "to", sv_str("")); sv_map_set(__m, "scope", scope); sv_map_set(__m, "install_path", path); sv_map_set(__m, "url", sv_str("")); sv_map_set(__m, "sha256", sv_str("")); __m; })})); sv_map_set(__m, "blocked_by", sv_arr_new(0, NULL)); sv_map_set(__m, "requires_root", requires_privilege_escalation(scope)); sv_map_set(__m, "error", sv_str("")); __m; });
}
SVal __st_fn_plan_remove(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return plan_remove((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null()), (_st_argc > 3 ? _st_argv[3] : sv_null())); }

SVal plan_system_snapshot_transition(SVal installed, SVal catalogue, SVal target_snapshot) {
    SVal target_ids = sv_sort(sv_keys(target_snapshot));
    SVal actions = sv_arr_new(0, NULL);
    SVal skipped = sv_arr_new(0, NULL);
    SVal needs_root = sv_bool(false);
    SVal inst_ids = sv_sort(sv_keys(installed));
    SVal i = sv_num(0);
    while ((i.num < (sv_len(inst_ids)).num)) {
        SVal id = sv_get(inst_ids, i);
        if ((!sv_truthy(sv_has_key(target_snapshot, id)))) {
            SVal scope = sv_str("system");
            SVal path = sv_str("");
            if (sv_truthy(sv_has_key(catalogue, id))) {
                SVal entry = sv_get(catalogue, id);
                if ((sv_truthy(sv_has_key(entry, sv_str("scope"))) && (!sv_eq(sv_get(entry, sv_str("scope")), sv_null())))) {
                    scope = sv_get(entry, sv_str("scope"));
                }
                if ((sv_truthy(sv_has_key(entry, sv_str("install_path"))) && (!sv_eq(sv_get(entry, sv_str("install_path")), sv_null())))) {
                    path = sv_get(entry, sv_str("install_path"));
                }
            }
            if (sv_truthy(requires_privilege_escalation(scope))) {
                needs_root = sv_bool(true);
            }
            sv_push(actions, ({ SVal __m = sv_map_new(); sv_map_set(__m, "id", id); sv_map_set(__m, "action", sv_str("remove")); sv_map_set(__m, "from", sv_str_cast(sv_get(installed, id))); sv_map_set(__m, "to", sv_str("")); sv_map_set(__m, "scope", scope); sv_map_set(__m, "install_path", path); sv_map_set(__m, "url", sv_str("")); sv_map_set(__m, "sha256", sv_str("")); __m; }));
        }
        i = sv_add(i, sv_num(1));
    }
    SVal j = sv_num(0);
    while ((j.num < (sv_len(target_ids)).num)) {
        SVal id = sv_get(target_ids, j);
        SVal want_ver = sv_str_cast(sv_get(target_snapshot, id));
        SVal cur_ver = (sv_truthy(sv_has_key(installed, id)) ? (sv_str_cast(sv_get(installed, id))) : (sv_str("")));
        SVal kind = classify_transition(cur_ver, want_ver);
        if (sv_eq(kind, sv_str("noop"))) {
            sv_push(skipped, id);
        } else {
            SVal scope = sv_str("system");
            SVal path = sv_str("");
            SVal url = sv_str("");
            SVal sha = sv_str("");
            if (sv_truthy(sv_has_key(catalogue, id))) {
                SVal entry = sv_get(catalogue, id);
                if ((sv_truthy(sv_has_key(entry, sv_str("scope"))) && (!sv_eq(sv_get(entry, sv_str("scope")), sv_null())))) {
                    scope = sv_get(entry, sv_str("scope"));
                }
                if ((sv_truthy(sv_has_key(entry, sv_str("install_path"))) && (!sv_eq(sv_get(entry, sv_str("install_path")), sv_null())))) {
                    path = sv_get(entry, sv_str("install_path"));
                }
                if ((sv_truthy(sv_has_key(entry, sv_str("url"))) && (!sv_eq(sv_get(entry, sv_str("url")), sv_null())))) {
                    url = sv_get(entry, sv_str("url"));
                }
                if ((sv_truthy(sv_has_key(entry, sv_str("sha256"))) && (!sv_eq(sv_get(entry, sv_str("sha256")), sv_null())))) {
                    sha = sv_get(entry, sv_str("sha256"));
                }
            }
            SVal pchk = validate_install_path(path, scope);
            if ((!sv_truthy(sv_get(pchk, sv_str("ok"))))) {
                return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "skipped", sv_arr_new(0, NULL)); sv_map_set(__m, "requires_root", sv_bool(false)); sv_map_set(__m, "error", sv_add(sv_add(id, sv_str(": ")), sv_get(pchk, sv_str("reason")))); __m; });
            }
            if (sv_truthy(requires_privilege_escalation(scope))) {
                needs_root = sv_bool(true);
            }
            sv_push(actions, ({ SVal __m = sv_map_new(); sv_map_set(__m, "id", id); sv_map_set(__m, "action", kind); sv_map_set(__m, "from", cur_ver); sv_map_set(__m, "to", want_ver); sv_map_set(__m, "scope", scope); sv_map_set(__m, "install_path", path); sv_map_set(__m, "url", url); sv_map_set(__m, "sha256", sha); __m; }));
        }
        j = sv_add(j, sv_num(1));
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "actions", actions); sv_map_set(__m, "skipped", skipped); sv_map_set(__m, "requires_root", needs_root); sv_map_set(__m, "error", sv_str("")); __m; });
}
SVal __st_fn_plan_system_snapshot_transition(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return plan_system_snapshot_transition((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal db_default_state(void) {
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "schema_version", sv_num(2)); sv_map_set(__m, "installed", sv_map_new()); sv_map_set(__m, "ownership", sv_map_new()); sv_map_set(__m, "journal", sv_arr_new(0, NULL)); sv_map_set(__m, "config", ({ SVal __m = sv_map_new(); sv_map_set(__m, "channel", sv_str("stable")); sv_map_set(__m, "auto_check", sv_bool(true)); sv_map_set(__m, "catalogue_url", sv_str("https://updates.infinityos.uk/catalogue.json")); sv_map_set(__m, "cache_ttl_sec", sv_num(3600)); __m; })); __m; });
}
SVal __st_fn_db_default_state(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return db_default_state(); }

SVal db_load_from_path(SVal db_path) {
    if ((!sv_truthy(sv_file_exists(db_path)))) {
        return db_default_state();
    }
    SVal raw = sv_read_file(db_path);
    if (sv_eq(sv_len(sv_trim(raw)), sv_num(0))) {
        return db_default_state();
    }
    SVal parsed = sv_json_parse(raw);
    if ((sv_eq(parsed, sv_null()) || (!sv_eq(sv_type(parsed), sv_str("map"))))) {
        return db_default_state();
    }
    if ((!sv_truthy(sv_has_key(parsed, sv_str("installed"))))) {
        sv_set(parsed, sv_str("installed"), sv_map_new());
    }
    if ((!sv_truthy(sv_has_key(parsed, sv_str("ownership"))))) {
        sv_set(parsed, sv_str("ownership"), sv_map_new());
    }
    if ((!sv_truthy(sv_has_key(parsed, sv_str("journal"))))) {
        sv_set(parsed, sv_str("journal"), sv_arr_new(0, NULL));
    }
    if ((!sv_truthy(sv_has_key(parsed, sv_str("config"))))) {
        sv_set(parsed, sv_str("config"), sv_get(db_default_state(), sv_str("config")));
    }
    return parsed;
}
SVal __st_fn_db_load_from_path(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return db_load_from_path((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal db_save_to_path(SVal db_path, SVal state) {
    return sv_write_file(db_path, sv_json_stringify(state));
}
SVal __st_fn_db_save_to_path(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return db_save_to_path((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal db_apply_transaction(SVal state, SVal actions, SVal timestamp) {
    SVal tx_id = sv_add(sv_str("tx-"), sv_str_cast(sv_add(sv_len(sv_get(state, sv_str("journal"))), sv_num(1))));
    SVal installed = sv_get(state, sv_str("installed"));
    SVal record_ops = sv_arr_new(0, NULL);
    SVal i = sv_num(0);
    while ((i.num < (sv_len(actions)).num)) {
        SVal a = sv_get(actions, i);
        SVal id = sv_get(a, sv_str("id"));
        SVal kind = sv_get(a, sv_str("action"));
        SVal prev_ver = (sv_truthy(sv_has_key(installed, id)) ? (sv_str_cast(sv_get(installed, id))) : (sv_str("")));
        if (sv_eq(kind, sv_str("remove"))) {
            sv_delete_key(installed, id);
        } else {
            sv_set(installed, id, sv_get(a, sv_str("to")));
        }
        sv_push(record_ops, ({ SVal __m = sv_map_new(); sv_map_set(__m, "id", id); sv_map_set(__m, "action", kind); sv_map_set(__m, "from", prev_ver); sv_map_set(__m, "to", sv_get(a, sv_str("to"))); sv_map_set(__m, "install_path", sv_get(a, sv_str("install_path"))); __m; }));
        i = sv_add(i, sv_num(1));
    }
    SVal entry = ({ SVal __m = sv_map_new(); sv_map_set(__m, "tx_id", tx_id); sv_map_set(__m, "timestamp", timestamp); sv_map_set(__m, "status", sv_str("committed")); sv_map_set(__m, "ops", record_ops); __m; });
    sv_push(sv_get(state, sv_str("journal")), entry);
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "tx_id", tx_id); sv_map_set(__m, "state", state); __m; });
}
SVal __st_fn_db_apply_transaction(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return db_apply_transaction((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal db_rollback_last(SVal state, SVal timestamp) {
    SVal journal = sv_get(state, sv_str("journal"));
    if (sv_eq(sv_len(journal), sv_num(0))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "error", sv_str("no transactions in journal")); sv_map_set(__m, "state", state); __m; });
    }
    SVal idx = sv_num(((sv_len(journal)).num - 1));
    while (((idx.num >= 0) && (!sv_eq(sv_get(sv_get(journal, idx), sv_str("status")), sv_str("committed"))))) {
        idx = sv_num((idx.num - 1));
    }
    if ((idx.num < 0)) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "error", sv_str("no committed transaction to roll back")); sv_map_set(__m, "state", state); __m; });
    }
    SVal tx = sv_get(journal, idx);
    SVal ops = sv_get(tx, sv_str("ops"));
    SVal installed = sv_get(state, sv_str("installed"));
    SVal reverted = sv_arr_new(0, NULL);
    SVal j = sv_num(((sv_len(ops)).num - 1));
    while ((j.num >= 0)) {
        SVal op = sv_get(ops, j);
        SVal id = sv_get(op, sv_str("id"));
        SVal from_ver = sv_get(op, sv_str("from"));
        if (sv_eq(sv_len(from_ver), sv_num(0))) {
            sv_delete_key(installed, id);
        } else {
            sv_set(installed, id, from_ver);
        }
        sv_push(reverted, ({ SVal __m = sv_map_new(); sv_map_set(__m, "id", id); sv_map_set(__m, "restored_version", from_ver); __m; }));
        j = sv_num((j.num - 1));
    }
    sv_set(tx, sv_str("status"), sv_str("rolled_back"));
    sv_set(tx, sv_str("rolled_back_at"), timestamp);
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "tx_id", sv_get(tx, sv_str("tx_id"))); sv_map_set(__m, "reverted", reverted); sv_map_set(__m, "state", state); __m; });
}
SVal __st_fn_db_rollback_last(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return db_rollback_last((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal db_set_config(SVal state, SVal key, SVal value) {
    SVal cfg = sv_get(state, sv_str("config"));
    if (sv_eq(key, sv_str("channel"))) {
        if ((((!sv_eq(value, sv_str("stable"))) && (!sv_eq(value, sv_str("beta")))) && (!sv_eq(value, sv_str("nightly"))))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "error", sv_add(sv_str("invalid channel: "), sv_str_cast(value))); __m; });
        }
        sv_set(cfg, sv_str("channel"), value);
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "error", sv_str("")); __m; });
    }
    if (sv_eq(key, sv_str("auto_check"))) {
        sv_set(cfg, sv_str("auto_check"), sv_bool((sv_eq(value, sv_str("true")) || sv_eq(value, sv_bool(true)))));
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "error", sv_str("")); __m; });
    }
    if (sv_eq(key, sv_str("catalogue_url"))) {
        if ((!sv_truthy(sv_starts_with(sv_str_cast(value), sv_str("https://"))))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "error", sv_str("catalogue_url must use https://")); __m; });
        }
        sv_set(cfg, sv_str("catalogue_url"), sv_str_cast(value));
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "error", sv_str("")); __m; });
    }
    if (sv_eq(key, sv_str("cache_ttl_sec"))) {
        SVal n = sv_int_cast(sv_num_cast(value));
        if ((n.num < 60)) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "error", sv_str("cache_ttl_sec must be at least 60")); __m; });
        }
        sv_set(cfg, sv_str("cache_ttl_sec"), n);
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "error", sv_str("")); __m; });
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "error", sv_add(sv_str("unknown config key: "), sv_str_cast(key))); __m; });
}
SVal __st_fn_db_set_config(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return db_set_config((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal parse_cli_flags(SVal args) {
    SVal positional = sv_arr_new(0, NULL);
    SVal flags = ({ SVal __m = sv_map_new(); sv_map_set(__m, "json", sv_bool(false)); sv_map_set(__m, "dry_run", sv_bool(false)); sv_map_set(__m, "force", sv_bool(false)); sv_map_set(__m, "channel", sv_str("")); __m; });
    SVal i = sv_num(0);
    while ((i.num < (sv_len(args)).num)) {
        SVal a = sv_get(args, i);
        if (sv_eq(a, sv_str("--json"))) {
            sv_set(flags, sv_str("json"), sv_bool(true));
        } else {
            if (sv_eq(a, sv_str("--dry-run"))) {
                sv_set(flags, sv_str("dry_run"), sv_bool(true));
            } else {
                if ((sv_eq(a, sv_str("--force")) || sv_eq(a, sv_str("-f")))) {
                    sv_set(flags, sv_str("force"), sv_bool(true));
                } else {
                    if ((sv_eq(a, sv_str("--channel")) && ((i.num + 1) < (sv_len(args)).num))) {
                        i = sv_add(i, sv_num(1));
                        sv_set(flags, sv_str("channel"), sv_get(args, i));
                    } else {
                        sv_push(positional, a);
                    }
                }
            }
        }
        i = sv_add(i, sv_num(1));
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "positional", positional); sv_map_set(__m, "flags", flags); __m; });
}
SVal __st_fn_parse_cli_flags(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return parse_cli_flags((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal format_help_text(void) {
    SVal lines = sv_arr_new(22, (SVal[]){sv_str("InfinityOS Updater (Steel-compiled core)"), sv_str(""), sv_str("Usage: infinity-updater <command> [args] [flags]"), sv_str(""), sv_str("Commands:"), sv_str("  status              Show installed components and pending updates"), sv_str("  list                List installed components"), sv_str("  search [query]      Search catalogue components"), sv_str("  check               Check for available updates"), sv_str("  install <id...>     Plan/install one or more components"), sv_str("  upgrade [id...]     Plan/upgrade installed components"), sv_str("  remove <id>         Plan/remove an installed component"), sv_str("  rollback            Roll back the most recent transaction"), sv_str("  history             Show transaction journal"), sv_str("  verify              Validate catalogue schema and security policy"), sv_str("  config [k] [v]      Show or update configuration"), sv_str(""), sv_str("Flags:"), sv_str("  --json              Emit machine-readable JSON output"), sv_str("  --dry-run           Plan actions without modifying state"), sv_str("  --force, -f         Bypass reverse-dependency checks on remove"), sv_str("  --channel <name>    Filter channel (stable|beta|nightly)")});
    return sv_join(lines, sv_str("\n"));
}
SVal __st_fn_format_help_text(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return format_help_text(); }

SVal format_plan_lines(SVal plan) {
    if ((!sv_truthy(sv_get(plan, sv_str("ok"))))) {
        return sv_add(sv_str("Error: "), sv_str_cast(sv_get(plan, sv_str("error"))));
    }
    SVal lines = sv_arr_new(0, NULL);
    SVal acts = sv_get(plan, sv_str("actions"));
    SVal i = sv_num(0);
    while ((i.num < (sv_len(acts)).num)) {
        SVal a = sv_get(acts, i);
        SVal arrow = (((sv_len(sv_get(a, sv_str("from")))).num > 0) ? (sv_add(sv_add(sv_get(a, sv_str("from")), sv_str(" -> ")), sv_get(a, sv_str("to")))) : (sv_get(a, sv_str("to"))));
        if (sv_eq(sv_get(a, sv_str("action")), sv_str("remove"))) {
            arrow = sv_get(a, sv_str("from"));
        }
        sv_push(lines, sv_add(sv_add(sv_add(sv_add(sv_add(sv_add(sv_add(sv_str("  ["), sv_upper(sv_get(a, sv_str("action")))), sv_str("] ")), sv_get(a, sv_str("id"))), sv_str(" (")), arrow), sv_str(") -> ")), sv_get(a, sv_str("install_path"))));
        i = sv_add(i, sv_num(1));
    }
    if (((sv_len(sv_get(plan, sv_str("skipped")))).num > 0)) {
        sv_push(lines, sv_add(sv_str("  [SKIP] already up to date: "), sv_join(sv_get(plan, sv_str("skipped")), sv_str(", "))));
    }
    if (sv_eq(sv_len(lines), sv_num(0))) {
        return sv_str("Nothing to do.");
    }
    return sv_join(lines, sv_str("\n"));
}
SVal __st_fn_format_plan_lines(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return format_plan_lines((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal cli_dispatch(SVal args, SVal state, SVal catalogue, SVal timestamp) {
    SVal parsed = parse_cli_flags(args);
    SVal pos = sv_get(parsed, sv_str("positional"));
    SVal flags = sv_get(parsed, sv_str("flags"));
    SVal channel = (((sv_len(sv_get(flags, sv_str("channel")))).num > 0) ? (sv_get(flags, sv_str("channel"))) : (sv_get(sv_get(state, sv_str("config")), sv_str("channel"))));
    if ((((sv_eq(sv_len(pos), sv_num(0)) || sv_eq(sv_get(pos, sv_num(0)), sv_str("help"))) || sv_eq(sv_get(pos, sv_num(0)), sv_str("--help"))) || sv_eq(sv_get(pos, sv_num(0)), sv_str("-h")))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", format_help_text()); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
    }
    SVal cmd = sv_get(pos, sv_num(0));
    if (sv_eq(cmd, sv_str("list"))) {
        SVal ks = sv_sort(sv_keys(sv_get(state, sv_str("installed"))));
        if (sv_truthy(sv_get(flags, sv_str("json")))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_json_stringify(sv_get(state, sv_str("installed")))); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        if (sv_eq(sv_len(ks), sv_num(0))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_str("No components installed.")); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        SVal lines = sv_arr_new(0, NULL);
        SVal i = sv_num(0);
        while ((i.num < (sv_len(ks)).num)) {
            SVal id = sv_get(ks, i);
            sv_push(lines, sv_add(sv_add(sv_pad_end(id, sv_num(24), sv_str(" ")), sv_str(" ")), sv_str_cast(sv_get(sv_get(state, sv_str("installed")), id))));
            i = sv_add(i, sv_num(1));
        }
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_join(lines, sv_str("\n"))); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
    }
    if (sv_eq(cmd, sv_str("search"))) {
        SVal q = (((sv_len(pos)).num > 1) ? (sv_get(pos, sv_num(1))) : (sv_str("")));
        SVal hits = search_catalogue(catalogue, q, channel);
        if (sv_truthy(sv_get(flags, sv_str("json")))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_json_stringify(hits)); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        if (sv_eq(sv_len(hits), sv_num(0))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_str("No matching components.")); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        SVal lines = sv_arr_new(0, NULL);
        SVal i = sv_num(0);
        while ((i.num < (sv_len(hits)).num)) {
            SVal h = sv_get(hits, i);
            sv_push(lines, sv_add(sv_add(sv_add(sv_add(sv_pad_end(sv_get(h, sv_str("id")), sv_num(22), sv_str(" ")), sv_str(" ")), sv_pad_end(sv_get(h, sv_str("version")), sv_num(10), sv_str(" "))), sv_str(" ")), sv_get(h, sv_str("description"))));
            i = sv_add(i, sv_num(1));
        }
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_join(lines, sv_str("\n"))); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
    }
    if ((sv_eq(cmd, sv_str("check")) || sv_eq(cmd, sv_str("status")))) {
        SVal chk = plan_check_updates(sv_get(state, sv_str("installed")), catalogue, channel);
        if (sv_truthy(sv_get(flags, sv_str("json")))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_json_stringify(chk)); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        SVal ups = sv_get(chk, sv_str("updates"));
        if (sv_eq(sv_len(ups), sv_num(0))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_add(sv_add(sv_str("All "), sv_str_cast(sv_len(sv_get(chk, sv_str("up_to_date"))))), sv_str(" installed components are up to date."))); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        SVal lines = sv_arr_new(1, (SVal[]){sv_add(sv_add(sv_str("Available updates ("), sv_str_cast(sv_len(ups))), sv_str("):"))});
        SVal i = sv_num(0);
        while ((i.num < (sv_len(ups)).num)) {
            SVal u = sv_get(ups, i);
            sv_push(lines, sv_add(sv_add(sv_add(sv_add(sv_add(sv_str("  "), sv_get(u, sv_str("id"))), sv_str(": ")), sv_get(u, sv_str("from"))), sv_str(" -> ")), sv_get(u, sv_str("to"))));
            i = sv_add(i, sv_num(1));
        }
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_join(lines, sv_str("\n"))); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
    }
    if (sv_eq(cmd, sv_str("verify"))) {
        SVal v = validate_catalogue(catalogue);
        SVal out = (sv_truthy(sv_get(flags, sv_str("json"))) ? (sv_json_stringify(v)) : ((sv_truthy(sv_get(v, sv_str("ok"))) ? (sv_add(sv_add(sv_str("Catalogue OK ("), sv_str_cast(sv_get(v, sv_str("count")))), sv_str(" components)."))) : (sv_add(sv_str("Catalogue errors:\n  "), sv_join(sv_get(v, sv_str("errors")), sv_str("\n  ")))))));
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", (sv_truthy(sv_get(v, sv_str("ok"))) ? (sv_num(0)) : (sv_num(1)))); sv_map_set(__m, "output", out); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
    }
    if ((sv_eq(cmd, sv_str("install")) || sv_eq(cmd, sv_str("upgrade")))) {
        SVal targets = sv_slice(pos, sv_num(1), sv_len(pos));
        if ((sv_eq(cmd, sv_str("upgrade")) && sv_eq(sv_len(targets), sv_num(0)))) {
            targets = sv_sort(sv_keys(sv_get(state, sv_str("installed"))));
        }
        if (sv_eq(sv_len(targets), sv_num(0))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(2)); sv_map_set(__m, "output", sv_str("Error: specify at least one component id")); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        SVal plan = plan_reconcile(sv_get(state, sv_str("installed")), catalogue, targets);
        if ((!sv_truthy(sv_get(plan, sv_str("ok"))))) {
            SVal out = (sv_truthy(sv_get(flags, sv_str("json"))) ? (sv_json_stringify(plan)) : (sv_add(sv_str("Error: "), sv_get(plan, sv_str("error")))));
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(1)); sv_map_set(__m, "output", out); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        SVal should_save = sv_bool(((!sv_truthy(sv_get(flags, sv_str("dry_run")))) && ((sv_len(sv_get(plan, sv_str("actions")))).num > 0)));
        if (sv_truthy(should_save)) {
            db_apply_transaction(state, sv_get(plan, sv_str("actions")), timestamp);
        }
        SVal out = (sv_truthy(sv_get(flags, sv_str("json"))) ? (sv_json_stringify(plan)) : (format_plan_lines(plan)));
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", out); sv_map_set(__m, "actions", (sv_truthy(sv_get(flags, sv_str("dry_run"))) ? (sv_arr_new(0, NULL)) : (sv_get(plan, sv_str("actions"))))); sv_map_set(__m, "save_state", should_save); sv_map_set(__m, "state", state); __m; });
    }
    if (sv_eq(cmd, sv_str("remove"))) {
        if (((sv_len(pos)).num < 2)) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(2)); sv_map_set(__m, "output", sv_str("Error: specify a component id to remove")); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        SVal plan = plan_remove(sv_get(state, sv_str("installed")), catalogue, sv_get(pos, sv_num(1)), sv_get(flags, sv_str("force")));
        if ((!sv_truthy(sv_get(plan, sv_str("ok"))))) {
            SVal out = (sv_truthy(sv_get(flags, sv_str("json"))) ? (sv_json_stringify(plan)) : (sv_add(sv_str("Error: "), sv_get(plan, sv_str("error")))));
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(1)); sv_map_set(__m, "output", out); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        SVal should_save = sv_bool(!sv_truthy(sv_get(flags, sv_str("dry_run"))));
        if (sv_truthy(should_save)) {
            db_apply_transaction(state, sv_get(plan, sv_str("actions")), timestamp);
        }
        SVal out = (sv_truthy(sv_get(flags, sv_str("json"))) ? (sv_json_stringify(plan)) : (format_plan_lines(plan)));
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", out); sv_map_set(__m, "actions", (sv_truthy(sv_get(flags, sv_str("dry_run"))) ? (sv_arr_new(0, NULL)) : (sv_get(plan, sv_str("actions"))))); sv_map_set(__m, "save_state", should_save); sv_map_set(__m, "state", state); __m; });
    }
    if (sv_eq(cmd, sv_str("rollback"))) {
        SVal rb = db_rollback_last(state, timestamp);
        if ((!sv_truthy(sv_get(rb, sv_str("ok"))))) {
            SVal out = (sv_truthy(sv_get(flags, sv_str("json"))) ? (sv_json_stringify(rb)) : (sv_add(sv_str("Error: "), sv_get(rb, sv_str("error")))));
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(1)); sv_map_set(__m, "output", out); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        SVal out = (sv_truthy(sv_get(flags, sv_str("json"))) ? (sv_json_stringify(rb)) : (sv_add(sv_add(sv_add(sv_add(sv_str("Rolled back "), sv_get(rb, sv_str("tx_id"))), sv_str(" (")), sv_str_cast(sv_len(sv_get(rb, sv_str("reverted"))))), sv_str(" components restored)."))));
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", out); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(!sv_truthy(sv_get(flags, sv_str("dry_run"))))); sv_map_set(__m, "state", state); __m; });
    }
    if (sv_eq(cmd, sv_str("history"))) {
        if (sv_truthy(sv_get(flags, sv_str("json")))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_json_stringify(sv_get(state, sv_str("journal")))); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        SVal j = sv_get(state, sv_str("journal"));
        if (sv_eq(sv_len(j), sv_num(0))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_str("No transactions recorded.")); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        SVal lines = sv_arr_new(0, NULL);
        SVal i = sv_num(0);
        while ((i.num < (sv_len(j)).num)) {
            SVal tx = sv_get(j, i);
            sv_push(lines, sv_add(sv_add(sv_add(sv_add(sv_add(sv_add(sv_get(tx, sv_str("tx_id")), sv_str(" [")), sv_get(tx, sv_str("status"))), sv_str("] ops=")), sv_str_cast(sv_len(sv_get(tx, sv_str("ops"))))), sv_str(" ts=")), sv_str_cast(sv_get(tx, sv_str("timestamp")))));
            i = sv_add(i, sv_num(1));
        }
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_join(lines, sv_str("\n"))); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
    }
    if (sv_eq(cmd, sv_str("config"))) {
        if (sv_eq(sv_len(pos), sv_num(1))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_json_stringify(sv_get(state, sv_str("config")))); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        if (((sv_len(pos)).num < 3)) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(2)); sv_map_set(__m, "output", sv_str("Usage: infinity-updater config <key> <value>")); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        SVal res = db_set_config(state, sv_get(pos, sv_num(1)), sv_get(pos, sv_num(2)));
        if ((!sv_truthy(sv_get(res, sv_str("ok"))))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(1)); sv_map_set(__m, "output", sv_add(sv_str("Error: "), sv_get(res, sv_str("error")))); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
        }
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_add(sv_add(sv_add(sv_str("Updated "), sv_get(pos, sv_num(1))), sv_str(" = ")), sv_get(pos, sv_num(2)))); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(true)); sv_map_set(__m, "state", state); __m; });
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(2)); sv_map_set(__m, "output", sv_add(sv_add(sv_str("Unknown command: "), cmd), sv_str("\nRun 'infinity-updater help' for usage."))); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); sv_map_set(__m, "save_state", sv_bool(false)); sv_map_set(__m, "state", state); __m; });
}
SVal __st_fn_cli_dispatch(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return cli_dispatch((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null()), (_st_argc > 3 ? _st_argv[3] : sv_null())); }

SVal ui_initial_state(void) {
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "active_tab", sv_str("updates")); sv_map_set(__m, "search_query", sv_str("")); sv_map_set(__m, "selected_ids", sv_arr_new(0, NULL)); sv_map_set(__m, "busy", sv_bool(false)); sv_map_set(__m, "banner_level", sv_str("info")); sv_map_set(__m, "banner_text", sv_str("Ready")); sv_map_set(__m, "pending_plan", sv_null()); __m; });
}
SVal __st_fn_ui_initial_state(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return ui_initial_state(); }

SVal ui_toggle_selection(SVal selected, SVal id) {
    SVal out = sv_arr_new(0, NULL);
    SVal found = sv_bool(false);
    SVal i = sv_num(0);
    while ((i.num < (sv_len(selected)).num)) {
        if (sv_eq(sv_get(selected, i), id)) {
            found = sv_bool(true);
        } else {
            sv_push(out, sv_get(selected, i));
        }
        i = sv_add(i, sv_num(1));
    }
    if ((!sv_truthy(found))) {
        sv_push(out, id);
    }
    return out;
}
SVal __st_fn_ui_toggle_selection(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return ui_toggle_selection((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal ui_reduce(SVal ui_state, SVal event, SVal db_state, SVal catalogue) {
    SVal kind = sv_get(event, sv_str("type"));
    if (sv_eq(kind, sv_str("switch_tab"))) {
        SVal tab = sv_get(event, sv_str("tab"));
        if (((((sv_eq(tab, sv_str("updates")) || sv_eq(tab, sv_str("installed"))) || sv_eq(tab, sv_str("explore"))) || sv_eq(tab, sv_str("history"))) || sv_eq(tab, sv_str("settings")))) {
            sv_set(ui_state, sv_str("active_tab"), tab);
        }
        return ui_state;
    }
    if (sv_eq(kind, sv_str("set_search"))) {
        sv_set(ui_state, sv_str("search_query"), sv_str_cast(sv_get(event, sv_str("query"))));
        return ui_state;
    }
    if (sv_eq(kind, sv_str("toggle_select"))) {
        sv_set(ui_state, sv_str("selected_ids"), ui_toggle_selection(sv_get(ui_state, sv_str("selected_ids")), sv_str_cast(sv_get(event, sv_str("id")))));
        return ui_state;
    }
    if (sv_eq(kind, sv_str("select_all_updates"))) {
        SVal chk = plan_check_updates(sv_get(db_state, sv_str("installed")), catalogue, sv_get(sv_get(db_state, sv_str("config")), sv_str("channel")));
        SVal ids = sv_arr_new(0, NULL);
        SVal ups = sv_get(chk, sv_str("updates"));
        SVal i = sv_num(0);
        while ((i.num < (sv_len(ups)).num)) {
            sv_push(ids, sv_get(sv_get(ups, i), sv_str("id")));
            i = sv_add(i, sv_num(1));
        }
        sv_set(ui_state, sv_str("selected_ids"), ids);
        sv_set(ui_state, sv_str("banner_level"), sv_str("info"));
        sv_set(ui_state, sv_str("banner_text"), sv_add(sv_str_cast(sv_len(ids)), sv_str(" updates selected")));
        return ui_state;
    }
    if (sv_eq(kind, sv_str("preview_plan"))) {
        SVal plan = plan_reconcile(sv_get(db_state, sv_str("installed")), catalogue, sv_get(ui_state, sv_str("selected_ids")));
        sv_set(ui_state, sv_str("pending_plan"), plan);
        if ((!sv_truthy(sv_get(plan, sv_str("ok"))))) {
            sv_set(ui_state, sv_str("banner_level"), sv_str("error"));
            sv_set(ui_state, sv_str("banner_text"), sv_get(plan, sv_str("error")));
        } else {
            sv_set(ui_state, sv_str("banner_level"), sv_str("success"));
            sv_set(ui_state, sv_str("banner_text"), sv_add(sv_add(sv_str("Ready to apply "), sv_str_cast(sv_len(sv_get(plan, sv_str("actions"))))), sv_str(" actions")));
        }
        return ui_state;
    }
    if (sv_eq(kind, sv_str("clear_selection"))) {
        sv_set(ui_state, sv_str("selected_ids"), sv_arr_new(0, NULL));
        sv_set(ui_state, sv_str("pending_plan"), sv_null());
        sv_set(ui_state, sv_str("banner_level"), sv_str("info"));
        sv_set(ui_state, sv_str("banner_text"), sv_str("Selection cleared"));
        return ui_state;
    }
    return ui_state;
}
SVal __st_fn_ui_reduce(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return ui_reduce((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null()), (_st_argc > 3 ? _st_argv[3] : sv_null())); }

SVal build_package_manifest(SVal id, SVal version, SVal scope, SVal install_path, SVal sha256, SVal signature, SVal files) {
    SVal entry_check = validate_catalogue_entry(id, ({ SVal __m = sv_map_new(); sv_map_set(__m, "version", version); sv_map_set(__m, "scope", scope); sv_map_set(__m, "install_path", install_path); sv_map_set(__m, "sha256", sha256); __m; }));
    if ((!sv_truthy(sv_get(entry_check, sv_str("ok"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "error", sv_get(entry_check, sv_str("reason"))); sv_map_set(__m, "manifest", sv_null()); __m; });
    }
    SVal arch_check = validate_archive_manifest(files);
    if ((!sv_truthy(sv_get(arch_check, sv_str("ok"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "error", sv_get(arch_check, sv_str("reason"))); sv_map_set(__m, "manifest", sv_null()); __m; });
    }
    if ((((!sv_eq(signature, sv_null())) && ((sv_len(sv_str_cast(signature))).num > 0)) && (!sv_truthy(validate_ed25519_sig(sv_str_cast(signature)))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "error", sv_str("invalid ed25519 signature hex")); sv_map_set(__m, "manifest", sv_null()); __m; });
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "error", sv_str("")); sv_map_set(__m, "manifest", ({ SVal __m = sv_map_new(); sv_map_set(__m, "id", id); sv_map_set(__m, "version", version); sv_map_set(__m, "scope", scope); sv_map_set(__m, "install_path", install_path); sv_map_set(__m, "sha256", sv_lower(sv_str_cast(sha256))); sv_map_set(__m, "signature", ((!sv_eq(signature, sv_null())) ? (sv_lower(sv_str_cast(signature))) : (sv_str("")))); sv_map_set(__m, "file_count", sv_len(files)); sv_map_set(__m, "files", files); __m; })); __m; });
}
SVal __st_fn_build_package_manifest(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return build_package_manifest((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null()), (_st_argc > 3 ? _st_argv[3] : sv_null()), (_st_argc > 4 ? _st_argv[4] : sv_null()), (_st_argc > 5 ? _st_argv[5] : sv_null()), (_st_argc > 6 ? _st_argv[6] : sv_null())); }

SVal publish_catalogue_entry(SVal catalogue, SVal id, SVal entry) {
    SVal chk = validate_catalogue_entry(id, entry);
    if ((!sv_truthy(sv_get(chk, sv_str("ok"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "error", sv_get(chk, sv_str("reason"))); sv_map_set(__m, "catalogue", catalogue); __m; });
    }
    if (sv_truthy(sv_has_key(catalogue, id))) {
        SVal prev = sv_get(sv_get(catalogue, id), sv_str("version"));
        if (((compare_semver(sv_get(entry, sv_str("version")), prev)).num < 0)) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "error", sv_add(sv_add(sv_add(sv_str("cannot publish older version "), sv_str_cast(sv_get(entry, sv_str("version")))), sv_str(" over ")), sv_str_cast(prev))); sv_map_set(__m, "catalogue", catalogue); __m; });
        }
    }
    sv_set(catalogue, id, entry);
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "error", sv_str("")); sv_map_set(__m, "catalogue", catalogue); __m; });
}
SVal __st_fn_publish_catalogue_entry(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return publish_catalogue_entry((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal validate_helper_request(SVal req) {
    if ((sv_eq(req, sv_null()) || (!sv_eq(sv_type(req), sv_str("map"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("helper request must be a JSON object")); __m; });
    }
    if ((((!sv_truthy(sv_has_key(req, sv_str("op")))) || (!sv_truthy(sv_has_key(req, sv_str("component_id"))))) || (!sv_truthy(sv_has_key(req, sv_str("install_path")))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("missing required helper fields (op, component_id, install_path)")); __m; });
    }
    SVal op = sv_str_cast(sv_get(req, sv_str("op")));
    if (((((!sv_eq(op, sv_str("install"))) && (!sv_eq(op, sv_str("upgrade")))) && (!sv_eq(op, sv_str("downgrade")))) && (!sv_eq(op, sv_str("remove"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(sv_str("unsupported helper op: "), op)); __m; });
    }
    SVal id = sv_str_cast(sv_get(req, sv_str("component_id")));
    if ((!sv_truthy(is_valid_component_id(id)))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(sv_str("invalid component_id: "), id)); __m; });
    }
    SVal scope = ((sv_truthy(sv_has_key(req, sv_str("scope"))) && (!sv_eq(sv_get(req, sv_str("scope")), sv_null()))) ? (sv_str_cast(sv_get(req, sv_str("scope")))) : (sv_str("system")));
    SVal pchk = validate_install_path(sv_str_cast(sv_get(req, sv_str("install_path"))), scope);
    if ((!sv_truthy(sv_get(pchk, sv_str("ok"))))) {
        return pchk;
    }
    if ((!sv_eq(op, sv_str("remove")))) {
        if (((!sv_truthy(sv_has_key(req, sv_str("archive_path")))) || sv_eq(sv_len(sv_trim(sv_str_cast(sv_get(req, sv_str("archive_path"))))), sv_num(0)))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(sv_str("archive_path required for "), op)); __m; });
        }
        if (((sv_truthy(sv_has_key(req, sv_str("sha256"))) && (!sv_eq(sv_get(req, sv_str("sha256")), sv_null()))) && ((sv_len(sv_str_cast(sv_get(req, sv_str("sha256"))))).num > 0))) {
            if ((!sv_truthy(validate_sha256(sv_str_cast(sv_get(req, sv_str("sha256"))))))) {
                return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_str("invalid sha256 in helper request")); __m; });
            }
        }
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "reason", sv_str("")); __m; });
}
SVal __st_fn_validate_helper_request(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return validate_helper_request((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal helper_check_dpkg_owner(SVal target_path) {
    SVal res = sv_exec(sv_str("dpkg"), sv_arr_new(2, (SVal[]){sv_str("-S"), target_path}), 1);
    if (((((!sv_eq(res, sv_null())) && sv_eq(sv_type(res), sv_str("map"))) && sv_eq(sv_get(res, sv_str("code")), sv_num(0))) && ((sv_len(sv_trim(sv_str_cast(sv_get(res, sv_str("stdout")))))).num > 0))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "reason", sv_add(sv_str("path owned by dpkg: "), sv_trim(sv_str_cast(sv_get(res, sv_str("stdout")))))); __m; });
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "reason", sv_str("")); __m; });
}
SVal __st_fn_helper_check_dpkg_owner(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return helper_check_dpkg_owner((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal helper_backup_path(SVal backup_root, SVal tx_id, SVal component_id) {
    SVal root = (((!sv_eq(backup_root, sv_null())) && ((sv_len(sv_trim(sv_str_cast(backup_root)))).num > 0)) ? (sv_str_cast(backup_root)) : (sv_str("/var/lib/infinityos-updater/backups")));
    SVal tx = (((!sv_eq(tx_id, sv_null())) && ((sv_len(sv_trim(sv_str_cast(tx_id)))).num > 0)) ? (sv_str_cast(tx_id)) : (sv_str("tx-current")));
    return sv_add(sv_add(sv_add(sv_add(root, sv_str("/")), tx), sv_str("/")), sv_str_cast(component_id));
}
SVal __st_fn_helper_backup_path(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return helper_backup_path((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal helper_create_backup(SVal dest, SVal backup_path) {
    if ((!sv_truthy(sv_file_exists(dest)))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "backed_up", sv_bool(false)); sv_map_set(__m, "backup_path", sv_str("")); __m; });
    }
    SVal parent_idx = sv_num(((sv_len(backup_path)).num - 1));
    while (((parent_idx.num > 0) && (!sv_eq(sv_slice(backup_path, parent_idx, sv_add(parent_idx, sv_num(1))), sv_str("/"))))) {
        parent_idx = sv_num((parent_idx.num - 1));
    }
    SVal parent_dir = sv_slice(backup_path, sv_num(0), parent_idx);
    if (((sv_len(parent_dir)).num > 0)) {
        sv_mkdir(parent_dir);
    }
    if (sv_truthy(sv_file_exists(backup_path))) {
        sv_remove_dir(backup_path);
    }
    if ((!sv_truthy(sv_rename(dest, backup_path)))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "backed_up", sv_bool(false)); sv_map_set(__m, "backup_path", sv_str("")); __m; });
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "backed_up", sv_bool(true)); sv_map_set(__m, "backup_path", backup_path); __m; });
}
SVal __st_fn_helper_create_backup(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return helper_create_backup((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal helper_restore_backup(SVal dest, SVal backup_path) {
    if (sv_truthy(sv_file_exists(dest))) {
        sv_remove_dir(dest);
    }
    if ((((!sv_eq(backup_path, sv_null())) && ((sv_len(sv_str_cast(backup_path))).num > 0)) && sv_truthy(sv_file_exists(sv_str_cast(backup_path))))) {
        if ((!sv_truthy(sv_rename(sv_str_cast(backup_path), dest)))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(false)); sv_map_set(__m, "error", sv_add(sv_str("failed to restore backup from "), sv_str_cast(backup_path))); __m; });
        }
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "ok", sv_bool(true)); sv_map_set(__m, "error", sv_str("")); __m; });
}
SVal __st_fn_helper_restore_backup(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return helper_restore_backup((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal helper_execute_request(SVal req_json) {
    SVal req = sv_json_parse(req_json);
    if (((((!sv_eq(req, sv_null())) && sv_eq(sv_type(req), sv_str("map"))) && sv_truthy(sv_has_key(req, sv_str("op")))) && sv_eq(sv_get(req, sv_str("op")), sv_str("restore_backup")))) {
        SVal dest = sv_str_cast(sv_get(req, sv_str("install_path")));
        SVal bp = sv_str_cast(sv_get(req, sv_str("backup_path")));
        SVal r = helper_restore_backup(dest, bp);
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", (sv_truthy(sv_get(r, sv_str("ok"))) ? (sv_num(0)) : (sv_num(1)))); sv_map_set(__m, "error", sv_get(r, sv_str("error"))); __m; });
    }
    SVal chk = validate_helper_request(req);
    if ((!sv_truthy(sv_get(chk, sv_str("ok"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(2)); sv_map_set(__m, "error", sv_add(sv_str("policy rejected request: "), sv_get(chk, sv_str("reason")))); __m; });
    }
    SVal op = sv_str_cast(sv_get(req, sv_str("op")));
    SVal id = sv_str_cast(sv_get(req, sv_str("component_id")));
    SVal dest = sv_str_cast(sv_get(req, sv_str("install_path")));
    SVal dpkg_chk = helper_check_dpkg_owner(dest);
    if ((!sv_truthy(sv_get(dpkg_chk, sv_str("ok"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(3)); sv_map_set(__m, "error", sv_get(dpkg_chk, sv_str("reason"))); __m; });
    }
    SVal b_root = (sv_truthy(sv_has_key(req, sv_str("backup_root"))) ? (sv_get(req, sv_str("backup_root"))) : (sv_str("")));
    SVal tx_id = (sv_truthy(sv_has_key(req, sv_str("tx_id"))) ? (sv_get(req, sv_str("tx_id"))) : (sv_str("tx-current")));
    SVal b_path = helper_backup_path(b_root, tx_id, id);
    if (sv_eq(op, sv_str("remove"))) {
        SVal b = helper_create_backup(dest, b_path);
        if ((!sv_truthy(sv_get(b, sv_str("ok"))))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(1)); sv_map_set(__m, "error", sv_add(sv_add(sv_str("failed to back up "), dest), sv_str(" before remove"))); __m; });
        }
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "error", sv_str("")); sv_map_set(__m, "backup_path", sv_get(b, sv_str("backup_path"))); __m; });
    }
    SVal archive = sv_str_cast(sv_get(req, sv_str("archive_path")));
    if (((sv_truthy(sv_has_key(req, sv_str("sha256"))) && (!sv_eq(sv_get(req, sv_str("sha256")), sv_null()))) && ((sv_len(sv_str_cast(sv_get(req, sv_str("sha256"))))).num > 0))) {
        SVal actual = sv_sha256_file(archive);
        if ((!sv_eq(sv_lower(actual), sv_lower(sv_str_cast(sv_get(req, sv_str("sha256"))))))) {
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(4)); sv_map_set(__m, "error", sv_add(sv_add(sv_add(sv_str("sha256 mismatch: expected "), sv_str_cast(sv_get(req, sv_str("sha256")))), sv_str(", got ")), actual)); __m; });
        }
    }
    SVal b = helper_create_backup(dest, b_path);
    if ((!sv_truthy(sv_get(b, sv_str("ok"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(1)); sv_map_set(__m, "error", sv_add(sv_str("failed to create on-disk backup for "), dest)); __m; });
    }
    if ((!sv_truthy(sv_mkdir(dest)))) {
        helper_restore_backup(dest, sv_get(b, sv_str("backup_path")));
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(1)); sv_map_set(__m, "error", sv_add(sv_str("failed to create destination dir "), dest)); __m; });
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "error", sv_str("")); sv_map_set(__m, "backup_path", sv_get(b, sv_str("backup_path"))); sv_map_set(__m, "extract", ({ SVal __m = sv_map_new(); sv_map_set(__m, "archive", archive); sv_map_set(__m, "dest", dest); __m; })); __m; });
}
SVal __st_fn_helper_execute_request(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return helper_execute_request((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal runner_atomic_write(SVal path, SVal content) {
    SVal tmp = sv_add(path, sv_str(".tmp"));
    if ((!sv_truthy(sv_write_file(tmp, content)))) {
        return sv_bool(false);
    }
    return sv_rename(tmp, path);
}
SVal __st_fn_runner_atomic_write(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return runner_atomic_write((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal runner_cli_main(void) {
    SVal state_path = sv_env(sv_str("INFINITY_STATE_PATH"));
    if ((sv_eq(state_path, sv_null()) || sv_eq(sv_len(sv_str_cast(state_path)), sv_num(0)))) {
        state_path = sv_str("infinity-state.json");
    }
    SVal cat_path = sv_env(sv_str("INFINITY_CATALOGUE_PATH"));
    if ((sv_eq(cat_path, sv_null()) || sv_eq(sv_len(sv_str_cast(cat_path)), sv_num(0)))) {
        cat_path = sv_str("catalogue.json");
    }
    SVal state = db_load_from_path(state_path);
    SVal catalogue = sv_map_new();
    if (sv_truthy(sv_file_exists(cat_path))) {
        SVal raw_cat = sv_read_file(cat_path);
        if (((sv_len(sv_trim(raw_cat))).num > 0)) {
            SVal parsed = sv_json_parse(raw_cat);
            if (((!sv_eq(parsed, sv_null())) && sv_eq(sv_type(parsed), sv_str("map")))) {
                catalogue = parsed;
            }
        }
    }
    SVal argv = sv_args();
    SVal ts = sv_int_cast(sv_clock());
    SVal outcome = cli_dispatch(argv, state, catalogue, ts);
    if (((sv_len(sv_str_cast(sv_get(outcome, sv_str("output"))))).num > 0)) {
        sv_print_one(sv_get(outcome, sv_str("output"))); printf("\n");
    }
    if (sv_truthy(sv_get(outcome, sv_str("save_state")))) {
        if ((!sv_truthy(runner_atomic_write(state_path, sv_json_stringify(sv_get(outcome, sv_str("state"))))))) {
            sv_print_one(sv_add(sv_str("Error saving state to "), state_path)); printf("\n");
            return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(1)); sv_map_set(__m, "actions", sv_arr_new(0, NULL)); __m; });
        }
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_get(outcome, sv_str("exit_code"))); sv_map_set(__m, "actions", sv_get(outcome, sv_str("actions"))); __m; });
}
SVal __st_fn_runner_cli_main(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return runner_cli_main(); }

SVal runner_publish_catalogue(SVal cat_path, SVal id, SVal entry_path) {
    if ((!sv_truthy(sv_file_exists(entry_path)))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(1)); sv_map_set(__m, "output", sv_add(sv_str("Error: entry file not found: "), entry_path)); __m; });
    }
    SVal entry = sv_json_parse(sv_read_file(entry_path));
    SVal cat = sv_map_new();
    if (sv_truthy(sv_file_exists(cat_path))) {
        SVal raw = sv_read_file(cat_path);
        if (((sv_len(sv_trim(raw))).num > 0)) {
            SVal parsed = sv_json_parse(raw);
            if (((!sv_eq(parsed, sv_null())) && sv_eq(sv_type(parsed), sv_str("map")))) {
                cat = parsed;
            }
        }
    }
    SVal res = publish_catalogue_entry(cat, id, entry);
    if ((!sv_truthy(sv_get(res, sv_str("ok"))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(1)); sv_map_set(__m, "output", sv_add(sv_str("publish rejected: "), sv_get(res, sv_str("error")))); __m; });
    }
    if ((!sv_truthy(runner_atomic_write(cat_path, sv_json_stringify(sv_get(res, sv_str("catalogue"))))))) {
        return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(1)); sv_map_set(__m, "output", sv_add(sv_str("Error writing catalogue to "), cat_path)); __m; });
    }
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "exit_code", sv_num(0)); sv_map_set(__m, "output", sv_add(sv_add(sv_add(sv_str("Published "), id), sv_str(" to ")), cat_path)); __m; });
}
SVal __st_fn_runner_publish_catalogue(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return runner_publish_catalogue((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal test_sample_catalogue(void) {
    SVal sha_a = sv_str("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    SVal sha_b = sv_str("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    return ({ SVal __m = sv_map_new(); sv_map_set(__m, "origin-core", ({ SVal __m = sv_map_new(); sv_map_set(__m, "version", sv_str("1.1.0")); sv_map_set(__m, "install_path", sv_str("/usr/share/gnome-shell/extensions/Origin@appscreen")); sv_map_set(__m, "scope", sv_str("system")); sv_map_set(__m, "description", sv_str("Origin shell core")); sv_map_set(__m, "channel", sv_str("stable")); sv_map_set(__m, "url", sv_str("https://updates.infinityos.uk/origin-core-1.1.0.tar.xz")); sv_map_set(__m, "sha256", sha_a); sv_map_set(__m, "depends", sv_arr_new(0, NULL)); __m; })); sv_map_set(__m, "origin-cc", ({ SVal __m = sv_map_new(); sv_map_set(__m, "version", sv_str("1.0.0")); sv_map_set(__m, "install_path", sv_str("/usr/share/gnome-shell/extensions/OriginUICC@quandz24.extension")); sv_map_set(__m, "scope", sv_str("system")); sv_map_set(__m, "description", sv_str("Origin control center")); sv_map_set(__m, "channel", sv_str("stable")); sv_map_set(__m, "url", sv_str("https://updates.infinityos.uk/origin-cc-1.0.0.tar.xz")); sv_map_set(__m, "sha256", sha_b); sv_map_set(__m, "depends", sv_arr_new(1, (SVal[]){({ SVal __m = sv_map_new(); sv_map_set(__m, "id", sv_str("origin-core")); sv_map_set(__m, "min_version", sv_str(">=1.1.0")); __m; })})); __m; })); __m; });
}
SVal __st_fn_test_sample_catalogue(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return test_sample_catalogue(); }

SVal test_semver_suite(void) {
    sv_assert(sv_bool(sv_eq(compare_semver(sv_str("1.2.3"), sv_str("1.2.3")), sv_num(0))), sv_str("semver eq"));
    sv_assert(sv_bool(sv_eq(compare_semver(sv_str("1.2.3"), sv_str("1.2.4")), sv_num(-1))), sv_str("semver lt"));
    sv_assert(sv_bool(sv_eq(compare_semver(sv_str("2.0.0"), sv_str("1.9.9")), sv_num(1))), sv_str("semver gt"));
    sv_assert(sv_bool(sv_eq(compare_semver(sv_str("1.0.0-beta.2"), sv_str("1.0.0")), sv_num(-1))), sv_str("semver prerelease"));
    sv_assert(sv_bool(sv_eq(compare_semver(sv_str("v1.0.0"), sv_str("1.0.0")), sv_num(0))), sv_str("semver v-prefix"));
    sv_assert(satisfies_constraint(sv_str("1.4.0"), sv_str("^1.2.0")), sv_str("caret ok"));
    sv_assert(sv_bool(!sv_truthy(satisfies_constraint(sv_str("2.0.0"), sv_str("^1.2.0")))), sv_str("caret major reject"));
    sv_assert(satisfies_constraint(sv_str("1.2.9"), sv_str("~1.2.3")), sv_str("tilde ok"));
    sv_assert(sv_bool(!sv_truthy(satisfies_constraint(sv_str("1.3.0"), sv_str("~1.2.3")))), sv_str("tilde minor reject"));
    sv_assert(sv_bool(sv_eq(classify_transition(sv_str(""), sv_str("1.0.0")), sv_str("install"))), sv_str("classify install"));
    sv_assert(sv_bool(sv_eq(classify_transition(sv_str("1.0.0"), sv_str("1.1.0")), sv_str("upgrade"))), sv_str("classify upgrade"));
    sv_assert(sv_bool(sv_eq(classify_transition(sv_str("1.1.0"), sv_str("1.0.0")), sv_str("downgrade"))), sv_str("classify downgrade"));
    sv_assert(sv_bool(sv_eq(classify_transition(sv_str("1.0.0"), sv_str("1.0.0")), sv_str("noop"))), sv_str("classify noop"));
    return sv_bool(true);
}
SVal __st_fn_test_semver_suite(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return test_semver_suite(); }

SVal test_security_policy_suite(void) {
    SVal ok = validate_install_path(sv_str("/usr/share/icons/Radience"), sv_str("system"));
    sv_assert(sv_get(ok, sv_str("ok")), sv_str("valid system icon path"));
    SVal bad_etc = validate_install_path(sv_str("/etc/shadow"), sv_str("system"));
    sv_assert(sv_bool(!sv_truthy(sv_get(bad_etc, sv_str("ok")))), sv_str("block /etc"));
    SVal bad_trav = validate_install_path(sv_str("/usr/share/icons/../../etc/passwd"), sv_str("system"));
    sv_assert(sv_bool(!sv_truthy(sv_get(bad_trav, sv_str("ok")))), sv_str("block traversal"));
    SVal bad_slash = validate_install_path(sv_str("/usr/share//icons/Radience"), sv_str("system"));
    sv_assert(sv_bool(!sv_truthy(sv_get(bad_slash, sv_str("ok")))), sv_str("block duplicate slash"));
    SVal user_ok = validate_install_path(sv_str("~/.local/share/icons/Radience"), sv_str("user"));
    sv_assert(sv_get(user_ok, sv_str("ok")), sv_str("valid user path"));
    SVal zip_ok = validate_archive_manifest(sv_arr_new(2, (SVal[]){sv_str("usr/file.txt"), sv_str("meta/info.json")}));
    sv_assert(sv_get(zip_ok, sv_str("ok")), sv_str("valid archive entries"));
    SVal zip_bad = validate_archive_manifest(sv_arr_new(2, (SVal[]){sv_str("ok/file.txt"), sv_str("../escape")}));
    sv_assert(sv_bool(!sv_truthy(sv_get(zip_bad, sv_str("ok")))), sv_str("block zip-slip"));
    sv_assert(validate_sha256(sv_sha256(sv_str("infinityos"))), sv_str("valid sha256 output"));
    return sv_bool(true);
}
SVal __st_fn_test_security_policy_suite(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return test_security_policy_suite(); }

SVal test_solver_and_reconciler_suite(void) {
    SVal cat = test_sample_catalogue();
    SVal cat_chk = validate_catalogue(cat);
    sv_assert(sv_bool((sv_truthy(sv_get(cat_chk, sv_str("ok"))) && sv_eq(sv_get(cat_chk, sv_str("count")), sv_num(2)))), sv_str("catalogue valid"));
    SVal plan = plan_reconcile(sv_map_new(), cat, sv_arr_new(1, (SVal[]){sv_str("origin-cc")}));
    sv_assert(sv_get(plan, sv_str("ok")), sv_str("reconcile plan ok"));
    sv_assert(sv_bool(sv_eq(sv_len(sv_get(plan, sv_str("actions"))), sv_num(2))), sv_str("reconcile installs dep first"));
    sv_assert(sv_bool(sv_eq(sv_get(sv_get(sv_get(plan, sv_str("actions")), sv_num(0)), sv_str("id")), sv_str("origin-core"))), sv_str("dep ordered first"));
    sv_assert(sv_bool(sv_eq(sv_get(sv_get(sv_get(plan, sv_str("actions")), sv_num(1)), sv_str("id")), sv_str("origin-cc"))), sv_str("target ordered second"));
    SVal cyclic = ({ SVal __m = sv_map_new(); sv_map_set(__m, "a", ({ SVal __m = sv_map_new(); sv_map_set(__m, "version", sv_str("1.0.0")); sv_map_set(__m, "install_path", sv_str("/usr/share/icons/A")); sv_map_set(__m, "scope", sv_str("system")); sv_map_set(__m, "depends", sv_arr_new(1, (SVal[]){({ SVal __m = sv_map_new(); sv_map_set(__m, "id", sv_str("b")); __m; })})); __m; })); sv_map_set(__m, "b", ({ SVal __m = sv_map_new(); sv_map_set(__m, "version", sv_str("1.0.0")); sv_map_set(__m, "install_path", sv_str("/usr/share/icons/B")); sv_map_set(__m, "scope", sv_str("system")); sv_map_set(__m, "depends", sv_arr_new(1, (SVal[]){({ SVal __m = sv_map_new(); sv_map_set(__m, "id", sv_str("a")); __m; })})); __m; })); __m; });
    SVal cyc_res = resolve_install_order(cyclic, sv_arr_new(1, (SVal[]){sv_str("a")}));
    sv_assert(sv_bool(((!sv_truthy(sv_get(cyc_res, sv_str("ok")))) && sv_truthy(sv_contains(sv_get(cyc_res, sv_str("error")), sv_str("cycle"))))), sv_str("cycle detected"));
    SVal installed = ({ SVal __m = sv_map_new(); sv_map_set(__m, "origin-core", sv_str("1.1.0")); sv_map_set(__m, "origin-cc", sv_str("1.0.0")); __m; });
    SVal snap = ({ SVal __m = sv_map_new(); sv_map_set(__m, "origin-core", sv_str("1.0.0")); __m; });
    SVal sp = plan_system_snapshot_transition(installed, cat, snap);
    sv_assert(sv_bool((sv_truthy(sv_get(sp, sv_str("ok"))) && sv_eq(sv_len(sv_get(sp, sv_str("actions"))), sv_num(2)))), sv_str("snapshot transition count"));
    sv_assert(sv_bool((sv_eq(sv_get(sv_get(sv_get(sp, sv_str("actions")), sv_num(0)), sv_str("id")), sv_str("origin-cc")) && sv_eq(sv_get(sv_get(sv_get(sp, sv_str("actions")), sv_num(0)), sv_str("action")), sv_str("remove")))), sv_str("snapshot removes newer"));
    sv_assert(sv_bool((sv_eq(sv_get(sv_get(sv_get(sp, sv_str("actions")), sv_num(1)), sv_str("id")), sv_str("origin-core")) && sv_eq(sv_get(sv_get(sv_get(sp, sv_str("actions")), sv_num(1)), sv_str("action")), sv_str("downgrade")))), sv_str("snapshot downgrades"));
    return sv_bool(true);
}
SVal __st_fn_test_solver_and_reconciler_suite(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return test_solver_and_reconciler_suite(); }

SVal test_cli_db_and_ui_suite(void) {
    SVal cat = test_sample_catalogue();
    SVal st = db_default_state();
    SVal res = cli_dispatch(sv_arr_new(2, (SVal[]){sv_str("install"), sv_str("origin-cc")}), st, cat, sv_num(1700000000));
    sv_assert(sv_bool(((sv_eq(sv_get(res, sv_str("exit_code")), sv_num(0)) && sv_eq(sv_len(sv_get(res, sv_str("actions"))), sv_num(2))) && sv_truthy(sv_get(res, sv_str("save_state"))))), sv_str("cli install"));
    SVal rb = cli_dispatch(sv_arr_new(1, (SVal[]){sv_str("rollback")}), sv_get(res, sv_str("state")), cat, sv_num(1700000010));
    sv_assert(sv_bool(sv_eq(sv_get(rb, sv_str("exit_code")), sv_num(0))), sv_str("cli rollback"));
    SVal list_after = cli_dispatch(sv_arr_new(2, (SVal[]){sv_str("list"), sv_str("--json")}), sv_get(rb, sv_str("state")), cat, sv_num(1700000020));
    sv_assert(sv_bool(sv_eq(sv_get(list_after, sv_str("output")), sv_str("{}"))), sv_str("rollback cleared installed"));
    SVal ui0 = ui_initial_state();
    SVal ui1 = ui_reduce(ui0, ({ SVal __m = sv_map_new(); sv_map_set(__m, "type", sv_str("toggle_select")); sv_map_set(__m, "id", sv_str("origin-cc")); __m; }), db_default_state(), cat);
    SVal ui2 = ui_reduce(ui1, ({ SVal __m = sv_map_new(); sv_map_set(__m, "type", sv_str("preview_plan")); __m; }), db_default_state(), cat);
    sv_assert(sv_bool(sv_eq(sv_get(ui2, sv_str("banner_level")), sv_str("success"))), sv_str("ui preview plan success"));
    return sv_bool(true);
}
SVal __st_fn_test_cli_db_and_ui_suite(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return test_cli_db_and_ui_suite(); }

SVal test_helper_policy_suite(void) {
    SVal bad = helper_execute_request(sv_json_stringify(({ SVal __m = sv_map_new(); sv_map_set(__m, "op", sv_str("install")); sv_map_set(__m, "component_id", sv_str("origin-core")); sv_map_set(__m, "scope", sv_str("system")); sv_map_set(__m, "install_path", sv_str("/etc/passwd")); sv_map_set(__m, "archive_path", sv_str("x.tar.xz")); __m; })));
    sv_assert(sv_bool(sv_eq(sv_get(bad, sv_str("exit_code")), sv_num(2))), sv_str("helper rejects /etc/passwd"));
    return sv_bool(true);
}
SVal __st_fn_test_helper_policy_suite(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return test_helper_policy_suite(); }

SVal run_all_steel_self_tests(void) {
    sv_assert(test_semver_suite(), sv_str("semver suite"));
    sv_assert(test_security_policy_suite(), sv_str("security suite"));
    sv_assert(test_solver_and_reconciler_suite(), sv_str("solver suite"));
    sv_assert(test_cli_db_and_ui_suite(), sv_str("cli/db/ui suite"));
    sv_assert(test_helper_policy_suite(), sv_str("helper suite"));
    return sv_bool(true);
}
SVal __st_fn_run_all_steel_self_tests(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return run_all_steel_self_tests(); }

SVal steel_compare_semver(SVal a, SVal b) {
    return compare_semver(a, b);
}
SVal __st_fn_steel_compare_semver(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_compare_semver((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal steel_satisfies_constraint(SVal ver, SVal constraint) {
    return satisfies_constraint(ver, constraint);
}
SVal __st_fn_steel_satisfies_constraint(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_satisfies_constraint((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal steel_validate_path_json(SVal path, SVal scope) {
    return sv_json_stringify(validate_install_path(path, scope));
}
SVal __st_fn_steel_validate_path_json(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_validate_path_json((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null())); }

SVal steel_validate_archive_json(SVal entries_json) {
    SVal entries = sv_json_parse(entries_json);
    return sv_json_stringify(validate_archive_manifest(entries));
}
SVal __st_fn_steel_validate_archive_json(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_validate_archive_json((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal steel_validate_catalogue_json(SVal catalogue_json) {
    SVal cat = sv_json_parse(catalogue_json);
    return sv_json_stringify(validate_catalogue(cat));
}
SVal __st_fn_steel_validate_catalogue_json(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_validate_catalogue_json((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal steel_plan_reconcile(SVal installed_json, SVal catalogue_json, SVal targets_json) {
    SVal installed = sv_json_parse(installed_json);
    SVal catalogue = sv_json_parse(catalogue_json);
    SVal targets = sv_json_parse(targets_json);
    return sv_json_stringify(plan_reconcile(installed, catalogue, targets));
}
SVal __st_fn_steel_plan_reconcile(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_plan_reconcile((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal steel_plan_remove(SVal installed_json, SVal catalogue_json, SVal target_id, SVal force) {
    SVal installed = sv_json_parse(installed_json);
    SVal catalogue = sv_json_parse(catalogue_json);
    return sv_json_stringify(plan_remove(installed, catalogue, target_id, force));
}
SVal __st_fn_steel_plan_remove(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_plan_remove((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null()), (_st_argc > 3 ? _st_argv[3] : sv_null())); }

SVal steel_cli_dispatch(SVal args_json, SVal state_json, SVal catalogue_json, SVal timestamp) {
    SVal args = sv_json_parse(args_json);
    SVal state = (((sv_len(sv_trim(state_json))).num > 0) ? (sv_json_parse(state_json)) : (db_default_state()));
    SVal catalogue = (((sv_len(sv_trim(catalogue_json))).num > 0) ? (sv_json_parse(catalogue_json)) : (sv_map_new()));
    return sv_json_stringify(cli_dispatch(args, state, catalogue, timestamp));
}
SVal __st_fn_steel_cli_dispatch(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_cli_dispatch((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null()), (_st_argc > 3 ? _st_argv[3] : sv_null())); }

SVal steel_ui_reduce(SVal ui_json, SVal event_json, SVal state_json, SVal catalogue_json) {
    SVal ui = (((sv_len(sv_trim(ui_json))).num > 0) ? (sv_json_parse(ui_json)) : (ui_initial_state()));
    SVal ev = sv_json_parse(event_json);
    SVal st = (((sv_len(sv_trim(state_json))).num > 0) ? (sv_json_parse(state_json)) : (db_default_state()));
    SVal cat = (((sv_len(sv_trim(catalogue_json))).num > 0) ? (sv_json_parse(catalogue_json)) : (sv_map_new()));
    return sv_json_stringify(ui_reduce(ui, ev, st, cat));
}
SVal __st_fn_steel_ui_reduce(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_ui_reduce((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null()), (_st_argc > 3 ? _st_argv[3] : sv_null())); }

SVal steel_build_manifest_json(SVal id, SVal version, SVal scope, SVal install_path, SVal sha256, SVal signature, SVal files_json) {
    SVal files = sv_json_parse(files_json);
    return sv_json_stringify(build_package_manifest(id, version, scope, install_path, sha256, signature, files));
}
SVal __st_fn_steel_build_manifest_json(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_build_manifest_json((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null()), (_st_argc > 3 ? _st_argv[3] : sv_null()), (_st_argc > 4 ? _st_argv[4] : sv_null()), (_st_argc > 5 ? _st_argv[5] : sv_null()), (_st_argc > 6 ? _st_argv[6] : sv_null())); }

SVal steel_publish_entry_json(SVal catalogue_json, SVal id, SVal entry_json) {
    SVal cat = (((sv_len(sv_trim(catalogue_json))).num > 0) ? (sv_json_parse(catalogue_json)) : (sv_map_new()));
    SVal entry = sv_json_parse(entry_json);
    return sv_json_stringify(publish_catalogue_entry(cat, id, entry));
}
SVal __st_fn_steel_publish_entry_json(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_publish_entry_json((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal steel_validate_helper_request_json(SVal req_json) {
    SVal req = sv_json_parse(req_json);
    return sv_json_stringify(validate_helper_request(req));
}
SVal __st_fn_steel_validate_helper_request_json(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_validate_helper_request_json((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal steel_helper_run_json(SVal req_json) {
    return sv_json_stringify(helper_execute_request(req_json));
}
SVal __st_fn_steel_helper_run_json(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_helper_run_json((_st_argc > 0 ? _st_argv[0] : sv_null())); }

SVal steel_runner_cli_json(void) {
    return sv_json_stringify(runner_cli_main());
}
SVal __st_fn_steel_runner_cli_json(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_runner_cli_json(); }

SVal steel_runner_publish_json(SVal cat_path, SVal id, SVal entry_path) {
    return sv_json_stringify(runner_publish_catalogue(cat_path, id, entry_path));
}
SVal __st_fn_steel_runner_publish_json(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_runner_publish_json((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal steel_plan_snapshot_json(SVal installed_json, SVal catalogue_json, SVal snapshot_json) {
    SVal installed = sv_json_parse(installed_json);
    SVal catalogue = sv_json_parse(catalogue_json);
    SVal snapshot = sv_json_parse(snapshot_json);
    return sv_json_stringify(plan_system_snapshot_transition(installed, catalogue, snapshot));
}
SVal __st_fn_steel_plan_snapshot_json(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_plan_snapshot_json((_st_argc > 0 ? _st_argv[0] : sv_null()), (_st_argc > 1 ? _st_argv[1] : sv_null()), (_st_argc > 2 ? _st_argv[2] : sv_null())); }

SVal steel_run_self_tests(void) {
    return run_all_steel_self_tests();
}
SVal __st_fn_steel_run_self_tests(SVal __st_this, int _st_argc, SVal* _st_argv) { (void)__st_this; (void)_st_argc; (void)_st_argv; return steel_run_self_tests(); }

int main(int argc, char** argv) {
    __st_argc = argc; __st_argv = argv;
    sv_cleanup();
    return 0;
}
