// Thin C++20 packager and catalogue publisher CLI (non-Steel C++20 adapter rewritten by Gemini).
// Catalogue validation and publishing run in Steel (pkg.steel + runner.steel).
#define main __steel_unused_main
#include "../cpp-core/generated.cpp"
#undef main

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage:\n  infinity-pkg pack <src_dir> <out.tar.xz>\n  infinity-pkg publish <catalogue.json> <id> <entry.json>\n";
        return 2;
    }
    std::string cmd = argv[1];
    if (cmd == "pack" && argc >= 4) {
        std::string tar_cmd = std::string("tar -cJf \"") + argv[3] + "\" -C \"" + argv[2] + "\" .";
        if (std::system(tar_cmd.c_str()) != 0) {
            std::cerr << "pack failed\n";
            return 1;
        }
        steel::Val sha = steel::sha256_file_val(steel::Val(argv[3]));
        std::cout << "{\"archive\":\"" << argv[3] << "\",\"sha256\":\"" << sha.to_string() << "\"}\n";
        return 0;
    }
    if (cmd == "publish" && argc >= 5) {
        steel::Val res = steel::json_parse_val(
            steel_runner_publish_json(steel::Val(argv[2]), steel::Val(argv[3]), steel::Val(argv[4]))
        );
        steel::Val out = steel::idx_get(res, steel::Val("output"));
        if (out.is_str()) std::cout << out.to_string() << "\n";
        return static_cast<int>(steel::idx_get(res, steel::Val("exit_code")).as_num());
    }
    std::cerr << "Unknown command. Use 'pack' or 'publish'.\n";
    return 2;
}
