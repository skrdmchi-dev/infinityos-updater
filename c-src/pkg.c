/* Thin C11 packager and catalogue publisher CLI (Gemini AI-assisted C11 adapter).
 * Catalogue validation and publishing run in Steel (pkg.steel + runner.steel).
 */
#define main __steel_unused_main
#include "../c-core/generated.c"
#undef main

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage:\n  infinity-pkg pack <src_dir> <out.tar.xz>\n  infinity-pkg publish <catalogue.json> <id> <entry.json>\n");
        return 2;
    }
    if (strcmp(argv[1], "pack") == 0 && argc >= 4) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "tar -cJf \"%s\" -C \"%s\" .", argv[3], argv[2]);
        if (system(cmd) != 0) {
            fprintf(stderr, "pack failed\n");
            sv_cleanup();
            return 1;
        }
        SVal sha = sv_sha256_file(sv_str(argv[3]));
        printf("{\"archive\":\"%s\",\"sha256\":\"%s\"}\n", argv[3], sha.type == ST_STR ? sha.str : "");
        sv_cleanup();
        return 0;
    }
    if (strcmp(argv[1], "publish") == 0 && argc >= 5) {
        SVal res = sv_json_parse(steel_runner_publish_json(sv_str(argv[2]), sv_str(argv[3]), sv_str(argv[4])));
        SVal out = sv_map_get(res, "output");
        if (out.type == ST_STR && out.str) printf("%s\n", out.str);
        int code = (int)sv_map_get(res, "exit_code").num;
        sv_cleanup();
        return code;
    }
    fprintf(stderr, "Unknown command. Use 'pack' or 'publish'.\n");
    sv_cleanup();
    return 2;
}
