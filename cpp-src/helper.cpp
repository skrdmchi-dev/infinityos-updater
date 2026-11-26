// Thin C++20 privileged pkexec helper (non-Steel C++20 adapter rewritten by Gemini).
// Policy, dpkg -S ownership check, SHA-256, and on-disk backup/restore run in Steel.
#define main __steel_unused_main
#include "../cpp-core/generated.cpp"
#undef main

int main() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    std::string raw = ss.str();

    steel::Val res = steel::json_parse_val(steel_helper_run_json(steel::Val(raw)));
    int code = static_cast<int>(steel::idx_get(res, steel::Val("exit_code")).as_num());
    if (code != 0) {
        steel::Val err = steel::idx_get(res, steel::Val("error"));
        if (err.is_str()) std::cerr << "helper: " << err.to_string() << "\n";
        return code;
    }

    steel::Val ext = steel::idx_get(res, steel::Val("extract"));
    if (ext.is_map()) {
        std::string arc = steel::idx_get(ext, steel::Val("archive")).to_string();
        std::string dst = steel::idx_get(ext, steel::Val("dest")).to_string();
        std::string cmd = "tar -xJf \"" + arc + "\" -C \"" + dst + "\"";
        if (std::system(cmd.c_str()) != 0) {
            std::string bp = steel::idx_get(res, steel::Val("backup_path")).to_string();
            if (!bp.empty()) {
                std::string rb = "{\"op\":\"restore_backup\",\"install_path\":\"" + dst + "\",\"backup_path\":\"" + bp + "\"}";
                (void)steel_helper_run_json(steel::Val(rb));
            }
            std::cerr << "helper: tar.xz extraction failed (backup restored)\n";
            return 1;
        }
    }
    return 0;
}
