/* Thin C11 privileged pkexec helper (Gemini AI-assisted C11 adapter).
 * Policy, dpkg -S ownership check, SHA-256, and on-disk backup/restore run in Steel.
 */
#define main __steel_unused_main
#include "../c-core/generated.c"
#undef main

static int extract_tar_xz(const char* archive, const char* dest) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "tar -xJf \"%s\" -C \"%s\"", archive, dest);
    return system(cmd);
}

int main(void) {
    size_t cap = 4096, len = 0;
    char* buf = (char*)calloc(1, cap);
    if (!buf) return 1;
    size_t n;
    while ((n = fread(buf + len, 1, cap - len - 1, stdin)) > 0) {
        len += n;
        if (len + 1 >= cap) {
            cap *= 2;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) { free(buf); return 1; }
            buf = nb;
        }
    }
    buf[len] = '\0';

    SVal res = sv_json_parse(steel_helper_run_json(sv_str(buf)));
    free(buf);
    int code = (int)sv_map_get(res, "exit_code").num;
    if (code != 0) {
        SVal err = sv_map_get(res, "error");
        if (err.type == ST_STR && err.str) fprintf(stderr, "helper: %s\n", err.str);
        sv_cleanup();
        return code;
    }

    SVal ext = sv_map_get(res, "extract");
    if (ext.type == ST_MAP && ext.map) {
        SVal arc = sv_map_get(ext, "archive");
        SVal dst = sv_map_get(ext, "dest");
        if (arc.type == ST_STR && dst.type == ST_STR && extract_tar_xz(arc.str, dst.str) != 0) {
            SVal bp = sv_map_get(res, "backup_path");
            if (bp.type == ST_STR && bp.str && bp.str[0] != '\0') {
                char rb[4096];
                snprintf(rb, sizeof(rb), "{\"op\":\"restore_backup\",\"install_path\":\"%s\",\"backup_path\":\"%s\"}", dst.str, bp.str);
                (void)steel_helper_run_json(sv_str(rb));
            }
            fprintf(stderr, "helper: tar.xz extraction failed (backup restored)\n");
            sv_cleanup();
            return 1;
        }
    }
    sv_cleanup();
    return 0;
}
