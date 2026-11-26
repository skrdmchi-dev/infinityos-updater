/* Thin C11 CLI runner for InfinityOS Updater (Gemini AI-assisted C11 adapter).
 * Core logic is compiled from steel-core/*.steel into c-core/generated.c.
 */
#define main __steel_unused_main
#include "../c-core/generated.c"
#undef main

int main(int argc, char** argv) {
    __st_argc = argc;
    __st_argv = argv;
    SVal res_json = steel_runner_cli_json();
    SVal parsed = sv_json_parse(res_json);
    int code = (int)sv_map_get(parsed, "exit_code").num;
    sv_cleanup();
    return code;
}
