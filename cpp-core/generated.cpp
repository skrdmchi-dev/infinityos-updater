
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace steel {

struct Val;
using Arr = std::vector<Val>;
using ArrPtr = std::shared_ptr<Arr>;
using Map = std::unordered_map<std::string, Val>;
using MapPtr = std::shared_ptr<Map>;
using Fn = std::function<Val(Val, const std::vector<Val>&)>;

struct Error : std::runtime_error {
    explicit Error(const std::string& msg) : std::runtime_error(msg) {}
};

struct Val {
    using Storage = std::variant<std::monostate, bool, double, std::string, ArrPtr, MapPtr, Fn>;
    Storage data;

    Val() : data(std::monostate{}) {}
    Val(std::nullptr_t) : data(std::monostate{}) {}
    Val(bool b) : data(b) {}
    Val(int n) : data(static_cast<double>(n)) {}
    Val(double n) : data(n) {}
    Val(const char* s) : data(std::string(s)) {}
    Val(std::string s) : data(std::move(s)) {}
    Val(ArrPtr a) : data(std::move(a)) {}
    Val(MapPtr m) : data(std::move(m)) {}
    Val(Fn f) : data(std::move(f)) {}

    bool is_null() const { return std::holds_alternative<std::monostate>(data); }
    bool is_bool() const { return std::holds_alternative<bool>(data); }
    bool is_num() const { return std::holds_alternative<double>(data); }
    bool is_str() const { return std::holds_alternative<std::string>(data); }
    bool is_arr() const { return std::holds_alternative<ArrPtr>(data); }
    bool is_map() const { return std::holds_alternative<MapPtr>(data); }
    bool is_fn() const { return std::holds_alternative<Fn>(data); }

    double as_num() const {
        if (auto* p = std::get_if<double>(&data)) return *p;
        if (auto* b = std::get_if<bool>(&data)) return *b ? 1.0 : 0.0;
        if (auto* s = std::get_if<std::string>(&data)) {
            try { return std::stod(*s); } catch (...) { return 0.0; }
        }
        return 0.0;
    }

    bool truthy() const {
        if (is_null()) return false;
        if (auto* b = std::get_if<bool>(&data)) return *b;
        if (auto* n = std::get_if<double>(&data)) return *n != 0.0;
        if (auto* s = std::get_if<std::string>(&data)) return !s->empty();
        return true;
    }

    std::string to_string() const {
        if (is_null()) return "null";
        if (auto* b = std::get_if<bool>(&data)) return *b ? "true" : "false";
        if (auto* n = std::get_if<double>(&data)) {
            if (std::floor(*n) == *n && std::abs(*n) < 1e15) {
                return std::to_string(static_cast<long long>(*n));
            }
            std::ostringstream oss;
            oss << *n;
            return oss.str();
        }
        if (auto* s = std::get_if<std::string>(&data)) return *s;
        if (auto* a = std::get_if<ArrPtr>(&data)) {
            std::string out = "[";
            for (size_t i = 0; i < (*a)->size(); ++i) {
                if (i > 0) out += ", ";
                out += (**a)[i].to_string();
            }
            out += "]";
            return out;
        }
        if (is_map()) return "[object]";
        return "[fn]";
    }

    Val operator+(const Val& rhs) const {
        if (is_str() || rhs.is_str()) return Val(to_string() + rhs.to_string());
        return Val(as_num() + rhs.as_num());
    }
    Val operator-(const Val& rhs) const { return Val(as_num() - rhs.as_num()); }
    Val operator*(const Val& rhs) const { return Val(as_num() * rhs.as_num()); }
    Val operator/(const Val& rhs) const { return Val(as_num() / rhs.as_num()); }
    Val operator%(const Val& rhs) const { return Val(std::fmod(as_num(), rhs.as_num())); }
    Val operator-() const { return Val(-as_num()); }

    bool operator==(const Val& rhs) const {
        if (data.index() != rhs.data.index()) return false;
        if (is_null()) return true;
        if (is_bool()) return std::get<bool>(data) == std::get<bool>(rhs.data);
        if (is_num()) return std::get<double>(data) == std::get<double>(rhs.data);
        if (is_str()) return std::get<std::string>(data) == std::get<std::string>(rhs.data);
        if (is_arr()) return std::get<ArrPtr>(data) == std::get<ArrPtr>(rhs.data);
        if (is_map()) return std::get<MapPtr>(data) == std::get<MapPtr>(rhs.data);
        return false;
    }
    bool operator!=(const Val& rhs) const { return !(*this == rhs); }
    bool operator<(const Val& rhs) const { return as_num() < rhs.as_num(); }
    bool operator>(const Val& rhs) const { return as_num() > rhs.as_num(); }
    bool operator<=(const Val& rhs) const { return as_num() <= rhs.as_num(); }
    bool operator>=(const Val& rhs) const { return as_num() >= rhs.as_num(); }

    Val call(const std::vector<Val>& args, const Val& self = Val()) const {
        if (auto* f = std::get_if<Fn>(&data)) return (*f)(self, args);
        return Val();
    }
};

inline Val make_arr(std::initializer_list<Val> items) {
    return Val(std::make_shared<Arr>(items));
}

inline Val make_map(std::initializer_list<std::pair<const std::string, Val>> entries = {}) {
    auto m = std::make_shared<Map>();
    for (const auto& kv : entries) (*m)[kv.first] = kv.second;
    return Val(m);
}

inline Val map_get(const Val& obj, const std::string& key) {
    if (auto* m = std::get_if<MapPtr>(&obj.data)) {
        auto it = (*m)->find(key);
        if (it != (*m)->end()) return it->second;
    }
    return Val();
}

inline void map_set(const Val& obj, const std::string& key, const Val& val) {
    if (auto* m = std::get_if<MapPtr>(&obj.data)) {
        (**m)[key] = val;
    }
}

inline Val call_method(const Val& obj, const std::string& name, const std::vector<Val>& args) {
    Val m = map_get(obj, name);
    return m.call(args, obj);
}

inline Val idx_get(const Val& t, const Val& idx) {
    if (t.is_map() && idx.is_str()) return map_get(t, std::get<std::string>(idx.data));
    if (auto* a = std::get_if<ArrPtr>(&t.data)) {
        long long i = static_cast<long long>(idx.as_num());
        long long n = static_cast<long long>((*a)->size());
        if (i < 0) i += n;
        if (i >= 0 && i < n) return (**a)[static_cast<size_t>(i)];
    }
    return Val();
}

inline void idx_set(const Val& t, const Val& idx, const Val& val) {
    if (t.is_map() && idx.is_str()) {
        map_set(t, std::get<std::string>(idx.data), val);
        return;
    }
    if (auto* a = std::get_if<ArrPtr>(&t.data)) {
        long long i = static_cast<long long>(idx.as_num());
        long long n = static_cast<long long>((*a)->size());
        if (i < 0) i += n;
        if (i >= 0 && i < n) (**a)[static_cast<size_t>(i)] = val;
    }
}

inline Val print_vals(const std::vector<Val>& args) {
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) std::cout << ' ';
        std::cout << args[i].to_string();
    }
    std::cout << '\n';
    return Val();
}

inline Val len_of(const Val& v) {
    if (auto* a = std::get_if<ArrPtr>(&v.data)) return Val(static_cast<double>((*a)->size()));
    if (auto* s = std::get_if<std::string>(&v.data)) return Val(static_cast<double>(s->size()));
    if (auto* m = std::get_if<MapPtr>(&v.data)) return Val(static_cast<double>((*m)->size()));
    return Val(0.0);
}

inline Val push_val(const Val& arr, const Val& item) {
    if (auto* a = std::get_if<ArrPtr>(&arr.data)) (*a)->push_back(item);
    return arr;
}

inline Val pop_val(const Val& arr) {
    if (auto* a = std::get_if<ArrPtr>(&arr.data)) {
        if (!(*a)->empty()) {
            Val v = (*a)->back();
            (*a)->pop_back();
            return v;
        }
    }
    return Val();
}

inline Val range_val(const Val& a, const Val& b, const Val& step_v = Val(1.0)) {
    auto out = std::make_shared<Arr>();
    long long s = static_cast<long long>(a.as_num());
    long long e = static_cast<long long>(b.as_num());
    long long step = static_cast<long long>(step_v.as_num());
    if (step > 0) {
        for (long long i = s; i < e; i += step) out->push_back(Val(static_cast<double>(i)));
    } else if (step < 0) {
        for (long long i = s; i > e; i += step) out->push_back(Val(static_cast<double>(i)));
    }
    return Val(out);
}

inline std::vector<Val> iter_vec(const Val& v) {
    if (auto* a = std::get_if<ArrPtr>(&v.data)) return **a;
    return {};
}

inline Val map_hof(const Val& arr, const Val& fn) {
    auto out = std::make_shared<Arr>();
    for (const auto& item : iter_vec(arr)) out->push_back(fn.call({item}));
    return Val(out);
}

inline Val filter_hof(const Val& arr, const Val& fn) {
    auto out = std::make_shared<Arr>();
    for (const auto& item : iter_vec(arr)) {
        if (fn.call({item}).truthy()) out->push_back(item);
    }
    return Val(out);
}

inline Val reduce_hof(const Val& arr, const Val& fn, const Val& init, bool has_init) {
    auto items = iter_vec(arr);
    size_t start = 0;
    Val acc = init;
    if (!has_init) {
        if (items.empty()) return Val();
        acc = items[0];
        start = 1;
    }
    for (size_t i = start; i < items.size(); ++i) acc = fn.call({acc, items[i]});
    return acc;
}

inline Val find_hof(const Val& arr, const Val& fn) {
    for (const auto& item : iter_vec(arr)) {
        if (fn.call({item}).truthy()) return item;
    }
    return Val();
}

inline Val any_hof(const Val& arr, const Val& fn) {
    for (const auto& item : iter_vec(arr)) {
        if (fn.call({item}).truthy()) return Val(true);
    }
    return Val(false);
}

inline Val all_hof(const Val& arr, const Val& fn) {
    for (const auto& item : iter_vec(arr)) {
        if (!fn.call({item}).truthy()) return Val(false);
    }
    return Val(true);
}

inline Val type_of(const Val& v) {
    if (std::holds_alternative<std::monostate>(v.data)) return Val("null");
    if (std::holds_alternative<bool>(v.data)) return Val("boolean");
    if (std::holds_alternative<double>(v.data)) return Val("number");
    if (std::holds_alternative<std::string>(v.data)) return Val("string");
    if (std::holds_alternative<ArrPtr>(v.data)) return Val("array");
    if (std::holds_alternative<MapPtr>(v.data)) return Val("map");
    return Val("function");
}

inline Val assert_val(const Val& cond, const Val& msg = Val("Assertion failed")) {
    if (!cond.truthy()) throw Error(msg.to_string());
    return Val(true);
}

inline Val has_key_val(const Val& m, const Val& k) {
    if (auto* mp = std::get_if<MapPtr>(&m.data)) return Val((*mp)->count(k.to_string()) > 0);
    return Val(false);
}

inline Val delete_key_val(const Val& m, const Val& k) {
    if (auto* mp = std::get_if<MapPtr>(&m.data)) return Val((*mp)->erase(k.to_string()) > 0);
    return Val(false);
}

inline Val concat_val(const Val& a, const Val& b) {
    auto out = std::make_shared<Arr>(iter_vec(a));
    auto bv = iter_vec(b);
    out->insert(out->end(), bv.begin(), bv.end());
    return Val(out);
}

inline Val index_of_val(const Val& h, const Val& n) {
    if (auto* a = std::get_if<ArrPtr>(&h.data)) {
        for (size_t i = 0; i < (*a)->size(); ++i) {
            if ((**a)[i] == n) return Val(static_cast<double>(i));
        }
        return Val(-1.0);
    }
    std::string hs = h.to_string(), ns = n.to_string();
    size_t pos = hs.find(ns);
    return Val(pos == std::string::npos ? -1.0 : static_cast<double>(pos));
}

inline Val repeat_str(const Val& s, const Val& n) {
    long long c = std::max(0LL, static_cast<long long>(n.as_num()));
    std::string src = s.to_string(), out;
    out.reserve(src.size() * static_cast<size_t>(c));
    for (long long i = 0; i < c; ++i) out += src;
    return Val(std::move(out));
}

inline Val pad_start_str(const Val& s, const Val& w, const Val& ch = Val(" ")) {
    std::string src = s.to_string(), p = ch.to_string();
    size_t width = static_cast<size_t>(std::max(0LL, static_cast<long long>(w.as_num())));
    if (src.size() >= width || p.empty()) return Val(std::move(src));
    size_t need = width - src.size();
    std::string out;
    out.reserve(width);
    for (size_t i = 0; i < need; ++i) out.push_back(p[i % p.size()]);
    out += src;
    return Val(std::move(out));
}

inline Val pad_end_str(const Val& s, const Val& w, const Val& ch = Val(" ")) {
    std::string src = s.to_string(), p = ch.to_string();
    size_t width = static_cast<size_t>(std::max(0LL, static_cast<long long>(w.as_num())));
    if (src.size() >= width || p.empty()) return Val(std::move(src));
    size_t need = width - src.size();
    src.reserve(width);
    for (size_t i = 0; i < need; ++i) src.push_back(p[i % p.size()]);
    return Val(std::move(src));
}

inline Val char_code_val(const Val& s) {
    std::string src = s.to_string();
    return Val(src.empty() ? 0.0 : static_cast<double>(static_cast<unsigned char>(src[0])));
}

inline Val from_char_code_val(const Val& n) {
    int c = std::max(0, static_cast<int>(n.as_num()));
    return Val(std::string(1, static_cast<char>(c)));
}

inline Val sum_val(const Val& arr) {
    double s = 0.0;
    for (const auto& x : iter_vec(arr)) {
        if (auto* d = std::get_if<double>(&x.data)) s += *d;
    }
    return Val(s);
}

inline Val avg_val(const Val& arr) {
    auto items = iter_vec(arr);
    if (items.empty()) return Val(0.0);
    double s = 0.0;
    for (const auto& x : items) {
        if (auto* d = std::get_if<double>(&x.data)) s += *d;
    }
    return Val(s / static_cast<double>(items.size()));
}

inline Val unique_val(const Val& arr) {
    auto out = std::make_shared<Arr>();
    for (const auto& x : iter_vec(arr)) {
        bool dup = false;
        for (const auto& u : *out) if (u == x) { dup = true; break; }
        if (!dup) out->push_back(x);
    }
    return Val(out);
}

inline Val flat_val(const Val& arr) {
    auto out = std::make_shared<Arr>();
    for (const auto& x : iter_vec(arr)) {
        if (auto* inner = std::get_if<ArrPtr>(&x.data)) {
            out->insert(out->end(), (*inner)->begin(), (*inner)->end());
        } else {
            out->push_back(x);
        }
    }
    return Val(out);
}

inline Val zip_val(const Val& a, const Val& b) {
    auto av = iter_vec(a), bv = iter_vec(b);
    size_t n = std::min(av.size(), bv.size());
    auto out = std::make_shared<Arr>();
    for (size_t i = 0; i < n; ++i) {
        auto pair = std::make_shared<Arr>(Arr{av[i], bv[i]});
        out->push_back(Val(pair));
    }
    return Val(out);
}

inline Val enumerate_val(const Val& a) {
    auto av = iter_vec(a);
    auto out = std::make_shared<Arr>();
    for (size_t i = 0; i < av.size(); ++i) {
        auto pair = std::make_shared<Arr>(Arr{Val(static_cast<double>(i)), av[i]});
        out->push_back(Val(pair));
    }
    return Val(out);
}

inline Val hash_val(const Val& v) {
    std::string s = v.to_string();
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char b : s) {
        h ^= static_cast<std::uint64_t>(b);
        h *= 0x0100000001b3ULL;
    }
    std::ostringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << h;
    return Val(ss.str());
}

inline std::string json_escape(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out.push_back(c);
    }
    out.push_back('"');
    return out;
}

inline std::string json_stringify_raw(const Val& v) {
    if (std::holds_alternative<std::monostate>(v.data)) return "null";
    if (auto* b = std::get_if<bool>(&v.data)) return *b ? "true" : "false";
    if (std::holds_alternative<double>(v.data)) return v.to_string();
    if (auto* s = std::get_if<std::string>(&v.data)) return json_escape(*s);
    if (auto* a = std::get_if<ArrPtr>(&v.data)) {
        std::string out = "[";
        for (size_t i = 0; i < (*a)->size(); ++i) {
            if (i > 0) out += ",";
            out += json_stringify_raw((**a)[i]);
        }
        out += "]";
        return out;
    }
    if (auto* m = std::get_if<MapPtr>(&v.data)) {
        std::string out = "{";
        bool first = true;
        for (const auto& kv : **m) {
            if (!first) out += ",";
            first = false;
            out += json_escape(kv.first) + ":" + json_stringify_raw(kv.second);
        }
        out += "}";
        return out;
    }
    return "null";
}

inline Val json_stringify_val(const Val& v) { return Val(json_stringify_raw(v)); }

inline Val json_parse_at(const std::string& s, size_t& i) {
    auto skip = [&]() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; };
    skip();
    if (i >= s.size()) return Val();
    if (s[i] == '"') {
        ++i;
        std::string out;
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') break;
            if (c == '\\' && i < s.size()) {
                char e = s[i++];
                if (e == 'n') out.push_back('\n');
                else if (e == 'r') out.push_back('\r');
                else if (e == 't') out.push_back('\t');
                else out.push_back(e);
            } else {
                out.push_back(c);
            }
        }
        return Val(std::move(out));
    }
    if (s[i] == '[') {
        ++i;
        auto arr = std::make_shared<Arr>();
        skip();
        if (i < s.size() && s[i] == ']') { ++i; return Val(arr); }
        while (i < s.size()) {
            arr->push_back(json_parse_at(s, i));
            skip();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == ']') ++i;
            break;
        }
        return Val(arr);
    }
    if (s[i] == '{') {
        ++i;
        auto mp = std::make_shared<Map>();
        skip();
        if (i < s.size() && s[i] == '}') { ++i; return Val(mp); }
        while (i < s.size()) {
            Val k = json_parse_at(s, i);
            skip();
            if (i < s.size() && s[i] == ':') ++i;
            (*mp)[k.to_string()] = json_parse_at(s, i);
            skip();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == '}') ++i;
            break;
        }
        return Val(mp);
    }
    if (s.compare(i, 4, "true") == 0) { i += 4; return Val(true); }
    if (s.compare(i, 5, "false") == 0) { i += 5; return Val(false); }
    if (s.compare(i, 4, "null") == 0) { i += 4; return Val(); }
    size_t st = i;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) ++i;
    try { return Val(std::stod(s.substr(st, i - st))); } catch (...) { return Val(0.0); }
}

inline Val json_parse_val(const Val& v) {
    std::string s = v.to_string();
    size_t i = 0;
    return json_parse_at(s, i);
}

inline Val append_file_str(const Val& p, const Val& c) {
    std::ofstream f(p.to_string(), std::ios::binary | std::ios::app);
    if (!f) return Val(false);
    f << c.to_string();
    return Val(true);
}

inline Val remove_file_val(const Val& p) {
    return Val(std::filesystem::remove(p.to_string()));
}

inline std::vector<std::string>& cli_args_store() {
    static std::vector<std::string> store;
    return store;
}

inline Val args_val() {
    auto arr = std::make_shared<Arr>();
    for (const auto& s : cli_args_store()) arr->push_back(Val(s));
    return Val(arr);
}

inline Val exit_val(const Val& c) {
    std::exit(static_cast<int>(c.as_num()));
}

inline Val mkdir_val(const Val& p) {
    std::error_code ec;
    std::filesystem::create_directories(p.to_string(), ec);
    return Val(!ec);
}

inline Val list_dir_val(const Val& p) {
    std::error_code ec;
    std::vector<std::string> names;
    for (const auto& e : std::filesystem::directory_iterator(p.to_string(), ec)) {
        names.push_back(e.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    auto arr = std::make_shared<Arr>();
    for (auto& n : names) arr->push_back(Val(std::move(n)));
    return Val(arr);
}

inline Val remove_dir_val(const Val& p) {
    std::error_code ec;
    std::filesystem::remove_all(p.to_string(), ec);
    return Val(!ec);
}

inline Val rename_val(const Val& a, const Val& b) {
    std::error_code ec;
    std::filesystem::rename(a.to_string(), b.to_string(), ec);
    return Val(!ec);
}

inline Val exec_val(const Val& cmd, const Val* argv) {
    std::string full = cmd.to_string();
    if (argv && argv->is_arr()) {
        for (const auto& a : *std::get<ArrPtr>(argv->data)) {
            full += " \"" + a.to_string() + "\"";
        }
    }
    std::string out;
#ifdef _WIN32
    FILE* pipe = _popen(full.c_str(), "r");
#else
    FILE* pipe = popen(full.c_str(), "r");
#endif
    int code = -1;
    if (pipe) {
        char buf[4096];
        while (fgets(buf, sizeof(buf), pipe)) out += buf;
#ifdef _WIN32
        code = _pclose(pipe);
#else
        code = pclose(pipe);
#endif
    }
    if (argv && argv->is_arr()) {
        auto m = std::make_shared<Map>();
        (*m)["code"] = Val(static_cast<double>(code));
        (*m)["stdout"] = Val(out);
        (*m)["stderr"] = Val(std::string(""));
        return Val(m);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return Val(out);
}

inline std::string sha256_bytes_hex(const std::string& input) {
    static const uint32_t primes[64] = {
        2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,
        59,61,67,71,73,79,83,89,97,101,103,107,109,113,127,131,
        137,139,149,151,157,163,167,173,179,181,191,193,197,199,211,223,
        227,229,233,239,241,251,257,263,269,271,277,281,283,293,307,311
    };
    uint32_t k[64];
    for (int i = 0; i < 64; ++i) {
        double r = std::cbrt(static_cast<double>(primes[i]));
        k[i] = static_cast<uint32_t>((r - std::floor(r)) * 4294967296.0);
    }
    uint32_t h[8];
    for (int i = 0; i < 8; ++i) {
        double r = std::sqrt(static_cast<double>(primes[i]));
        h[i] = static_cast<uint32_t>((r - std::floor(r)) * 4294967296.0);
    }
    auto rr = [](uint32_t x, uint32_t n) -> uint32_t { return (x >> n) | (x << (32 - n)); };
    std::vector<uint8_t> msg(input.begin(), input.end());
    uint64_t bitlen = static_cast<uint64_t>(msg.size()) * 8ULL;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bitlen >> (i * 8)) & 0xff));
    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(msg[off + 4*i]) << 24) | (uint32_t(msg[off + 4*i + 1]) << 16) |
                   (uint32_t(msg[off + 4*i + 2]) << 8) | uint32_t(msg[off + 4*i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rr(w[i-15], 7) ^ rr(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = rr(w[i-2], 17) ^ rr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=h[0], b=h[1], c=h[2], d=h[3], e=h[4], f=h[5], g=h[6], hh=h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rr(e, 6) ^ rr(e, 11) ^ rr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            uint32_t s0 = rr(a, 2) ^ rr(a, 13) ^ rr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = s0 + maj;
            hh = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) oss << std::setw(8) << h[i];
    return oss.str();
}

inline Val sha256_val(const Val& v) { return Val(sha256_bytes_hex(v.to_string())); }

inline Val sha256_file_val(const Val& p) {
    std::ifstream f(p.to_string(), std::ios::binary);
    if (!f) return Val(std::string(""));
    std::ostringstream ss;
    ss << f.rdbuf();
    return Val(sha256_bytes_hex(ss.str()));
}

inline Val clock_val() {
    using namespace std::chrono;
    auto us = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
    return Val(static_cast<double>(us) / 1e6);
}

inline Val upper_str(const Val& v) {
    std::string s = v.to_string();
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return Val(std::move(s));
}

inline Val lower_str(const Val& v) {
    std::string s = v.to_string();
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return Val(std::move(s));
}

inline Val trim_str(const Val& v) {
    std::string s = v.to_string();
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return Val(s.substr(a, b - a));
}

inline Val contains_val(const Val& h, const Val& n) {
    if (auto* m = std::get_if<MapPtr>(&h.data)) return Val((*m)->count(n.to_string()) > 0);
    if (auto* a = std::get_if<ArrPtr>(&h.data)) {
        for (const auto& x : **a) if (x == n) return Val(true);
        return Val(false);
    }
    return Val(h.to_string().find(n.to_string()) != std::string::npos);
}

inline Val starts_with_str(const Val& h, const Val& p) {
    std::string hs = h.to_string(), ps = p.to_string();
    return Val(hs.size() >= ps.size() && hs.compare(0, ps.size(), ps) == 0);
}

inline Val ends_with_str(const Val& h, const Val& p) {
    std::string hs = h.to_string(), ps = p.to_string();
    return Val(hs.size() >= ps.size() && hs.compare(hs.size() - ps.size(), ps.size(), ps) == 0);
}

inline Val split_str(const Val& s, const Val& sep) {
    auto out = std::make_shared<Arr>();
    std::string src = s.to_string(), sp = sep.to_string();
    if (sp.empty()) {
        for (char c : src) out->push_back(Val(std::string(1, c)));
        return Val(out);
    }
    size_t pos = 0;
    while (true) {
        size_t hit = src.find(sp, pos);
        if (hit == std::string::npos) {
            out->push_back(Val(src.substr(pos)));
            break;
        }
        out->push_back(Val(src.substr(pos, hit - pos)));
        pos = hit + sp.size();
    }
    return Val(out);
}

inline Val join_arr(const Val& arr, const Val& sep) {
    std::string sp = sep.to_string(), out;
    auto items = iter_vec(arr);
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += sp;
        out += items[i].to_string();
    }
    return Val(std::move(out));
}

inline Val replace_str(const Val& s, const Val& a, const Val& b) {
    std::string src = s.to_string(), old_s = a.to_string(), new_s = b.to_string();
    if (old_s.empty()) return Val(std::move(src));
    size_t pos = 0;
    while ((pos = src.find(old_s, pos)) != std::string::npos) {
        src.replace(pos, old_s.size(), new_s);
        pos += new_s.size();
    }
    return Val(std::move(src));
}

inline Val reverse_val(const Val& v) {
    if (auto* a = std::get_if<ArrPtr>(&v.data)) {
        auto out = std::make_shared<Arr>((*a)->rbegin(), (*a)->rend());
        return Val(out);
    }
    std::string s = v.to_string();
    std::reverse(s.begin(), s.end());
    return Val(std::move(s));
}

inline Val slice_val(const Val& v, const Val& s, const Val& e) {
    long long si = std::max(0LL, static_cast<long long>(s.as_num()));
    if (auto* a = std::get_if<ArrPtr>(&v.data)) {
        long long n = static_cast<long long>((*a)->size());
        long long ei = e.is_null() ? n : std::min(n, static_cast<long long>(e.as_num()));
        if (si > ei) si = ei;
        auto out = std::make_shared<Arr>((*a)->begin() + si, (*a)->begin() + ei);
        return Val(out);
    }
    std::string str = v.to_string();
    long long n = static_cast<long long>(str.size());
    long long ei = e.is_null() ? n : std::min(n, static_cast<long long>(e.as_num()));
    if (si > ei) si = ei;
    return Val(str.substr(static_cast<size_t>(si), static_cast<size_t>(ei - si)));
}

inline Val sort_val(const Val& v) {
    auto out = std::make_shared<Arr>(iter_vec(v));
    std::sort(out->begin(), out->end(), [](const Val& a, const Val& b) {
        return a.to_string() < b.to_string();
    });
    return Val(out);
}

inline Val keys_of(const Val& v) {
    auto out = std::make_shared<Arr>();
    if (auto* m = std::get_if<MapPtr>(&v.data)) {
        for (const auto& kv : **m) out->push_back(Val(kv.first));
    }
    return Val(out);
}

inline Val values_of(const Val& v) {
    auto out = std::make_shared<Arr>();
    if (auto* m = std::get_if<MapPtr>(&v.data)) {
        for (const auto& kv : **m) out->push_back(kv.second);
    }
    return Val(out);
}

inline Val read_file_str(const Val& p) {
    std::ifstream f(p.to_string(), std::ios::binary);
    if (!f) return Val("");
    std::ostringstream ss;
    ss << f.rdbuf();
    return Val(ss.str());
}

inline Val write_file_str(const Val& p, const Val& c) {
    std::ofstream f(p.to_string(), std::ios::binary);
    if (!f) return Val(false);
    f << c.to_string();
    return Val(true);
}

inline Val file_exists_val(const Val& p) {
    return Val(std::filesystem::exists(p.to_string()));
}

inline Val env_val(const Val& k) {
    const char* v = std::getenv(k.to_string().c_str());
    return v ? Val(std::string(v)) : Val();
}

inline Val time_ms_val() {
    using namespace std::chrono;
    auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    return Val(static_cast<double>(ms));
}

} // namespace steel

steel::Val strip_v_prefix(steel::Val);
steel::Val st_fn_strip_v_prefix(steel::Val, const std::vector<steel::Val>&);
steel::Val is_all_digits(steel::Val);
steel::Val st_fn_is_all_digits(steel::Val, const std::vector<steel::Val>&);
steel::Val parse_semver(steel::Val);
steel::Val st_fn_parse_semver(steel::Val, const std::vector<steel::Val>&);
steel::Val format_semver(steel::Val);
steel::Val st_fn_format_semver(steel::Val, const std::vector<steel::Val>&);
steel::Val compare_prerelease(steel::Val, steel::Val);
steel::Val st_fn_compare_prerelease(steel::Val, const std::vector<steel::Val>&);
steel::Val compare_semver(steel::Val, steel::Val);
steel::Val st_fn_compare_semver(steel::Val, const std::vector<steel::Val>&);
steel::Val satisfies_constraint(steel::Val, steel::Val);
steel::Val st_fn_satisfies_constraint(steel::Val, const std::vector<steel::Val>&);
steel::Val classify_transition(steel::Val, steel::Val);
steel::Val st_fn_classify_transition(steel::Val, const std::vector<steel::Val>&);
steel::Val is_protected_system_prefix(steel::Val);
steel::Val st_fn_is_protected_system_prefix(steel::Val, const std::vector<steel::Val>&);
steel::Val validate_install_path(steel::Val, steel::Val);
steel::Val st_fn_validate_install_path(steel::Val, const std::vector<steel::Val>&);
steel::Val validate_archive_entry(steel::Val);
steel::Val st_fn_validate_archive_entry(steel::Val, const std::vector<steel::Val>&);
steel::Val validate_archive_manifest(steel::Val);
steel::Val st_fn_validate_archive_manifest(steel::Val, const std::vector<steel::Val>&);
steel::Val requires_privilege_escalation(steel::Val);
steel::Val st_fn_requires_privilege_escalation(steel::Val, const std::vector<steel::Val>&);
steel::Val is_valid_hex_string(steel::Val, steel::Val);
steel::Val st_fn_is_valid_hex_string(steel::Val, const std::vector<steel::Val>&);
steel::Val validate_sha256(steel::Val);
steel::Val st_fn_validate_sha256(steel::Val, const std::vector<steel::Val>&);
steel::Val validate_ed25519_sig(steel::Val);
steel::Val st_fn_validate_ed25519_sig(steel::Val, const std::vector<steel::Val>&);
steel::Val verify_component_integrity(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_verify_component_integrity(steel::Val, const std::vector<steel::Val>&);
steel::Val is_valid_component_id(steel::Val);
steel::Val st_fn_is_valid_component_id(steel::Val, const std::vector<steel::Val>&);
steel::Val validate_catalogue_entry(steel::Val, steel::Val);
steel::Val st_fn_validate_catalogue_entry(steel::Val, const std::vector<steel::Val>&);
steel::Val validate_catalogue(steel::Val);
steel::Val st_fn_validate_catalogue(steel::Val, const std::vector<steel::Val>&);
steel::Val matches_channel(steel::Val, steel::Val);
steel::Val st_fn_matches_channel(steel::Val, const std::vector<steel::Val>&);
steel::Val search_catalogue(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_search_catalogue(steel::Val, const std::vector<steel::Val>&);
steel::Val dfs_visit(steel::Val, steel::Val, steel::Val, steel::Val, steel::Val, steel::Val);
steel::Val st_fn_dfs_visit(steel::Val, const std::vector<steel::Val>&);
steel::Val resolve_install_order(steel::Val, steel::Val);
steel::Val st_fn_resolve_install_order(steel::Val, const std::vector<steel::Val>&);
steel::Val find_reverse_dependents(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_find_reverse_dependents(steel::Val, const std::vector<steel::Val>&);
steel::Val check_file_conflicts(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_check_file_conflicts(steel::Val, const std::vector<steel::Val>&);
steel::Val plan_reconcile(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_plan_reconcile(steel::Val, const std::vector<steel::Val>&);
steel::Val plan_check_updates(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_plan_check_updates(steel::Val, const std::vector<steel::Val>&);
steel::Val plan_remove(steel::Val, steel::Val, steel::Val, steel::Val);
steel::Val st_fn_plan_remove(steel::Val, const std::vector<steel::Val>&);
steel::Val plan_system_snapshot_transition(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_plan_system_snapshot_transition(steel::Val, const std::vector<steel::Val>&);
steel::Val db_default_state();
steel::Val st_fn_db_default_state(steel::Val, const std::vector<steel::Val>&);
steel::Val db_load_from_path(steel::Val);
steel::Val st_fn_db_load_from_path(steel::Val, const std::vector<steel::Val>&);
steel::Val db_save_to_path(steel::Val, steel::Val);
steel::Val st_fn_db_save_to_path(steel::Val, const std::vector<steel::Val>&);
steel::Val db_apply_transaction(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_db_apply_transaction(steel::Val, const std::vector<steel::Val>&);
steel::Val db_rollback_last(steel::Val, steel::Val);
steel::Val st_fn_db_rollback_last(steel::Val, const std::vector<steel::Val>&);
steel::Val db_set_config(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_db_set_config(steel::Val, const std::vector<steel::Val>&);
steel::Val parse_cli_flags(steel::Val);
steel::Val st_fn_parse_cli_flags(steel::Val, const std::vector<steel::Val>&);
steel::Val format_help_text();
steel::Val st_fn_format_help_text(steel::Val, const std::vector<steel::Val>&);
steel::Val format_plan_lines(steel::Val);
steel::Val st_fn_format_plan_lines(steel::Val, const std::vector<steel::Val>&);
steel::Val cli_dispatch(steel::Val, steel::Val, steel::Val, steel::Val);
steel::Val st_fn_cli_dispatch(steel::Val, const std::vector<steel::Val>&);
steel::Val ui_initial_state();
steel::Val st_fn_ui_initial_state(steel::Val, const std::vector<steel::Val>&);
steel::Val ui_toggle_selection(steel::Val, steel::Val);
steel::Val st_fn_ui_toggle_selection(steel::Val, const std::vector<steel::Val>&);
steel::Val ui_reduce(steel::Val, steel::Val, steel::Val, steel::Val);
steel::Val st_fn_ui_reduce(steel::Val, const std::vector<steel::Val>&);
steel::Val build_package_manifest(steel::Val, steel::Val, steel::Val, steel::Val, steel::Val, steel::Val, steel::Val);
steel::Val st_fn_build_package_manifest(steel::Val, const std::vector<steel::Val>&);
steel::Val publish_catalogue_entry(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_publish_catalogue_entry(steel::Val, const std::vector<steel::Val>&);
steel::Val validate_helper_request(steel::Val);
steel::Val st_fn_validate_helper_request(steel::Val, const std::vector<steel::Val>&);
steel::Val helper_check_dpkg_owner(steel::Val);
steel::Val st_fn_helper_check_dpkg_owner(steel::Val, const std::vector<steel::Val>&);
steel::Val helper_backup_path(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_helper_backup_path(steel::Val, const std::vector<steel::Val>&);
steel::Val helper_create_backup(steel::Val, steel::Val);
steel::Val st_fn_helper_create_backup(steel::Val, const std::vector<steel::Val>&);
steel::Val helper_restore_backup(steel::Val, steel::Val);
steel::Val st_fn_helper_restore_backup(steel::Val, const std::vector<steel::Val>&);
steel::Val helper_execute_request(steel::Val);
steel::Val st_fn_helper_execute_request(steel::Val, const std::vector<steel::Val>&);
steel::Val runner_atomic_write(steel::Val, steel::Val);
steel::Val st_fn_runner_atomic_write(steel::Val, const std::vector<steel::Val>&);
steel::Val runner_cli_main();
steel::Val st_fn_runner_cli_main(steel::Val, const std::vector<steel::Val>&);
steel::Val runner_publish_catalogue(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_runner_publish_catalogue(steel::Val, const std::vector<steel::Val>&);
steel::Val test_sample_catalogue();
steel::Val st_fn_test_sample_catalogue(steel::Val, const std::vector<steel::Val>&);
steel::Val test_semver_suite();
steel::Val st_fn_test_semver_suite(steel::Val, const std::vector<steel::Val>&);
steel::Val test_security_policy_suite();
steel::Val st_fn_test_security_policy_suite(steel::Val, const std::vector<steel::Val>&);
steel::Val test_solver_and_reconciler_suite();
steel::Val st_fn_test_solver_and_reconciler_suite(steel::Val, const std::vector<steel::Val>&);
steel::Val test_cli_db_and_ui_suite();
steel::Val st_fn_test_cli_db_and_ui_suite(steel::Val, const std::vector<steel::Val>&);
steel::Val test_helper_policy_suite();
steel::Val st_fn_test_helper_policy_suite(steel::Val, const std::vector<steel::Val>&);
steel::Val run_all_steel_self_tests();
steel::Val st_fn_run_all_steel_self_tests(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_compare_semver(steel::Val, steel::Val);
steel::Val st_fn_steel_compare_semver(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_satisfies_constraint(steel::Val, steel::Val);
steel::Val st_fn_steel_satisfies_constraint(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_validate_path_json(steel::Val, steel::Val);
steel::Val st_fn_steel_validate_path_json(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_validate_archive_json(steel::Val);
steel::Val st_fn_steel_validate_archive_json(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_validate_catalogue_json(steel::Val);
steel::Val st_fn_steel_validate_catalogue_json(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_plan_reconcile(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_steel_plan_reconcile(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_plan_remove(steel::Val, steel::Val, steel::Val, steel::Val);
steel::Val st_fn_steel_plan_remove(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_cli_dispatch(steel::Val, steel::Val, steel::Val, steel::Val);
steel::Val st_fn_steel_cli_dispatch(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_ui_reduce(steel::Val, steel::Val, steel::Val, steel::Val);
steel::Val st_fn_steel_ui_reduce(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_build_manifest_json(steel::Val, steel::Val, steel::Val, steel::Val, steel::Val, steel::Val, steel::Val);
steel::Val st_fn_steel_build_manifest_json(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_publish_entry_json(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_steel_publish_entry_json(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_validate_helper_request_json(steel::Val);
steel::Val st_fn_steel_validate_helper_request_json(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_helper_run_json(steel::Val);
steel::Val st_fn_steel_helper_run_json(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_runner_cli_json();
steel::Val st_fn_steel_runner_cli_json(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_runner_publish_json(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_steel_runner_publish_json(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_plan_snapshot_json(steel::Val, steel::Val, steel::Val);
steel::Val st_fn_steel_plan_snapshot_json(steel::Val, const std::vector<steel::Val>&);
steel::Val steel_run_self_tests();
steel::Val st_fn_steel_run_self_tests(steel::Val, const std::vector<steel::Val>&);

steel::Val strip_v_prefix(steel::Val raw) {
    steel::Val s = steel::trim_str(raw);
    if ((steel::Val((steel::starts_with_str(s, steel::Val("v"))).truthy() || (steel::starts_with_str(s, steel::Val("V"))).truthy())).truthy()) {
        return steel::slice_val(s, steel::Val(1.0), steel::len_of(s));
    }
    return s;
}
steel::Val st_fn_strip_v_prefix(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return strip_v_prefix((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val is_all_digits(steel::Val s) {
    if ((steel::Val(steel::len_of(s) == steel::Val(0.0))).truthy()) {
        return steel::Val(false);
    }
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(s))).truthy()) {
        steel::Val ch = steel::slice_val(s, i, (i + steel::Val(1.0)));
        steel::Val code = steel::char_code_val(ch);
        if ((steel::Val((steel::Val(code < steel::Val(48.0))).truthy() || (steel::Val(code > steel::Val(57.0))).truthy())).truthy()) {
            return steel::Val(false);
        }
        i = (i + steel::Val(1.0));
    }
    return steel::Val(true);
}
steel::Val st_fn_is_all_digits(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return is_all_digits((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val parse_semver(steel::Val raw) {
    steel::Val s = strip_v_prefix(raw);
    steel::Val plus_idx = steel::index_of_val(s, steel::Val("+"));
    steel::Val build = steel::Val("");
    if ((steel::Val(plus_idx >= steel::Val(0.0))).truthy()) {
        build = steel::slice_val(s, (plus_idx + steel::Val(1.0)), steel::len_of(s));
        s = steel::slice_val(s, steel::Val(0.0), plus_idx);
    }
    steel::Val dash_idx = steel::index_of_val(s, steel::Val("-"));
    steel::Val pre = steel::Val("");
    if ((steel::Val(dash_idx >= steel::Val(0.0))).truthy()) {
        pre = steel::slice_val(s, (dash_idx + steel::Val(1.0)), steel::len_of(s));
        s = steel::slice_val(s, steel::Val(0.0), dash_idx);
    }
    steel::Val parts = steel::split_str(s, steel::Val("."));
    if ((steel::Val((steel::Val(steel::len_of(parts) < steel::Val(2.0))).truthy() || (steel::Val(steel::len_of(parts) > steel::Val(3.0))).truthy())).truthy()) {
        return steel::make_map({{"valid", steel::Val(false)}, {"major", steel::Val(0.0)}, {"minor", steel::Val(0.0)}, {"patch", steel::Val(0.0)}, {"pre", steel::Val("")}, {"build", steel::Val("")}});
    }
    steel::Val maj_s = steel::idx_get(parts, steel::Val(0.0));
    steel::Val min_s = steel::idx_get(parts, steel::Val(1.0));
    steel::Val pat_s = steel::Val("0");
    if ((steel::Val(steel::len_of(parts) == steel::Val(3.0))).truthy()) {
        pat_s = steel::idx_get(parts, steel::Val(2.0));
    }
    if ((steel::Val((steel::Val((steel::Val(!(is_all_digits(maj_s)).truthy())).truthy() || (steel::Val(!(is_all_digits(min_s)).truthy())).truthy())).truthy() || (steel::Val(!(is_all_digits(pat_s)).truthy())).truthy())).truthy()) {
        return steel::make_map({{"valid", steel::Val(false)}, {"major", steel::Val(0.0)}, {"minor", steel::Val(0.0)}, {"patch", steel::Val(0.0)}, {"pre", steel::Val("")}, {"build", steel::Val("")}});
    }
    return steel::make_map({{"valid", steel::Val(true)}, {"major", steel::Val(std::trunc((steel::Val((maj_s).as_num())).as_num()))}, {"minor", steel::Val(std::trunc((steel::Val((min_s).as_num())).as_num()))}, {"patch", steel::Val(std::trunc((steel::Val((pat_s).as_num())).as_num()))}, {"pre", pre}, {"build", build}});
}
steel::Val st_fn_parse_semver(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return parse_semver((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val format_semver(steel::Val v) {
    steel::Val base = ((((steel::Val((steel::idx_get(v, steel::Val("major"))).to_string()) + steel::Val(".")) + steel::Val((steel::idx_get(v, steel::Val("minor"))).to_string())) + steel::Val(".")) + steel::Val((steel::idx_get(v, steel::Val("patch"))).to_string()));
    if ((steel::Val(steel::len_of(steel::idx_get(v, steel::Val("pre"))) > steel::Val(0.0))).truthy()) {
        base = ((base + steel::Val("-")) + steel::idx_get(v, steel::Val("pre")));
    }
    if ((steel::Val(steel::len_of(steel::idx_get(v, steel::Val("build"))) > steel::Val(0.0))).truthy()) {
        base = ((base + steel::Val("+")) + steel::idx_get(v, steel::Val("build")));
    }
    return base;
}
steel::Val st_fn_format_semver(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return format_semver((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val compare_prerelease(steel::Val a, steel::Val b) {
    if ((steel::Val(a == b)).truthy()) {
        return steel::Val(0.0);
    }
    if ((steel::Val(steel::len_of(a) == steel::Val(0.0))).truthy()) {
        return steel::Val(1.0);
    }
    if ((steel::Val(steel::len_of(b) == steel::Val(0.0))).truthy()) {
        return steel::Val(-1.0);
    }
    steel::Val pa = steel::split_str(a, steel::Val("."));
    steel::Val pb = steel::split_str(b, steel::Val("."));
    steel::Val limit = steel::len_of(pa);
    if ((steel::Val(steel::len_of(pb) < limit)).truthy()) {
        limit = steel::len_of(pb);
    }
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < limit)).truthy()) {
        steel::Val xa = steel::idx_get(pa, i);
        steel::Val xb = steel::idx_get(pb, i);
        if ((steel::Val(xa != xb)).truthy()) {
            steel::Val na = is_all_digits(xa);
            steel::Val nb = is_all_digits(xb);
            if ((steel::Val((na).truthy() && (nb).truthy())).truthy()) {
                steel::Val va = steel::Val((xa).as_num());
                steel::Val vb = steel::Val((xb).as_num());
                if ((steel::Val(va < vb)).truthy()) {
                    return steel::Val(-1.0);
                }
                if ((steel::Val(va > vb)).truthy()) {
                    return steel::Val(1.0);
                }
            } else {
                if ((steel::Val((na).truthy() && (steel::Val(!(nb).truthy())).truthy())).truthy()) {
                    return steel::Val(-1.0);
                } else {
                    if ((steel::Val((steel::Val(!(na).truthy())).truthy() && (nb).truthy())).truthy()) {
                        return steel::Val(1.0);
                    } else {
                        if ((steel::Val(xa < xb)).truthy()) {
                            return steel::Val(-1.0);
                        }
                        return steel::Val(1.0);
                    }
                }
            }
        }
        i = (i + steel::Val(1.0));
    }
    if ((steel::Val(steel::len_of(pa) < steel::len_of(pb))).truthy()) {
        return steel::Val(-1.0);
    }
    if ((steel::Val(steel::len_of(pa) > steel::len_of(pb))).truthy()) {
        return steel::Val(1.0);
    }
    return steel::Val(0.0);
}
steel::Val st_fn_compare_prerelease(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return compare_prerelease((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val compare_semver(steel::Val a_raw, steel::Val b_raw) {
    steel::Val a = parse_semver(a_raw);
    steel::Val b = parse_semver(b_raw);
    if ((steel::Val(steel::idx_get(a, steel::Val("major")) != steel::idx_get(b, steel::Val("major")))).truthy()) {
        if ((steel::Val(steel::idx_get(a, steel::Val("major")) < steel::idx_get(b, steel::Val("major")))).truthy()) {
            return steel::Val(-1.0);
        }
        return steel::Val(1.0);
    }
    if ((steel::Val(steel::idx_get(a, steel::Val("minor")) != steel::idx_get(b, steel::Val("minor")))).truthy()) {
        if ((steel::Val(steel::idx_get(a, steel::Val("minor")) < steel::idx_get(b, steel::Val("minor")))).truthy()) {
            return steel::Val(-1.0);
        }
        return steel::Val(1.0);
    }
    if ((steel::Val(steel::idx_get(a, steel::Val("patch")) != steel::idx_get(b, steel::Val("patch")))).truthy()) {
        if ((steel::Val(steel::idx_get(a, steel::Val("patch")) < steel::idx_get(b, steel::Val("patch")))).truthy()) {
            return steel::Val(-1.0);
        }
        return steel::Val(1.0);
    }
    return compare_prerelease(steel::idx_get(a, steel::Val("pre")), steel::idx_get(b, steel::Val("pre")));
}
steel::Val st_fn_compare_semver(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return compare_semver((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val satisfies_constraint(steel::Val version_raw, steel::Val constraint_raw) {
    steel::Val c = steel::trim_str(constraint_raw);
    if ((steel::Val((steel::Val(steel::len_of(c) == steel::Val(0.0))).truthy() || (steel::Val(c == steel::Val("*"))).truthy())).truthy()) {
        return steel::Val(true);
    }
    steel::Val v = parse_semver(version_raw);
    if ((steel::Val(!(steel::idx_get(v, steel::Val("valid"))).truthy())).truthy()) {
        return steel::Val(false);
    }
    if ((steel::starts_with_str(c, steel::Val(">="))).truthy()) {
        return steel::Val(compare_semver(version_raw, steel::trim_str(steel::slice_val(c, steel::Val(2.0), steel::len_of(c)))) >= steel::Val(0.0));
    }
    if ((steel::starts_with_str(c, steel::Val("<="))).truthy()) {
        return steel::Val(compare_semver(version_raw, steel::trim_str(steel::slice_val(c, steel::Val(2.0), steel::len_of(c)))) <= steel::Val(0.0));
    }
    if ((steel::starts_with_str(c, steel::Val(">"))).truthy()) {
        return steel::Val(compare_semver(version_raw, steel::trim_str(steel::slice_val(c, steel::Val(1.0), steel::len_of(c)))) > steel::Val(0.0));
    }
    if ((steel::starts_with_str(c, steel::Val("<"))).truthy()) {
        return steel::Val(compare_semver(version_raw, steel::trim_str(steel::slice_val(c, steel::Val(1.0), steel::len_of(c)))) < steel::Val(0.0));
    }
    if ((steel::starts_with_str(c, steel::Val("="))).truthy()) {
        return steel::Val(compare_semver(version_raw, steel::trim_str(steel::slice_val(c, steel::Val(1.0), steel::len_of(c)))) == steel::Val(0.0));
    }
    if ((steel::starts_with_str(c, steel::Val("^"))).truthy()) {
        steel::Val base_s = steel::trim_str(steel::slice_val(c, steel::Val(1.0), steel::len_of(c)));
        steel::Val base = parse_semver(base_s);
        if ((steel::Val(!(steel::idx_get(base, steel::Val("valid"))).truthy())).truthy()) {
            return steel::Val(false);
        }
        if ((steel::Val(compare_semver(version_raw, base_s) < steel::Val(0.0))).truthy()) {
            return steel::Val(false);
        }
        if ((steel::Val(steel::idx_get(base, steel::Val("major")) > steel::Val(0.0))).truthy()) {
            return steel::Val(steel::idx_get(v, steel::Val("major")) == steel::idx_get(base, steel::Val("major")));
        }
        if ((steel::Val(steel::idx_get(base, steel::Val("minor")) > steel::Val(0.0))).truthy()) {
            return steel::Val((steel::Val(steel::idx_get(v, steel::Val("major")) == steel::Val(0.0))).truthy() && (steel::Val(steel::idx_get(v, steel::Val("minor")) == steel::idx_get(base, steel::Val("minor")))).truthy());
        }
        return steel::Val((steel::Val((steel::Val(steel::idx_get(v, steel::Val("major")) == steel::Val(0.0))).truthy() && (steel::Val(steel::idx_get(v, steel::Val("minor")) == steel::Val(0.0))).truthy())).truthy() && (steel::Val(steel::idx_get(v, steel::Val("patch")) == steel::idx_get(base, steel::Val("patch")))).truthy());
    }
    if ((steel::starts_with_str(c, steel::Val("~"))).truthy()) {
        steel::Val base_s = steel::trim_str(steel::slice_val(c, steel::Val(1.0), steel::len_of(c)));
        steel::Val base = parse_semver(base_s);
        if ((steel::Val(!(steel::idx_get(base, steel::Val("valid"))).truthy())).truthy()) {
            return steel::Val(false);
        }
        if ((steel::Val(compare_semver(version_raw, base_s) < steel::Val(0.0))).truthy()) {
            return steel::Val(false);
        }
        return steel::Val((steel::Val(steel::idx_get(v, steel::Val("major")) == steel::idx_get(base, steel::Val("major")))).truthy() && (steel::Val(steel::idx_get(v, steel::Val("minor")) == steel::idx_get(base, steel::Val("minor")))).truthy());
    }
    return steel::Val(compare_semver(version_raw, c) >= steel::Val(0.0));
}
steel::Val st_fn_satisfies_constraint(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return satisfies_constraint((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val classify_transition(steel::Val from_ver, steel::Val to_ver) {
    if ((steel::Val((steel::Val(from_ver == steel::Val())).truthy() || (steel::Val(steel::len_of(steel::Val((from_ver).to_string())) == steel::Val(0.0))).truthy())).truthy()) {
        return steel::Val("install");
    }
    steel::Val cmp = compare_semver(from_ver, to_ver);
    if ((steel::Val(cmp < steel::Val(0.0))).truthy()) {
        return steel::Val("upgrade");
    }
    if ((steel::Val(cmp > steel::Val(0.0))).truthy()) {
        return steel::Val("downgrade");
    }
    return steel::Val("noop");
}
steel::Val st_fn_classify_transition(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return classify_transition((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val is_protected_system_prefix(steel::Val path) {
    steel::Val blocked = steel::make_arr({steel::Val("/etc"), steel::Val("/boot"), steel::Val("/proc"), steel::Val("/sys"), steel::Val("/dev"), steel::Val("/bin"), steel::Val("/sbin"), steel::Val("/lib"), steel::Val("/lib64"), steel::Val("/root"), steel::Val("/var/lib/dpkg"), steel::Val("/var/lib/apt"), steel::Val("/usr/bin/sudo")});
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(blocked))).truthy()) {
        steel::Val prefix = steel::idx_get(blocked, i);
        if ((steel::Val((steel::Val(path == prefix)).truthy() || (steel::starts_with_str(path, (prefix + steel::Val("/")))).truthy())).truthy()) {
            return steel::Val(true);
        }
        i = (i + steel::Val(1.0));
    }
    return steel::Val(false);
}
steel::Val st_fn_is_protected_system_prefix(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return is_protected_system_prefix((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val validate_install_path(steel::Val path, steel::Val scope) {
    if ((steel::Val((steel::Val(path == steel::Val())).truthy() || (steel::Val(steel::len_of(path) == steel::Val(0.0))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("install path must not be empty")}});
    }
    if ((steel::contains_val(path, steel::Val("//"))).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("duplicate slash in install path")}});
    }
    if ((steel::contains_val(path, steel::Val("\\"))).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("backslash not allowed in unix install path")}});
    }
    steel::Val segs = steel::split_str(path, steel::Val("/"));
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(segs))).truthy()) {
        if ((steel::Val((steel::Val(steel::idx_get(segs, i) == steel::Val(".."))).truthy() || (steel::Val(steel::idx_get(segs, i) == steel::Val("."))).truthy())).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("path traversal segment not allowed")}});
        }
        i = (i + steel::Val(1.0));
    }
    if ((is_protected_system_prefix(path)).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("protected system or dpkg directory")}});
    }
    if ((steel::Val(scope == steel::Val("system"))).truthy()) {
        if ((steel::Val((steel::Val(!(steel::starts_with_str(path, steel::Val("/usr/share/"))).truthy())).truthy() && (steel::Val(!(steel::starts_with_str(path, steel::Val("/opt/infinityos/"))).truthy())).truthy())).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("system scope must target /usr/share/ or /opt/infinityos/")}});
        }
        if ((steel::Val(steel::len_of(segs) < steel::Val(4.0))).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("system install path is too shallow")}});
        }
        return steel::make_map({{"ok", steel::Val(true)}, {"reason", steel::Val("")}});
    }
    if ((steel::Val(scope == steel::Val("user"))).truthy()) {
        if ((steel::Val((steel::Val(!(steel::starts_with_str(path, steel::Val("~/.local/share/"))).truthy())).truthy() && (steel::Val(!(steel::starts_with_str(path, steel::Val("/home/"))).truthy())).truthy())).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("user scope must target ~/.local/share/ or /home/")}});
        }
        return steel::make_map({{"ok", steel::Val(true)}, {"reason", steel::Val("")}});
    }
    return steel::make_map({{"ok", steel::Val(false)}, {"reason", (steel::Val("unknown install scope: ") + steel::Val((scope).to_string()))}});
}
steel::Val st_fn_validate_install_path(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return validate_install_path((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val validate_archive_entry(steel::Val entry_path) {
    if ((steel::Val((steel::Val(entry_path == steel::Val())).truthy() || (steel::Val(steel::len_of(entry_path) == steel::Val(0.0))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("empty archive entry")}});
    }
    if ((steel::starts_with_str(entry_path, steel::Val("/"))).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", (steel::Val("absolute path in archive: ") + entry_path)}});
    }
    if ((steel::contains_val(entry_path, steel::Val("\\"))).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", (steel::Val("backslash in archive entry: ") + entry_path)}});
    }
    steel::Val segs = steel::split_str(entry_path, steel::Val("/"));
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(segs))).truthy()) {
        if ((steel::Val(steel::idx_get(segs, i) == steel::Val(".."))).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"reason", (steel::Val("zip-slip traversal in archive: ") + entry_path)}});
        }
        i = (i + steel::Val(1.0));
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"reason", steel::Val("")}});
}
steel::Val st_fn_validate_archive_entry(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return validate_archive_entry((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val validate_archive_manifest(steel::Val entries) {
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(entries))).truthy()) {
        steel::Val chk = validate_archive_entry(steel::idx_get(entries, i));
        if ((steel::Val(!(steel::idx_get(chk, steel::Val("ok"))).truthy())).truthy()) {
            return chk;
        }
        i = (i + steel::Val(1.0));
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"reason", steel::Val("")}});
}
steel::Val st_fn_validate_archive_manifest(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return validate_archive_manifest((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val requires_privilege_escalation(steel::Val scope) {
    return steel::Val(scope == steel::Val("system"));
}
steel::Val st_fn_requires_privilege_escalation(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return requires_privilege_escalation((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val is_valid_hex_string(steel::Val s, steel::Val expected_len) {
    if ((steel::Val((steel::Val(s == steel::Val())).truthy() || (steel::Val(steel::len_of(s) != expected_len)).truthy())).truthy()) {
        return steel::Val(false);
    }
    steel::Val low = steel::lower_str(s);
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(low))).truthy()) {
        steel::Val ch = steel::slice_val(low, i, (i + steel::Val(1.0)));
        steel::Val code = steel::char_code_val(ch);
        steel::Val is_digit = steel::Val((steel::Val(code >= steel::Val(48.0))).truthy() && (steel::Val(code <= steel::Val(57.0))).truthy());
        steel::Val is_hex = steel::Val((steel::Val(code >= steel::Val(97.0))).truthy() && (steel::Val(code <= steel::Val(102.0))).truthy());
        if ((steel::Val((steel::Val(!(is_digit).truthy())).truthy() && (steel::Val(!(is_hex).truthy())).truthy())).truthy()) {
            return steel::Val(false);
        }
        i = (i + steel::Val(1.0));
    }
    return steel::Val(true);
}
steel::Val st_fn_is_valid_hex_string(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return is_valid_hex_string((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val validate_sha256(steel::Val digest) {
    return is_valid_hex_string(digest, steel::Val(64.0));
}
steel::Val st_fn_validate_sha256(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return validate_sha256((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val validate_ed25519_sig(steel::Val sig) {
    return is_valid_hex_string(sig, steel::Val(128.0));
}
steel::Val st_fn_validate_ed25519_sig(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return validate_ed25519_sig((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val verify_component_integrity(steel::Val entry, steel::Val computed_sha256, steel::Val sig_verified) {
    steel::Val expected = steel::idx_get(entry, steel::Val("sha256"));
    if ((steel::Val((steel::Val(expected != steel::Val())).truthy() && (steel::Val(steel::len_of(steel::Val((expected).to_string())) > steel::Val(0.0))).truthy())).truthy()) {
        if ((steel::Val(!(validate_sha256(steel::Val((expected).to_string()))).truthy())).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("malformed expected sha256 digest")}});
        }
        if ((steel::Val(steel::lower_str(steel::Val((computed_sha256).to_string())) != steel::lower_str(steel::Val((expected).to_string())))).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"reason", (((steel::Val("sha256 mismatch: expected ") + steel::Val((expected).to_string())) + steel::Val(", got ")) + steel::Val((computed_sha256).to_string()))}});
        }
    }
    if ((steel::Val((steel::has_key_val(entry, steel::Val("signature"))).truthy() && (steel::Val(steel::len_of(steel::Val((steel::idx_get(entry, steel::Val("signature"))).to_string())) > steel::Val(0.0))).truthy())).truthy()) {
        if ((steel::Val(!(sig_verified).truthy())).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("ed25519 signature verification failed")}});
        }
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"reason", steel::Val("")}});
}
steel::Val st_fn_verify_component_integrity(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return verify_component_integrity((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val is_valid_component_id(steel::Val id) {
    if ((steel::Val((steel::Val((steel::Val(id == steel::Val())).truthy() || (steel::Val(steel::len_of(id) < steel::Val(2.0))).truthy())).truthy() || (steel::Val(steel::len_of(id) > steel::Val(64.0))).truthy())).truthy()) {
        return steel::Val(false);
    }
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(id))).truthy()) {
        steel::Val c = steel::char_code_val(steel::slice_val(id, i, (i + steel::Val(1.0))));
        steel::Val ok_low = steel::Val((steel::Val(c >= steel::Val(97.0))).truthy() && (steel::Val(c <= steel::Val(122.0))).truthy());
        steel::Val ok_dig = steel::Val((steel::Val(c >= steel::Val(48.0))).truthy() && (steel::Val(c <= steel::Val(57.0))).truthy());
        steel::Val ok_sym = steel::Val((steel::Val((steel::Val(c == steel::Val(45.0))).truthy() || (steel::Val(c == steel::Val(95.0))).truthy())).truthy() || (steel::Val(c == steel::Val(46.0))).truthy());
        if ((steel::Val((steel::Val((steel::Val(!(ok_low).truthy())).truthy() && (steel::Val(!(ok_dig).truthy())).truthy())).truthy() && (steel::Val(!(ok_sym).truthy())).truthy())).truthy()) {
            return steel::Val(false);
        }
        i = (i + steel::Val(1.0));
    }
    return steel::Val(true);
}
steel::Val st_fn_is_valid_component_id(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return is_valid_component_id((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val validate_catalogue_entry(steel::Val id, steel::Val entry) {
    if ((steel::Val(!(is_valid_component_id(id)).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", (steel::Val("invalid component id: ") + steel::Val((id).to_string()))}});
    }
    if ((steel::Val(!(steel::has_key_val(entry, steel::Val("version"))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", (id + steel::Val(": missing version"))}});
    }
    steel::Val v = parse_semver(steel::idx_get(entry, steel::Val("version")));
    if ((steel::Val(!(steel::idx_get(v, steel::Val("valid"))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", ((id + steel::Val(": invalid semver: ")) + steel::Val((steel::idx_get(entry, steel::Val("version"))).to_string()))}});
    }
    if ((steel::Val(!(steel::has_key_val(entry, steel::Val("install_path"))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", (id + steel::Val(": missing install_path"))}});
    }
    steel::Val scope = steel::Val("system");
    if ((steel::Val((steel::Val((steel::has_key_val(entry, steel::Val("scope"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("scope")) != steel::Val())).truthy())).truthy() && (steel::Val(steel::len_of(steel::Val((steel::idx_get(entry, steel::Val("scope"))).to_string())) > steel::Val(0.0))).truthy())).truthy()) {
        scope = steel::idx_get(entry, steel::Val("scope"));
    }
    steel::Val pchk = validate_install_path(steel::idx_get(entry, steel::Val("install_path")), scope);
    if ((steel::Val(!(steel::idx_get(pchk, steel::Val("ok"))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", ((id + steel::Val(": ")) + steel::idx_get(pchk, steel::Val("reason")))}});
    }
    if ((steel::Val((steel::Val((steel::has_key_val(entry, steel::Val("sha256"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("sha256")) != steel::Val())).truthy())).truthy() && (steel::Val(steel::len_of(steel::Val((steel::idx_get(entry, steel::Val("sha256"))).to_string())) > steel::Val(0.0))).truthy())).truthy()) {
        if ((steel::Val(!(validate_sha256(steel::Val((steel::idx_get(entry, steel::Val("sha256"))).to_string()))).truthy())).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"reason", (id + steel::Val(": invalid sha256 hex"))}});
        }
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"reason", steel::Val("")}});
}
steel::Val st_fn_validate_catalogue_entry(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return validate_catalogue_entry((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val validate_catalogue(steel::Val catalogue) {
    steel::Val ks = steel::keys_of(catalogue);
    steel::Val errors = steel::make_arr({});
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(ks))).truthy()) {
        steel::Val id = steel::idx_get(ks, i);
        steel::Val chk = validate_catalogue_entry(id, steel::idx_get(catalogue, id));
        if ((steel::Val(!(steel::idx_get(chk, steel::Val("ok"))).truthy())).truthy()) {
            (void)(steel::push_val(errors, steel::idx_get(chk, steel::Val("reason"))));
        }
        i = (i + steel::Val(1.0));
    }
    return steel::make_map({{"ok", steel::Val(steel::len_of(errors) == steel::Val(0.0))}, {"count", steel::len_of(ks)}, {"errors", errors}});
}
steel::Val st_fn_validate_catalogue(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return validate_catalogue((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val matches_channel(steel::Val entry, steel::Val channel) {
    if ((steel::Val((steel::Val((steel::Val(channel == steel::Val())).truthy() || (steel::Val(steel::len_of(channel) == steel::Val(0.0))).truthy())).truthy() || (steel::Val(channel == steel::Val("all"))).truthy())).truthy()) {
        return steel::Val(true);
    }
    if ((steel::Val((steel::Val((steel::Val(!(steel::has_key_val(entry, steel::Val("channel"))).truthy())).truthy() || (steel::Val(steel::idx_get(entry, steel::Val("channel")) == steel::Val())).truthy())).truthy() || (steel::Val(steel::len_of(steel::Val((steel::idx_get(entry, steel::Val("channel"))).to_string())) == steel::Val(0.0))).truthy())).truthy()) {
        return steel::Val(channel == steel::Val("stable"));
    }
    steel::Val ec = steel::idx_get(entry, steel::Val("channel"));
    if ((steel::Val(channel == steel::Val("nightly"))).truthy()) {
        return steel::Val(true);
    }
    if ((steel::Val(channel == steel::Val("beta"))).truthy()) {
        return steel::Val((steel::Val(ec == steel::Val("stable"))).truthy() || (steel::Val(ec == steel::Val("beta"))).truthy());
    }
    return steel::Val(ec == steel::Val("stable"));
}
steel::Val st_fn_matches_channel(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return matches_channel((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val search_catalogue(steel::Val catalogue, steel::Val query, steel::Val channel) {
    steel::Val q = steel::lower_str(steel::trim_str(query));
    steel::Val ks = steel::sort_val(steel::keys_of(catalogue));
    steel::Val matches = steel::make_arr({});
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(ks))).truthy()) {
        steel::Val id = steel::idx_get(ks, i);
        steel::Val entry = steel::idx_get(catalogue, id);
        if ((matches_channel(entry, channel)).truthy()) {
            steel::Val desc = steel::Val("");
            if ((steel::Val((steel::has_key_val(entry, steel::Val("description"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("description")) != steel::Val())).truthy())).truthy()) {
                desc = steel::lower_str(steel::Val((steel::idx_get(entry, steel::Val("description"))).to_string()));
            }
            steel::Val cat = steel::Val("");
            if ((steel::Val((steel::has_key_val(entry, steel::Val("category"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("category")) != steel::Val())).truthy())).truthy()) {
                cat = steel::lower_str(steel::Val((steel::idx_get(entry, steel::Val("category"))).to_string()));
            }
            if ((steel::Val((steel::Val((steel::Val((steel::Val(steel::len_of(q) == steel::Val(0.0))).truthy() || (steel::contains_val(steel::lower_str(id), q)).truthy())).truthy() || (steel::contains_val(desc, q)).truthy())).truthy() || (steel::contains_val(cat, q)).truthy())).truthy()) {
                (void)(steel::push_val(matches, steel::make_map({{"id", id}, {"version", steel::idx_get(entry, steel::Val("version"))}, {"scope", ((steel::has_key_val(entry, steel::Val("scope"))).truthy() ? (steel::idx_get(entry, steel::Val("scope"))) : (steel::Val("system")))}, {"description", ((steel::has_key_val(entry, steel::Val("description"))).truthy() ? (steel::idx_get(entry, steel::Val("description"))) : (steel::Val("")))}, {"category", ((steel::has_key_val(entry, steel::Val("category"))).truthy() ? (steel::idx_get(entry, steel::Val("category"))) : (steel::Val("general")))}})));
            }
        }
        i = (i + steel::Val(1.0));
    }
    return matches;
}
steel::Val st_fn_search_catalogue(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return search_catalogue((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val dfs_visit(steel::Val id, steel::Val catalogue, steel::Val visiting, steel::Val visited, steel::Val order, steel::Val err_box) {
    if ((steel::Val(steel::len_of(steel::idx_get(err_box, steel::Val("msg"))) > steel::Val(0.0))).truthy()) {
        return steel::Val();
    }
    if ((steel::has_key_val(visited, id)).truthy()) {
        return steel::Val();
    }
    if ((steel::has_key_val(visiting, id)).truthy()) {
        steel::idx_set(err_box, steel::Val("msg"), (steel::Val("dependency cycle detected at ") + id));
        return steel::Val();
    }
    if ((steel::Val(!(steel::has_key_val(catalogue, id)).truthy())).truthy()) {
        steel::idx_set(err_box, steel::Val("msg"), (steel::Val("unknown component: ") + id));
        return steel::Val();
    }
    steel::idx_set(visiting, id, steel::Val(true));
    steel::Val entry = steel::idx_get(catalogue, id);
    if ((steel::Val((steel::has_key_val(entry, steel::Val("depends"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("depends")) != steel::Val())).truthy())).truthy()) {
        steel::Val deps = steel::idx_get(entry, steel::Val("depends"));
        steel::Val i = steel::Val(0.0);
        while ((steel::Val(i < steel::len_of(deps))).truthy()) {
            steel::Val dep = steel::idx_get(deps, i);
            steel::Val dep_id = steel::idx_get(dep, steel::Val("id"));
            if ((steel::Val(!(steel::has_key_val(catalogue, dep_id)).truthy())).truthy()) {
                steel::idx_set(err_box, steel::Val("msg"), ((id + steel::Val(" depends on missing component: ")) + dep_id));
                return steel::Val();
            }
            if ((steel::Val((steel::Val((steel::has_key_val(dep, steel::Val("min_version"))).truthy() && (steel::Val(steel::idx_get(dep, steel::Val("min_version")) != steel::Val())).truthy())).truthy() && (steel::Val(steel::len_of(steel::Val((steel::idx_get(dep, steel::Val("min_version"))).to_string())) > steel::Val(0.0))).truthy())).truthy()) {
                steel::Val avail = steel::idx_get(steel::idx_get(catalogue, dep_id), steel::Val("version"));
                if ((steel::Val(!(satisfies_constraint(avail, steel::Val((steel::idx_get(dep, steel::Val("min_version"))).to_string()))).truthy())).truthy()) {
                    steel::idx_set(err_box, steel::Val("msg"), ((((((id + steel::Val(" requires ")) + dep_id) + steel::Val(" ")) + steel::Val((steel::idx_get(dep, steel::Val("min_version"))).to_string())) + steel::Val(" but catalogue has ")) + steel::Val((avail).to_string())));
                    return steel::Val();
                }
            }
            (void)(dfs_visit(dep_id, catalogue, visiting, visited, order, err_box));
            if ((steel::Val(steel::len_of(steel::idx_get(err_box, steel::Val("msg"))) > steel::Val(0.0))).truthy()) {
                return steel::Val();
            }
            i = (i + steel::Val(1.0));
        }
    }
    (void)(steel::delete_key_val(visiting, id));
    steel::idx_set(visited, id, steel::Val(true));
    (void)(steel::push_val(order, id));
    return steel::Val();
}
steel::Val st_fn_dfs_visit(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return dfs_visit((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val()), (st_args.size() > 3 ? st_args[3] : steel::Val()), (st_args.size() > 4 ? st_args[4] : steel::Val()), (st_args.size() > 5 ? st_args[5] : steel::Val())); }

steel::Val resolve_install_order(steel::Val catalogue, steel::Val targets) {
    steel::Val visiting = steel::make_map({});
    steel::Val visited = steel::make_map({});
    steel::Val order = steel::make_arr({});
    steel::Val err_box = steel::make_map({{"msg", steel::Val("")}});
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(targets))).truthy()) {
        (void)(dfs_visit(steel::idx_get(targets, i), catalogue, visiting, visited, order, err_box));
        if ((steel::Val(steel::len_of(steel::idx_get(err_box, steel::Val("msg"))) > steel::Val(0.0))).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"order", steel::make_arr({})}, {"error", steel::idx_get(err_box, steel::Val("msg"))}});
        }
        i = (i + steel::Val(1.0));
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"order", order}, {"error", steel::Val("")}});
}
steel::Val st_fn_resolve_install_order(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return resolve_install_order((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val find_reverse_dependents(steel::Val catalogue, steel::Val installed, steel::Val target_id) {
    steel::Val dependents = steel::make_arr({});
    steel::Val ks = steel::sort_val(steel::keys_of(installed));
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(ks))).truthy()) {
        steel::Val id = steel::idx_get(ks, i);
        if ((steel::Val((steel::Val(id != target_id)).truthy() && (steel::has_key_val(catalogue, id)).truthy())).truthy()) {
            steel::Val entry = steel::idx_get(catalogue, id);
            if ((steel::Val((steel::has_key_val(entry, steel::Val("depends"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("depends")) != steel::Val())).truthy())).truthy()) {
                steel::Val deps = steel::idx_get(entry, steel::Val("depends"));
                steel::Val j = steel::Val(0.0);
                while ((steel::Val(j < steel::len_of(deps))).truthy()) {
                    if ((steel::Val(steel::idx_get(steel::idx_get(deps, j), steel::Val("id")) == target_id)).truthy()) {
                        (void)(steel::push_val(dependents, id));
                    }
                    j = (j + steel::Val(1.0));
                }
            }
        }
        i = (i + steel::Val(1.0));
    }
    return dependents;
}
steel::Val st_fn_find_reverse_dependents(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return find_reverse_dependents((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val check_file_conflicts(steel::Val ownership, steel::Val component_id, steel::Val files) {
    if ((steel::Val((steel::Val(ownership == steel::Val())).truthy() || (steel::Val(files == steel::Val())).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(true)}, {"conflict", steel::Val("")}});
    }
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(files))).truthy()) {
        steel::Val f = steel::idx_get(files, i);
        if ((steel::Val((steel::has_key_val(ownership, f)).truthy() && (steel::Val(steel::idx_get(ownership, f) != component_id)).truthy())).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"conflict", (((steel::Val("file ") + f) + steel::Val(" is already owned by ")) + steel::Val((steel::idx_get(ownership, f)).to_string()))}});
        }
        i = (i + steel::Val(1.0));
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"conflict", steel::Val("")}});
}
steel::Val st_fn_check_file_conflicts(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return check_file_conflicts((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val plan_reconcile(steel::Val installed, steel::Val catalogue, steel::Val targets) {
    steel::Val resolved = resolve_install_order(catalogue, targets);
    if ((steel::Val(!(steel::idx_get(resolved, steel::Val("ok"))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"actions", steel::make_arr({})}, {"skipped", steel::make_arr({})}, {"requires_root", steel::Val(false)}, {"error", steel::idx_get(resolved, steel::Val("error"))}});
    }
    steel::Val actions = steel::make_arr({});
    steel::Val skipped = steel::make_arr({});
    steel::Val needs_root = steel::Val(false);
    steel::Val order = steel::idx_get(resolved, steel::Val("order"));
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(order))).truthy()) {
        steel::Val id = steel::idx_get(order, i);
        steel::Val entry = steel::idx_get(catalogue, id);
        steel::Val scope = steel::Val("system");
        if ((steel::Val((steel::Val((steel::has_key_val(entry, steel::Val("scope"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("scope")) != steel::Val())).truthy())).truthy() && (steel::Val(steel::len_of(steel::Val((steel::idx_get(entry, steel::Val("scope"))).to_string())) > steel::Val(0.0))).truthy())).truthy()) {
            scope = steel::idx_get(entry, steel::Val("scope"));
        }
        steel::Val pchk = validate_install_path(steel::idx_get(entry, steel::Val("install_path")), scope);
        if ((steel::Val(!(steel::idx_get(pchk, steel::Val("ok"))).truthy())).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"actions", steel::make_arr({})}, {"skipped", steel::make_arr({})}, {"requires_root", steel::Val(false)}, {"error", ((id + steel::Val(": ")) + steel::idx_get(pchk, steel::Val("reason")))}});
        }
        steel::Val cur = steel::Val("");
        if ((steel::Val((steel::has_key_val(installed, id)).truthy() && (steel::Val(steel::idx_get(installed, id) != steel::Val())).truthy())).truthy()) {
            cur = steel::Val((steel::idx_get(installed, id)).to_string());
        }
        steel::Val target_ver = steel::Val((steel::idx_get(entry, steel::Val("version"))).to_string());
        steel::Val kind = classify_transition(cur, target_ver);
        if ((steel::Val(kind == steel::Val("noop"))).truthy()) {
            (void)(steel::push_val(skipped, id));
        } else {
            if ((requires_privilege_escalation(scope)).truthy()) {
                needs_root = steel::Val(true);
            }
            (void)(steel::push_val(actions, steel::make_map({{"id", id}, {"action", kind}, {"from", cur}, {"to", target_ver}, {"scope", scope}, {"install_path", steel::idx_get(entry, steel::Val("install_path"))}, {"url", ((steel::has_key_val(entry, steel::Val("url"))).truthy() ? (steel::idx_get(entry, steel::Val("url"))) : (steel::Val("")))}, {"sha256", ((steel::has_key_val(entry, steel::Val("sha256"))).truthy() ? (steel::idx_get(entry, steel::Val("sha256"))) : (steel::Val("")))}})));
        }
        i = (i + steel::Val(1.0));
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"actions", actions}, {"skipped", skipped}, {"requires_root", needs_root}, {"error", steel::Val("")}});
}
steel::Val st_fn_plan_reconcile(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return plan_reconcile((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val plan_check_updates(steel::Val installed, steel::Val catalogue, steel::Val channel) {
    steel::Val updates = steel::make_arr({});
    steel::Val up_to_date = steel::make_arr({});
    steel::Val ks = steel::sort_val(steel::keys_of(installed));
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(ks))).truthy()) {
        steel::Val id = steel::idx_get(ks, i);
        if ((steel::has_key_val(catalogue, id)).truthy()) {
            steel::Val entry = steel::idx_get(catalogue, id);
            if ((matches_channel(entry, channel)).truthy()) {
                steel::Val cur = steel::Val((steel::idx_get(installed, id)).to_string());
                steel::Val latest = steel::Val((steel::idx_get(entry, steel::Val("version"))).to_string());
                if ((steel::Val(compare_semver(cur, latest) < steel::Val(0.0))).truthy()) {
                    (void)(steel::push_val(updates, steel::make_map({{"id", id}, {"from", cur}, {"to", latest}, {"scope", ((steel::has_key_val(entry, steel::Val("scope"))).truthy() ? (steel::idx_get(entry, steel::Val("scope"))) : (steel::Val("system")))}})));
                } else {
                    (void)(steel::push_val(up_to_date, id));
                }
            }
        }
        i = (i + steel::Val(1.0));
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"updates", updates}, {"up_to_date", up_to_date}});
}
steel::Val st_fn_plan_check_updates(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return plan_check_updates((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val plan_remove(steel::Val installed, steel::Val catalogue, steel::Val target_id, steel::Val force) {
    if ((steel::Val(!(steel::has_key_val(installed, target_id)).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"actions", steel::make_arr({})}, {"blocked_by", steel::make_arr({})}, {"requires_root", steel::Val(false)}, {"error", (steel::Val("component is not installed: ") + target_id)}});
    }
    steel::Val deps = find_reverse_dependents(catalogue, installed, target_id);
    if ((steel::Val((steel::Val(steel::len_of(deps) > steel::Val(0.0))).truthy() && (steel::Val(!(force).truthy())).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"actions", steel::make_arr({})}, {"blocked_by", deps}, {"requires_root", steel::Val(false)}, {"error", (((steel::Val("cannot remove ") + target_id) + steel::Val(": required by ")) + steel::join_arr(deps, steel::Val(", ")))}});
    }
    steel::Val scope = steel::Val("system");
    steel::Val path = steel::Val("");
    if ((steel::has_key_val(catalogue, target_id)).truthy()) {
        steel::Val entry = steel::idx_get(catalogue, target_id);
        if ((steel::Val((steel::has_key_val(entry, steel::Val("scope"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("scope")) != steel::Val())).truthy())).truthy()) {
            scope = steel::idx_get(entry, steel::Val("scope"));
        }
        if ((steel::Val((steel::has_key_val(entry, steel::Val("install_path"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("install_path")) != steel::Val())).truthy())).truthy()) {
            path = steel::idx_get(entry, steel::Val("install_path"));
        }
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"actions", steel::make_arr({steel::make_map({{"id", target_id}, {"action", steel::Val("remove")}, {"from", steel::Val((steel::idx_get(installed, target_id)).to_string())}, {"to", steel::Val("")}, {"scope", scope}, {"install_path", path}, {"url", steel::Val("")}, {"sha256", steel::Val("")}})})}, {"blocked_by", steel::make_arr({})}, {"requires_root", requires_privilege_escalation(scope)}, {"error", steel::Val("")}});
}
steel::Val st_fn_plan_remove(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return plan_remove((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val()), (st_args.size() > 3 ? st_args[3] : steel::Val())); }

steel::Val plan_system_snapshot_transition(steel::Val installed, steel::Val catalogue, steel::Val target_snapshot) {
    steel::Val target_ids = steel::sort_val(steel::keys_of(target_snapshot));
    steel::Val actions = steel::make_arr({});
    steel::Val skipped = steel::make_arr({});
    steel::Val needs_root = steel::Val(false);
    steel::Val inst_ids = steel::sort_val(steel::keys_of(installed));
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(inst_ids))).truthy()) {
        steel::Val id = steel::idx_get(inst_ids, i);
        if ((steel::Val(!(steel::has_key_val(target_snapshot, id)).truthy())).truthy()) {
            steel::Val scope = steel::Val("system");
            steel::Val path = steel::Val("");
            if ((steel::has_key_val(catalogue, id)).truthy()) {
                steel::Val entry = steel::idx_get(catalogue, id);
                if ((steel::Val((steel::has_key_val(entry, steel::Val("scope"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("scope")) != steel::Val())).truthy())).truthy()) {
                    scope = steel::idx_get(entry, steel::Val("scope"));
                }
                if ((steel::Val((steel::has_key_val(entry, steel::Val("install_path"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("install_path")) != steel::Val())).truthy())).truthy()) {
                    path = steel::idx_get(entry, steel::Val("install_path"));
                }
            }
            if ((requires_privilege_escalation(scope)).truthy()) {
                needs_root = steel::Val(true);
            }
            (void)(steel::push_val(actions, steel::make_map({{"id", id}, {"action", steel::Val("remove")}, {"from", steel::Val((steel::idx_get(installed, id)).to_string())}, {"to", steel::Val("")}, {"scope", scope}, {"install_path", path}, {"url", steel::Val("")}, {"sha256", steel::Val("")}})));
        }
        i = (i + steel::Val(1.0));
    }
    steel::Val j = steel::Val(0.0);
    while ((steel::Val(j < steel::len_of(target_ids))).truthy()) {
        steel::Val id = steel::idx_get(target_ids, j);
        steel::Val want_ver = steel::Val((steel::idx_get(target_snapshot, id)).to_string());
        steel::Val cur_ver = ((steel::has_key_val(installed, id)).truthy() ? (steel::Val((steel::idx_get(installed, id)).to_string())) : (steel::Val("")));
        steel::Val kind = classify_transition(cur_ver, want_ver);
        if ((steel::Val(kind == steel::Val("noop"))).truthy()) {
            (void)(steel::push_val(skipped, id));
        } else {
            steel::Val scope = steel::Val("system");
            steel::Val path = steel::Val("");
            steel::Val url = steel::Val("");
            steel::Val sha = steel::Val("");
            if ((steel::has_key_val(catalogue, id)).truthy()) {
                steel::Val entry = steel::idx_get(catalogue, id);
                if ((steel::Val((steel::has_key_val(entry, steel::Val("scope"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("scope")) != steel::Val())).truthy())).truthy()) {
                    scope = steel::idx_get(entry, steel::Val("scope"));
                }
                if ((steel::Val((steel::has_key_val(entry, steel::Val("install_path"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("install_path")) != steel::Val())).truthy())).truthy()) {
                    path = steel::idx_get(entry, steel::Val("install_path"));
                }
                if ((steel::Val((steel::has_key_val(entry, steel::Val("url"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("url")) != steel::Val())).truthy())).truthy()) {
                    url = steel::idx_get(entry, steel::Val("url"));
                }
                if ((steel::Val((steel::has_key_val(entry, steel::Val("sha256"))).truthy() && (steel::Val(steel::idx_get(entry, steel::Val("sha256")) != steel::Val())).truthy())).truthy()) {
                    sha = steel::idx_get(entry, steel::Val("sha256"));
                }
            }
            steel::Val pchk = validate_install_path(path, scope);
            if ((steel::Val(!(steel::idx_get(pchk, steel::Val("ok"))).truthy())).truthy()) {
                return steel::make_map({{"ok", steel::Val(false)}, {"actions", steel::make_arr({})}, {"skipped", steel::make_arr({})}, {"requires_root", steel::Val(false)}, {"error", ((id + steel::Val(": ")) + steel::idx_get(pchk, steel::Val("reason")))}});
            }
            if ((requires_privilege_escalation(scope)).truthy()) {
                needs_root = steel::Val(true);
            }
            (void)(steel::push_val(actions, steel::make_map({{"id", id}, {"action", kind}, {"from", cur_ver}, {"to", want_ver}, {"scope", scope}, {"install_path", path}, {"url", url}, {"sha256", sha}})));
        }
        j = (j + steel::Val(1.0));
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"actions", actions}, {"skipped", skipped}, {"requires_root", needs_root}, {"error", steel::Val("")}});
}
steel::Val st_fn_plan_system_snapshot_transition(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return plan_system_snapshot_transition((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val db_default_state() {
    return steel::make_map({{"schema_version", steel::Val(2.0)}, {"installed", steel::make_map({})}, {"ownership", steel::make_map({})}, {"journal", steel::make_arr({})}, {"config", steel::make_map({{"channel", steel::Val("stable")}, {"auto_check", steel::Val(true)}, {"catalogue_url", steel::Val("https://updates.infinityos.uk/catalogue.json")}, {"cache_ttl_sec", steel::Val(3600.0)}})}});
}
steel::Val st_fn_db_default_state(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return db_default_state(); }

steel::Val db_load_from_path(steel::Val db_path) {
    if ((steel::Val(!(steel::file_exists_val(db_path)).truthy())).truthy()) {
        return db_default_state();
    }
    steel::Val raw = steel::read_file_str(db_path);
    if ((steel::Val(steel::len_of(steel::trim_str(raw)) == steel::Val(0.0))).truthy()) {
        return db_default_state();
    }
    steel::Val parsed = steel::json_parse_val(raw);
    if ((steel::Val((steel::Val(parsed == steel::Val())).truthy() || (steel::Val(steel::type_of(parsed) != steel::Val("map"))).truthy())).truthy()) {
        return db_default_state();
    }
    if ((steel::Val(!(steel::has_key_val(parsed, steel::Val("installed"))).truthy())).truthy()) {
        steel::idx_set(parsed, steel::Val("installed"), steel::make_map({}));
    }
    if ((steel::Val(!(steel::has_key_val(parsed, steel::Val("ownership"))).truthy())).truthy()) {
        steel::idx_set(parsed, steel::Val("ownership"), steel::make_map({}));
    }
    if ((steel::Val(!(steel::has_key_val(parsed, steel::Val("journal"))).truthy())).truthy()) {
        steel::idx_set(parsed, steel::Val("journal"), steel::make_arr({}));
    }
    if ((steel::Val(!(steel::has_key_val(parsed, steel::Val("config"))).truthy())).truthy()) {
        steel::idx_set(parsed, steel::Val("config"), steel::idx_get(db_default_state(), steel::Val("config")));
    }
    return parsed;
}
steel::Val st_fn_db_load_from_path(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return db_load_from_path((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val db_save_to_path(steel::Val db_path, steel::Val state) {
    return steel::write_file_str(db_path, steel::json_stringify_val(state));
}
steel::Val st_fn_db_save_to_path(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return db_save_to_path((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val db_apply_transaction(steel::Val state, steel::Val actions, steel::Val timestamp) {
    steel::Val tx_id = (steel::Val("tx-") + steel::Val(((steel::len_of(steel::idx_get(state, steel::Val("journal"))) + steel::Val(1.0))).to_string()));
    steel::Val installed = steel::idx_get(state, steel::Val("installed"));
    steel::Val record_ops = steel::make_arr({});
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(actions))).truthy()) {
        steel::Val a = steel::idx_get(actions, i);
        steel::Val id = steel::idx_get(a, steel::Val("id"));
        steel::Val kind = steel::idx_get(a, steel::Val("action"));
        steel::Val prev_ver = ((steel::has_key_val(installed, id)).truthy() ? (steel::Val((steel::idx_get(installed, id)).to_string())) : (steel::Val("")));
        if ((steel::Val(kind == steel::Val("remove"))).truthy()) {
            (void)(steel::delete_key_val(installed, id));
        } else {
            steel::idx_set(installed, id, steel::idx_get(a, steel::Val("to")));
        }
        (void)(steel::push_val(record_ops, steel::make_map({{"id", id}, {"action", kind}, {"from", prev_ver}, {"to", steel::idx_get(a, steel::Val("to"))}, {"install_path", steel::idx_get(a, steel::Val("install_path"))}})));
        i = (i + steel::Val(1.0));
    }
    steel::Val entry = steel::make_map({{"tx_id", tx_id}, {"timestamp", timestamp}, {"status", steel::Val("committed")}, {"ops", record_ops}});
    (void)(steel::push_val(steel::idx_get(state, steel::Val("journal")), entry));
    return steel::make_map({{"ok", steel::Val(true)}, {"tx_id", tx_id}, {"state", state}});
}
steel::Val st_fn_db_apply_transaction(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return db_apply_transaction((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val db_rollback_last(steel::Val state, steel::Val timestamp) {
    steel::Val journal = steel::idx_get(state, steel::Val("journal"));
    if ((steel::Val(steel::len_of(journal) == steel::Val(0.0))).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"error", steel::Val("no transactions in journal")}, {"state", state}});
    }
    steel::Val idx = (steel::len_of(journal) - steel::Val(1.0));
    while ((steel::Val((steel::Val(idx >= steel::Val(0.0))).truthy() && (steel::Val(steel::idx_get(steel::idx_get(journal, idx), steel::Val("status")) != steel::Val("committed"))).truthy())).truthy()) {
        idx = (idx - steel::Val(1.0));
    }
    if ((steel::Val(idx < steel::Val(0.0))).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"error", steel::Val("no committed transaction to roll back")}, {"state", state}});
    }
    steel::Val tx = steel::idx_get(journal, idx);
    steel::Val ops = steel::idx_get(tx, steel::Val("ops"));
    steel::Val installed = steel::idx_get(state, steel::Val("installed"));
    steel::Val reverted = steel::make_arr({});
    steel::Val j = (steel::len_of(ops) - steel::Val(1.0));
    while ((steel::Val(j >= steel::Val(0.0))).truthy()) {
        steel::Val op = steel::idx_get(ops, j);
        steel::Val id = steel::idx_get(op, steel::Val("id"));
        steel::Val from_ver = steel::idx_get(op, steel::Val("from"));
        if ((steel::Val(steel::len_of(from_ver) == steel::Val(0.0))).truthy()) {
            (void)(steel::delete_key_val(installed, id));
        } else {
            steel::idx_set(installed, id, from_ver);
        }
        (void)(steel::push_val(reverted, steel::make_map({{"id", id}, {"restored_version", from_ver}})));
        j = (j - steel::Val(1.0));
    }
    steel::idx_set(tx, steel::Val("status"), steel::Val("rolled_back"));
    steel::idx_set(tx, steel::Val("rolled_back_at"), timestamp);
    return steel::make_map({{"ok", steel::Val(true)}, {"tx_id", steel::idx_get(tx, steel::Val("tx_id"))}, {"reverted", reverted}, {"state", state}});
}
steel::Val st_fn_db_rollback_last(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return db_rollback_last((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val db_set_config(steel::Val state, steel::Val key, steel::Val value) {
    steel::Val cfg = steel::idx_get(state, steel::Val("config"));
    if ((steel::Val(key == steel::Val("channel"))).truthy()) {
        if ((steel::Val((steel::Val((steel::Val(value != steel::Val("stable"))).truthy() && (steel::Val(value != steel::Val("beta"))).truthy())).truthy() && (steel::Val(value != steel::Val("nightly"))).truthy())).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"error", (steel::Val("invalid channel: ") + steel::Val((value).to_string()))}});
        }
        steel::idx_set(cfg, steel::Val("channel"), value);
        return steel::make_map({{"ok", steel::Val(true)}, {"error", steel::Val("")}});
    }
    if ((steel::Val(key == steel::Val("auto_check"))).truthy()) {
        steel::idx_set(cfg, steel::Val("auto_check"), steel::Val((steel::Val(value == steel::Val("true"))).truthy() || (steel::Val(value == steel::Val(true))).truthy()));
        return steel::make_map({{"ok", steel::Val(true)}, {"error", steel::Val("")}});
    }
    if ((steel::Val(key == steel::Val("catalogue_url"))).truthy()) {
        if ((steel::Val(!(steel::starts_with_str(steel::Val((value).to_string()), steel::Val("https://"))).truthy())).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"error", steel::Val("catalogue_url must use https://")}});
        }
        steel::idx_set(cfg, steel::Val("catalogue_url"), steel::Val((value).to_string()));
        return steel::make_map({{"ok", steel::Val(true)}, {"error", steel::Val("")}});
    }
    if ((steel::Val(key == steel::Val("cache_ttl_sec"))).truthy()) {
        steel::Val n = steel::Val(std::trunc((steel::Val((value).as_num())).as_num()));
        if ((steel::Val(n < steel::Val(60.0))).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"error", steel::Val("cache_ttl_sec must be at least 60")}});
        }
        steel::idx_set(cfg, steel::Val("cache_ttl_sec"), n);
        return steel::make_map({{"ok", steel::Val(true)}, {"error", steel::Val("")}});
    }
    return steel::make_map({{"ok", steel::Val(false)}, {"error", (steel::Val("unknown config key: ") + steel::Val((key).to_string()))}});
}
steel::Val st_fn_db_set_config(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return db_set_config((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val parse_cli_flags(steel::Val args) {
    steel::Val positional = steel::make_arr({});
    steel::Val flags = steel::make_map({{"json", steel::Val(false)}, {"dry_run", steel::Val(false)}, {"force", steel::Val(false)}, {"channel", steel::Val("")}});
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(args))).truthy()) {
        steel::Val a = steel::idx_get(args, i);
        if ((steel::Val(a == steel::Val("--json"))).truthy()) {
            steel::idx_set(flags, steel::Val("json"), steel::Val(true));
        } else {
            if ((steel::Val(a == steel::Val("--dry-run"))).truthy()) {
                steel::idx_set(flags, steel::Val("dry_run"), steel::Val(true));
            } else {
                if ((steel::Val((steel::Val(a == steel::Val("--force"))).truthy() || (steel::Val(a == steel::Val("-f"))).truthy())).truthy()) {
                    steel::idx_set(flags, steel::Val("force"), steel::Val(true));
                } else {
                    if ((steel::Val((steel::Val(a == steel::Val("--channel"))).truthy() && (steel::Val((i + steel::Val(1.0)) < steel::len_of(args))).truthy())).truthy()) {
                        i = (i + steel::Val(1.0));
                        steel::idx_set(flags, steel::Val("channel"), steel::idx_get(args, i));
                    } else {
                        (void)(steel::push_val(positional, a));
                    }
                }
            }
        }
        i = (i + steel::Val(1.0));
    }
    return steel::make_map({{"positional", positional}, {"flags", flags}});
}
steel::Val st_fn_parse_cli_flags(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return parse_cli_flags((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val format_help_text() {
    steel::Val lines = steel::make_arr({steel::Val("InfinityOS Updater (Steel-compiled core)"), steel::Val(""), steel::Val("Usage: infinity-updater <command> [args] [flags]"), steel::Val(""), steel::Val("Commands:"), steel::Val("  status              Show installed components and pending updates"), steel::Val("  list                List installed components"), steel::Val("  search [query]      Search catalogue components"), steel::Val("  check               Check for available updates"), steel::Val("  install <id...>     Plan/install one or more components"), steel::Val("  upgrade [id...]     Plan/upgrade installed components"), steel::Val("  remove <id>         Plan/remove an installed component"), steel::Val("  rollback            Roll back the most recent transaction"), steel::Val("  history             Show transaction journal"), steel::Val("  verify              Validate catalogue schema and security policy"), steel::Val("  config [k] [v]      Show or update configuration"), steel::Val(""), steel::Val("Flags:"), steel::Val("  --json              Emit machine-readable JSON output"), steel::Val("  --dry-run           Plan actions without modifying state"), steel::Val("  --force, -f         Bypass reverse-dependency checks on remove"), steel::Val("  --channel <name>    Filter channel (stable|beta|nightly)")});
    return steel::join_arr(lines, steel::Val("\n"));
}
steel::Val st_fn_format_help_text(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return format_help_text(); }

steel::Val format_plan_lines(steel::Val plan) {
    if ((steel::Val(!(steel::idx_get(plan, steel::Val("ok"))).truthy())).truthy()) {
        return (steel::Val("Error: ") + steel::Val((steel::idx_get(plan, steel::Val("error"))).to_string()));
    }
    steel::Val lines = steel::make_arr({});
    steel::Val acts = steel::idx_get(plan, steel::Val("actions"));
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(acts))).truthy()) {
        steel::Val a = steel::idx_get(acts, i);
        steel::Val arrow = ((steel::Val(steel::len_of(steel::idx_get(a, steel::Val("from"))) > steel::Val(0.0))).truthy() ? (((steel::idx_get(a, steel::Val("from")) + steel::Val(" -> ")) + steel::idx_get(a, steel::Val("to")))) : (steel::idx_get(a, steel::Val("to"))));
        if ((steel::Val(steel::idx_get(a, steel::Val("action")) == steel::Val("remove"))).truthy()) {
            arrow = steel::idx_get(a, steel::Val("from"));
        }
        (void)(steel::push_val(lines, (((((((steel::Val("  [") + steel::upper_str(steel::idx_get(a, steel::Val("action")))) + steel::Val("] ")) + steel::idx_get(a, steel::Val("id"))) + steel::Val(" (")) + arrow) + steel::Val(") -> ")) + steel::idx_get(a, steel::Val("install_path")))));
        i = (i + steel::Val(1.0));
    }
    if ((steel::Val(steel::len_of(steel::idx_get(plan, steel::Val("skipped"))) > steel::Val(0.0))).truthy()) {
        (void)(steel::push_val(lines, (steel::Val("  [SKIP] already up to date: ") + steel::join_arr(steel::idx_get(plan, steel::Val("skipped")), steel::Val(", ")))));
    }
    if ((steel::Val(steel::len_of(lines) == steel::Val(0.0))).truthy()) {
        return steel::Val("Nothing to do.");
    }
    return steel::join_arr(lines, steel::Val("\n"));
}
steel::Val st_fn_format_plan_lines(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return format_plan_lines((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val cli_dispatch(steel::Val args, steel::Val state, steel::Val catalogue, steel::Val timestamp) {
    steel::Val parsed = parse_cli_flags(args);
    steel::Val pos = steel::idx_get(parsed, steel::Val("positional"));
    steel::Val flags = steel::idx_get(parsed, steel::Val("flags"));
    steel::Val channel = ((steel::Val(steel::len_of(steel::idx_get(flags, steel::Val("channel"))) > steel::Val(0.0))).truthy() ? (steel::idx_get(flags, steel::Val("channel"))) : (steel::idx_get(steel::idx_get(state, steel::Val("config")), steel::Val("channel"))));
    if ((steel::Val((steel::Val((steel::Val((steel::Val(steel::len_of(pos) == steel::Val(0.0))).truthy() || (steel::Val(steel::idx_get(pos, steel::Val(0.0)) == steel::Val("help"))).truthy())).truthy() || (steel::Val(steel::idx_get(pos, steel::Val(0.0)) == steel::Val("--help"))).truthy())).truthy() || (steel::Val(steel::idx_get(pos, steel::Val(0.0)) == steel::Val("-h"))).truthy())).truthy()) {
        return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", format_help_text()}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
    }
    steel::Val cmd = steel::idx_get(pos, steel::Val(0.0));
    if ((steel::Val(cmd == steel::Val("list"))).truthy()) {
        steel::Val ks = steel::sort_val(steel::keys_of(steel::idx_get(state, steel::Val("installed"))));
        if ((steel::idx_get(flags, steel::Val("json"))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", steel::json_stringify_val(steel::idx_get(state, steel::Val("installed")))}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        if ((steel::Val(steel::len_of(ks) == steel::Val(0.0))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", steel::Val("No components installed.")}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        steel::Val lines = steel::make_arr({});
        steel::Val i = steel::Val(0.0);
        while ((steel::Val(i < steel::len_of(ks))).truthy()) {
            steel::Val id = steel::idx_get(ks, i);
            (void)(steel::push_val(lines, ((steel::pad_end_str(id, steel::Val(24.0), steel::Val(" ")) + steel::Val(" ")) + steel::Val((steel::idx_get(steel::idx_get(state, steel::Val("installed")), id)).to_string()))));
            i = (i + steel::Val(1.0));
        }
        return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", steel::join_arr(lines, steel::Val("\n"))}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
    }
    if ((steel::Val(cmd == steel::Val("search"))).truthy()) {
        steel::Val q = ((steel::Val(steel::len_of(pos) > steel::Val(1.0))).truthy() ? (steel::idx_get(pos, steel::Val(1.0))) : (steel::Val("")));
        steel::Val hits = search_catalogue(catalogue, q, channel);
        if ((steel::idx_get(flags, steel::Val("json"))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", steel::json_stringify_val(hits)}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        if ((steel::Val(steel::len_of(hits) == steel::Val(0.0))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", steel::Val("No matching components.")}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        steel::Val lines = steel::make_arr({});
        steel::Val i = steel::Val(0.0);
        while ((steel::Val(i < steel::len_of(hits))).truthy()) {
            steel::Val h = steel::idx_get(hits, i);
            (void)(steel::push_val(lines, ((((steel::pad_end_str(steel::idx_get(h, steel::Val("id")), steel::Val(22.0), steel::Val(" ")) + steel::Val(" ")) + steel::pad_end_str(steel::idx_get(h, steel::Val("version")), steel::Val(10.0), steel::Val(" "))) + steel::Val(" ")) + steel::idx_get(h, steel::Val("description")))));
            i = (i + steel::Val(1.0));
        }
        return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", steel::join_arr(lines, steel::Val("\n"))}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
    }
    if ((steel::Val((steel::Val(cmd == steel::Val("check"))).truthy() || (steel::Val(cmd == steel::Val("status"))).truthy())).truthy()) {
        steel::Val chk = plan_check_updates(steel::idx_get(state, steel::Val("installed")), catalogue, channel);
        if ((steel::idx_get(flags, steel::Val("json"))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", steel::json_stringify_val(chk)}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        steel::Val ups = steel::idx_get(chk, steel::Val("updates"));
        if ((steel::Val(steel::len_of(ups) == steel::Val(0.0))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", ((steel::Val("All ") + steel::Val((steel::len_of(steel::idx_get(chk, steel::Val("up_to_date")))).to_string())) + steel::Val(" installed components are up to date."))}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        steel::Val lines = steel::make_arr({((steel::Val("Available updates (") + steel::Val((steel::len_of(ups)).to_string())) + steel::Val("):"))});
        steel::Val i = steel::Val(0.0);
        while ((steel::Val(i < steel::len_of(ups))).truthy()) {
            steel::Val u = steel::idx_get(ups, i);
            (void)(steel::push_val(lines, (((((steel::Val("  ") + steel::idx_get(u, steel::Val("id"))) + steel::Val(": ")) + steel::idx_get(u, steel::Val("from"))) + steel::Val(" -> ")) + steel::idx_get(u, steel::Val("to")))));
            i = (i + steel::Val(1.0));
        }
        return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", steel::join_arr(lines, steel::Val("\n"))}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
    }
    if ((steel::Val(cmd == steel::Val("verify"))).truthy()) {
        steel::Val v = validate_catalogue(catalogue);
        steel::Val out = ((steel::idx_get(flags, steel::Val("json"))).truthy() ? (steel::json_stringify_val(v)) : (((steel::idx_get(v, steel::Val("ok"))).truthy() ? (((steel::Val("Catalogue OK (") + steel::Val((steel::idx_get(v, steel::Val("count"))).to_string())) + steel::Val(" components)."))) : ((steel::Val("Catalogue errors:\n  ") + steel::join_arr(steel::idx_get(v, steel::Val("errors")), steel::Val("\n  ")))))));
        return steel::make_map({{"exit_code", ((steel::idx_get(v, steel::Val("ok"))).truthy() ? (steel::Val(0.0)) : (steel::Val(1.0)))}, {"output", out}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
    }
    if ((steel::Val((steel::Val(cmd == steel::Val("install"))).truthy() || (steel::Val(cmd == steel::Val("upgrade"))).truthy())).truthy()) {
        steel::Val targets = steel::slice_val(pos, steel::Val(1.0), steel::len_of(pos));
        if ((steel::Val((steel::Val(cmd == steel::Val("upgrade"))).truthy() && (steel::Val(steel::len_of(targets) == steel::Val(0.0))).truthy())).truthy()) {
            targets = steel::sort_val(steel::keys_of(steel::idx_get(state, steel::Val("installed"))));
        }
        if ((steel::Val(steel::len_of(targets) == steel::Val(0.0))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(2.0)}, {"output", steel::Val("Error: specify at least one component id")}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        steel::Val plan = plan_reconcile(steel::idx_get(state, steel::Val("installed")), catalogue, targets);
        if ((steel::Val(!(steel::idx_get(plan, steel::Val("ok"))).truthy())).truthy()) {
            steel::Val out = ((steel::idx_get(flags, steel::Val("json"))).truthy() ? (steel::json_stringify_val(plan)) : ((steel::Val("Error: ") + steel::idx_get(plan, steel::Val("error")))));
            return steel::make_map({{"exit_code", steel::Val(1.0)}, {"output", out}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        steel::Val should_save = steel::Val((steel::Val(!(steel::idx_get(flags, steel::Val("dry_run"))).truthy())).truthy() && (steel::Val(steel::len_of(steel::idx_get(plan, steel::Val("actions"))) > steel::Val(0.0))).truthy());
        if ((should_save).truthy()) {
            (void)(db_apply_transaction(state, steel::idx_get(plan, steel::Val("actions")), timestamp));
        }
        steel::Val out = ((steel::idx_get(flags, steel::Val("json"))).truthy() ? (steel::json_stringify_val(plan)) : (format_plan_lines(plan)));
        return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", out}, {"actions", ((steel::idx_get(flags, steel::Val("dry_run"))).truthy() ? (steel::make_arr({})) : (steel::idx_get(plan, steel::Val("actions"))))}, {"save_state", should_save}, {"state", state}});
    }
    if ((steel::Val(cmd == steel::Val("remove"))).truthy()) {
        if ((steel::Val(steel::len_of(pos) < steel::Val(2.0))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(2.0)}, {"output", steel::Val("Error: specify a component id to remove")}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        steel::Val plan = plan_remove(steel::idx_get(state, steel::Val("installed")), catalogue, steel::idx_get(pos, steel::Val(1.0)), steel::idx_get(flags, steel::Val("force")));
        if ((steel::Val(!(steel::idx_get(plan, steel::Val("ok"))).truthy())).truthy()) {
            steel::Val out = ((steel::idx_get(flags, steel::Val("json"))).truthy() ? (steel::json_stringify_val(plan)) : ((steel::Val("Error: ") + steel::idx_get(plan, steel::Val("error")))));
            return steel::make_map({{"exit_code", steel::Val(1.0)}, {"output", out}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        steel::Val should_save = steel::Val(!(steel::idx_get(flags, steel::Val("dry_run"))).truthy());
        if ((should_save).truthy()) {
            (void)(db_apply_transaction(state, steel::idx_get(plan, steel::Val("actions")), timestamp));
        }
        steel::Val out = ((steel::idx_get(flags, steel::Val("json"))).truthy() ? (steel::json_stringify_val(plan)) : (format_plan_lines(plan)));
        return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", out}, {"actions", ((steel::idx_get(flags, steel::Val("dry_run"))).truthy() ? (steel::make_arr({})) : (steel::idx_get(plan, steel::Val("actions"))))}, {"save_state", should_save}, {"state", state}});
    }
    if ((steel::Val(cmd == steel::Val("rollback"))).truthy()) {
        steel::Val rb = db_rollback_last(state, timestamp);
        if ((steel::Val(!(steel::idx_get(rb, steel::Val("ok"))).truthy())).truthy()) {
            steel::Val out = ((steel::idx_get(flags, steel::Val("json"))).truthy() ? (steel::json_stringify_val(rb)) : ((steel::Val("Error: ") + steel::idx_get(rb, steel::Val("error")))));
            return steel::make_map({{"exit_code", steel::Val(1.0)}, {"output", out}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        steel::Val out = ((steel::idx_get(flags, steel::Val("json"))).truthy() ? (steel::json_stringify_val(rb)) : (((((steel::Val("Rolled back ") + steel::idx_get(rb, steel::Val("tx_id"))) + steel::Val(" (")) + steel::Val((steel::len_of(steel::idx_get(rb, steel::Val("reverted")))).to_string())) + steel::Val(" components restored)."))));
        return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", out}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(!(steel::idx_get(flags, steel::Val("dry_run"))).truthy())}, {"state", state}});
    }
    if ((steel::Val(cmd == steel::Val("history"))).truthy()) {
        if ((steel::idx_get(flags, steel::Val("json"))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", steel::json_stringify_val(steel::idx_get(state, steel::Val("journal")))}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        steel::Val j = steel::idx_get(state, steel::Val("journal"));
        if ((steel::Val(steel::len_of(j) == steel::Val(0.0))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", steel::Val("No transactions recorded.")}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        steel::Val lines = steel::make_arr({});
        steel::Val i = steel::Val(0.0);
        while ((steel::Val(i < steel::len_of(j))).truthy()) {
            steel::Val tx = steel::idx_get(j, i);
            (void)(steel::push_val(lines, ((((((steel::idx_get(tx, steel::Val("tx_id")) + steel::Val(" [")) + steel::idx_get(tx, steel::Val("status"))) + steel::Val("] ops=")) + steel::Val((steel::len_of(steel::idx_get(tx, steel::Val("ops")))).to_string())) + steel::Val(" ts=")) + steel::Val((steel::idx_get(tx, steel::Val("timestamp"))).to_string()))));
            i = (i + steel::Val(1.0));
        }
        return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", steel::join_arr(lines, steel::Val("\n"))}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
    }
    if ((steel::Val(cmd == steel::Val("config"))).truthy()) {
        if ((steel::Val(steel::len_of(pos) == steel::Val(1.0))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", steel::json_stringify_val(steel::idx_get(state, steel::Val("config")))}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        if ((steel::Val(steel::len_of(pos) < steel::Val(3.0))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(2.0)}, {"output", steel::Val("Usage: infinity-updater config <key> <value>")}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        steel::Val res = db_set_config(state, steel::idx_get(pos, steel::Val(1.0)), steel::idx_get(pos, steel::Val(2.0)));
        if ((steel::Val(!(steel::idx_get(res, steel::Val("ok"))).truthy())).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(1.0)}, {"output", (steel::Val("Error: ") + steel::idx_get(res, steel::Val("error")))}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
        }
        return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", (((steel::Val("Updated ") + steel::idx_get(pos, steel::Val(1.0))) + steel::Val(" = ")) + steel::idx_get(pos, steel::Val(2.0)))}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(true)}, {"state", state}});
    }
    return steel::make_map({{"exit_code", steel::Val(2.0)}, {"output", ((steel::Val("Unknown command: ") + cmd) + steel::Val("\nRun 'infinity-updater help' for usage."))}, {"actions", steel::make_arr({})}, {"save_state", steel::Val(false)}, {"state", state}});
}
steel::Val st_fn_cli_dispatch(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return cli_dispatch((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val()), (st_args.size() > 3 ? st_args[3] : steel::Val())); }

steel::Val ui_initial_state() {
    return steel::make_map({{"active_tab", steel::Val("updates")}, {"search_query", steel::Val("")}, {"selected_ids", steel::make_arr({})}, {"busy", steel::Val(false)}, {"banner_level", steel::Val("info")}, {"banner_text", steel::Val("Ready")}, {"pending_plan", steel::Val()}});
}
steel::Val st_fn_ui_initial_state(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return ui_initial_state(); }

steel::Val ui_toggle_selection(steel::Val selected, steel::Val id) {
    steel::Val out = steel::make_arr({});
    steel::Val found = steel::Val(false);
    steel::Val i = steel::Val(0.0);
    while ((steel::Val(i < steel::len_of(selected))).truthy()) {
        if ((steel::Val(steel::idx_get(selected, i) == id)).truthy()) {
            found = steel::Val(true);
        } else {
            (void)(steel::push_val(out, steel::idx_get(selected, i)));
        }
        i = (i + steel::Val(1.0));
    }
    if ((steel::Val(!(found).truthy())).truthy()) {
        (void)(steel::push_val(out, id));
    }
    return out;
}
steel::Val st_fn_ui_toggle_selection(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return ui_toggle_selection((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val ui_reduce(steel::Val ui_state, steel::Val event, steel::Val db_state, steel::Val catalogue) {
    steel::Val kind = steel::idx_get(event, steel::Val("type"));
    if ((steel::Val(kind == steel::Val("switch_tab"))).truthy()) {
        steel::Val tab = steel::idx_get(event, steel::Val("tab"));
        if ((steel::Val((steel::Val((steel::Val((steel::Val((steel::Val(tab == steel::Val("updates"))).truthy() || (steel::Val(tab == steel::Val("installed"))).truthy())).truthy() || (steel::Val(tab == steel::Val("explore"))).truthy())).truthy() || (steel::Val(tab == steel::Val("history"))).truthy())).truthy() || (steel::Val(tab == steel::Val("settings"))).truthy())).truthy()) {
            steel::idx_set(ui_state, steel::Val("active_tab"), tab);
        }
        return ui_state;
    }
    if ((steel::Val(kind == steel::Val("set_search"))).truthy()) {
        steel::idx_set(ui_state, steel::Val("search_query"), steel::Val((steel::idx_get(event, steel::Val("query"))).to_string()));
        return ui_state;
    }
    if ((steel::Val(kind == steel::Val("toggle_select"))).truthy()) {
        steel::idx_set(ui_state, steel::Val("selected_ids"), ui_toggle_selection(steel::idx_get(ui_state, steel::Val("selected_ids")), steel::Val((steel::idx_get(event, steel::Val("id"))).to_string())));
        return ui_state;
    }
    if ((steel::Val(kind == steel::Val("select_all_updates"))).truthy()) {
        steel::Val chk = plan_check_updates(steel::idx_get(db_state, steel::Val("installed")), catalogue, steel::idx_get(steel::idx_get(db_state, steel::Val("config")), steel::Val("channel")));
        steel::Val ids = steel::make_arr({});
        steel::Val ups = steel::idx_get(chk, steel::Val("updates"));
        steel::Val i = steel::Val(0.0);
        while ((steel::Val(i < steel::len_of(ups))).truthy()) {
            (void)(steel::push_val(ids, steel::idx_get(steel::idx_get(ups, i), steel::Val("id"))));
            i = (i + steel::Val(1.0));
        }
        steel::idx_set(ui_state, steel::Val("selected_ids"), ids);
        steel::idx_set(ui_state, steel::Val("banner_level"), steel::Val("info"));
        steel::idx_set(ui_state, steel::Val("banner_text"), (steel::Val((steel::len_of(ids)).to_string()) + steel::Val(" updates selected")));
        return ui_state;
    }
    if ((steel::Val(kind == steel::Val("preview_plan"))).truthy()) {
        steel::Val plan = plan_reconcile(steel::idx_get(db_state, steel::Val("installed")), catalogue, steel::idx_get(ui_state, steel::Val("selected_ids")));
        steel::idx_set(ui_state, steel::Val("pending_plan"), plan);
        if ((steel::Val(!(steel::idx_get(plan, steel::Val("ok"))).truthy())).truthy()) {
            steel::idx_set(ui_state, steel::Val("banner_level"), steel::Val("error"));
            steel::idx_set(ui_state, steel::Val("banner_text"), steel::idx_get(plan, steel::Val("error")));
        } else {
            steel::idx_set(ui_state, steel::Val("banner_level"), steel::Val("success"));
            steel::idx_set(ui_state, steel::Val("banner_text"), ((steel::Val("Ready to apply ") + steel::Val((steel::len_of(steel::idx_get(plan, steel::Val("actions")))).to_string())) + steel::Val(" actions")));
        }
        return ui_state;
    }
    if ((steel::Val(kind == steel::Val("clear_selection"))).truthy()) {
        steel::idx_set(ui_state, steel::Val("selected_ids"), steel::make_arr({}));
        steel::idx_set(ui_state, steel::Val("pending_plan"), steel::Val());
        steel::idx_set(ui_state, steel::Val("banner_level"), steel::Val("info"));
        steel::idx_set(ui_state, steel::Val("banner_text"), steel::Val("Selection cleared"));
        return ui_state;
    }
    return ui_state;
}
steel::Val st_fn_ui_reduce(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return ui_reduce((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val()), (st_args.size() > 3 ? st_args[3] : steel::Val())); }

steel::Val build_package_manifest(steel::Val id, steel::Val version, steel::Val scope, steel::Val install_path, steel::Val sha256, steel::Val signature, steel::Val files) {
    steel::Val entry_check = validate_catalogue_entry(id, steel::make_map({{"version", version}, {"scope", scope}, {"install_path", install_path}, {"sha256", sha256}}));
    if ((steel::Val(!(steel::idx_get(entry_check, steel::Val("ok"))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"error", steel::idx_get(entry_check, steel::Val("reason"))}, {"manifest", steel::Val()}});
    }
    steel::Val arch_check = validate_archive_manifest(files);
    if ((steel::Val(!(steel::idx_get(arch_check, steel::Val("ok"))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"error", steel::idx_get(arch_check, steel::Val("reason"))}, {"manifest", steel::Val()}});
    }
    if ((steel::Val((steel::Val((steel::Val(signature != steel::Val())).truthy() && (steel::Val(steel::len_of(steel::Val((signature).to_string())) > steel::Val(0.0))).truthy())).truthy() && (steel::Val(!(validate_ed25519_sig(steel::Val((signature).to_string()))).truthy())).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"error", steel::Val("invalid ed25519 signature hex")}, {"manifest", steel::Val()}});
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"error", steel::Val("")}, {"manifest", steel::make_map({{"id", id}, {"version", version}, {"scope", scope}, {"install_path", install_path}, {"sha256", steel::lower_str(steel::Val((sha256).to_string()))}, {"signature", ((steel::Val(signature != steel::Val())).truthy() ? (steel::lower_str(steel::Val((signature).to_string()))) : (steel::Val("")))}, {"file_count", steel::len_of(files)}, {"files", files}})}});
}
steel::Val st_fn_build_package_manifest(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return build_package_manifest((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val()), (st_args.size() > 3 ? st_args[3] : steel::Val()), (st_args.size() > 4 ? st_args[4] : steel::Val()), (st_args.size() > 5 ? st_args[5] : steel::Val()), (st_args.size() > 6 ? st_args[6] : steel::Val())); }

steel::Val publish_catalogue_entry(steel::Val catalogue, steel::Val id, steel::Val entry) {
    steel::Val chk = validate_catalogue_entry(id, entry);
    if ((steel::Val(!(steel::idx_get(chk, steel::Val("ok"))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"error", steel::idx_get(chk, steel::Val("reason"))}, {"catalogue", catalogue}});
    }
    if ((steel::has_key_val(catalogue, id)).truthy()) {
        steel::Val prev = steel::idx_get(steel::idx_get(catalogue, id), steel::Val("version"));
        if ((steel::Val(compare_semver(steel::idx_get(entry, steel::Val("version")), prev) < steel::Val(0.0))).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"error", (((steel::Val("cannot publish older version ") + steel::Val((steel::idx_get(entry, steel::Val("version"))).to_string())) + steel::Val(" over ")) + steel::Val((prev).to_string()))}, {"catalogue", catalogue}});
        }
    }
    steel::idx_set(catalogue, id, entry);
    return steel::make_map({{"ok", steel::Val(true)}, {"error", steel::Val("")}, {"catalogue", catalogue}});
}
steel::Val st_fn_publish_catalogue_entry(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return publish_catalogue_entry((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val validate_helper_request(steel::Val req) {
    if ((steel::Val((steel::Val(req == steel::Val())).truthy() || (steel::Val(steel::type_of(req) != steel::Val("map"))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("helper request must be a JSON object")}});
    }
    if ((steel::Val((steel::Val((steel::Val(!(steel::has_key_val(req, steel::Val("op"))).truthy())).truthy() || (steel::Val(!(steel::has_key_val(req, steel::Val("component_id"))).truthy())).truthy())).truthy() || (steel::Val(!(steel::has_key_val(req, steel::Val("install_path"))).truthy())).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("missing required helper fields (op, component_id, install_path)")}});
    }
    steel::Val op = steel::Val((steel::idx_get(req, steel::Val("op"))).to_string());
    if ((steel::Val((steel::Val((steel::Val((steel::Val(op != steel::Val("install"))).truthy() && (steel::Val(op != steel::Val("upgrade"))).truthy())).truthy() && (steel::Val(op != steel::Val("downgrade"))).truthy())).truthy() && (steel::Val(op != steel::Val("remove"))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", (steel::Val("unsupported helper op: ") + op)}});
    }
    steel::Val id = steel::Val((steel::idx_get(req, steel::Val("component_id"))).to_string());
    if ((steel::Val(!(is_valid_component_id(id)).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", (steel::Val("invalid component_id: ") + id)}});
    }
    steel::Val scope = ((steel::Val((steel::has_key_val(req, steel::Val("scope"))).truthy() && (steel::Val(steel::idx_get(req, steel::Val("scope")) != steel::Val())).truthy())).truthy() ? (steel::Val((steel::idx_get(req, steel::Val("scope"))).to_string())) : (steel::Val("system")));
    steel::Val pchk = validate_install_path(steel::Val((steel::idx_get(req, steel::Val("install_path"))).to_string()), scope);
    if ((steel::Val(!(steel::idx_get(pchk, steel::Val("ok"))).truthy())).truthy()) {
        return pchk;
    }
    if ((steel::Val(op != steel::Val("remove"))).truthy()) {
        if ((steel::Val((steel::Val(!(steel::has_key_val(req, steel::Val("archive_path"))).truthy())).truthy() || (steel::Val(steel::len_of(steel::trim_str(steel::Val((steel::idx_get(req, steel::Val("archive_path"))).to_string()))) == steel::Val(0.0))).truthy())).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"reason", (steel::Val("archive_path required for ") + op)}});
        }
        if ((steel::Val((steel::Val((steel::has_key_val(req, steel::Val("sha256"))).truthy() && (steel::Val(steel::idx_get(req, steel::Val("sha256")) != steel::Val())).truthy())).truthy() && (steel::Val(steel::len_of(steel::Val((steel::idx_get(req, steel::Val("sha256"))).to_string())) > steel::Val(0.0))).truthy())).truthy()) {
            if ((steel::Val(!(validate_sha256(steel::Val((steel::idx_get(req, steel::Val("sha256"))).to_string()))).truthy())).truthy()) {
                return steel::make_map({{"ok", steel::Val(false)}, {"reason", steel::Val("invalid sha256 in helper request")}});
            }
        }
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"reason", steel::Val("")}});
}
steel::Val st_fn_validate_helper_request(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return validate_helper_request((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val helper_check_dpkg_owner(steel::Val target_path) {
    steel::Val res = ([&](){ steel::Val __av = steel::make_arr({steel::Val("-S"), target_path}); return steel::exec_val(steel::Val("dpkg"), &__av); }());
    if ((steel::Val((steel::Val((steel::Val((steel::Val(res != steel::Val())).truthy() && (steel::Val(steel::type_of(res) == steel::Val("map"))).truthy())).truthy() && (steel::Val(steel::idx_get(res, steel::Val("code")) == steel::Val(0.0))).truthy())).truthy() && (steel::Val(steel::len_of(steel::trim_str(steel::Val((steel::idx_get(res, steel::Val("stdout"))).to_string()))) > steel::Val(0.0))).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"reason", (steel::Val("path owned by dpkg: ") + steel::trim_str(steel::Val((steel::idx_get(res, steel::Val("stdout"))).to_string())))}});
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"reason", steel::Val("")}});
}
steel::Val st_fn_helper_check_dpkg_owner(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return helper_check_dpkg_owner((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val helper_backup_path(steel::Val backup_root, steel::Val tx_id, steel::Val component_id) {
    steel::Val root = ((steel::Val((steel::Val(backup_root != steel::Val())).truthy() && (steel::Val(steel::len_of(steel::trim_str(steel::Val((backup_root).to_string()))) > steel::Val(0.0))).truthy())).truthy() ? (steel::Val((backup_root).to_string())) : (steel::Val("/var/lib/infinityos-updater/backups")));
    steel::Val tx = ((steel::Val((steel::Val(tx_id != steel::Val())).truthy() && (steel::Val(steel::len_of(steel::trim_str(steel::Val((tx_id).to_string()))) > steel::Val(0.0))).truthy())).truthy() ? (steel::Val((tx_id).to_string())) : (steel::Val("tx-current")));
    return ((((root + steel::Val("/")) + tx) + steel::Val("/")) + steel::Val((component_id).to_string()));
}
steel::Val st_fn_helper_backup_path(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return helper_backup_path((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val helper_create_backup(steel::Val dest, steel::Val backup_path) {
    if ((steel::Val(!(steel::file_exists_val(dest)).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(true)}, {"backed_up", steel::Val(false)}, {"backup_path", steel::Val("")}});
    }
    steel::Val parent_idx = (steel::len_of(backup_path) - steel::Val(1.0));
    while ((steel::Val((steel::Val(parent_idx > steel::Val(0.0))).truthy() && (steel::Val(steel::slice_val(backup_path, parent_idx, (parent_idx + steel::Val(1.0))) != steel::Val("/"))).truthy())).truthy()) {
        parent_idx = (parent_idx - steel::Val(1.0));
    }
    steel::Val parent_dir = steel::slice_val(backup_path, steel::Val(0.0), parent_idx);
    if ((steel::Val(steel::len_of(parent_dir) > steel::Val(0.0))).truthy()) {
        (void)(steel::mkdir_val(parent_dir));
    }
    if ((steel::file_exists_val(backup_path)).truthy()) {
        (void)(steel::remove_dir_val(backup_path));
    }
    if ((steel::Val(!(steel::rename_val(dest, backup_path)).truthy())).truthy()) {
        return steel::make_map({{"ok", steel::Val(false)}, {"backed_up", steel::Val(false)}, {"backup_path", steel::Val("")}});
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"backed_up", steel::Val(true)}, {"backup_path", backup_path}});
}
steel::Val st_fn_helper_create_backup(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return helper_create_backup((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val helper_restore_backup(steel::Val dest, steel::Val backup_path) {
    if ((steel::file_exists_val(dest)).truthy()) {
        (void)(steel::remove_dir_val(dest));
    }
    if ((steel::Val((steel::Val((steel::Val(backup_path != steel::Val())).truthy() && (steel::Val(steel::len_of(steel::Val((backup_path).to_string())) > steel::Val(0.0))).truthy())).truthy() && (steel::file_exists_val(steel::Val((backup_path).to_string()))).truthy())).truthy()) {
        if ((steel::Val(!(steel::rename_val(steel::Val((backup_path).to_string()), dest)).truthy())).truthy()) {
            return steel::make_map({{"ok", steel::Val(false)}, {"error", (steel::Val("failed to restore backup from ") + steel::Val((backup_path).to_string()))}});
        }
    }
    return steel::make_map({{"ok", steel::Val(true)}, {"error", steel::Val("")}});
}
steel::Val st_fn_helper_restore_backup(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return helper_restore_backup((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val helper_execute_request(steel::Val req_json) {
    steel::Val req = steel::json_parse_val(req_json);
    if ((steel::Val((steel::Val((steel::Val((steel::Val(req != steel::Val())).truthy() && (steel::Val(steel::type_of(req) == steel::Val("map"))).truthy())).truthy() && (steel::has_key_val(req, steel::Val("op"))).truthy())).truthy() && (steel::Val(steel::idx_get(req, steel::Val("op")) == steel::Val("restore_backup"))).truthy())).truthy()) {
        steel::Val dest = steel::Val((steel::idx_get(req, steel::Val("install_path"))).to_string());
        steel::Val bp = steel::Val((steel::idx_get(req, steel::Val("backup_path"))).to_string());
        steel::Val r = helper_restore_backup(dest, bp);
        return steel::make_map({{"exit_code", ((steel::idx_get(r, steel::Val("ok"))).truthy() ? (steel::Val(0.0)) : (steel::Val(1.0)))}, {"error", steel::idx_get(r, steel::Val("error"))}});
    }
    steel::Val chk = validate_helper_request(req);
    if ((steel::Val(!(steel::idx_get(chk, steel::Val("ok"))).truthy())).truthy()) {
        return steel::make_map({{"exit_code", steel::Val(2.0)}, {"error", (steel::Val("policy rejected request: ") + steel::idx_get(chk, steel::Val("reason")))}});
    }
    steel::Val op = steel::Val((steel::idx_get(req, steel::Val("op"))).to_string());
    steel::Val id = steel::Val((steel::idx_get(req, steel::Val("component_id"))).to_string());
    steel::Val dest = steel::Val((steel::idx_get(req, steel::Val("install_path"))).to_string());
    steel::Val dpkg_chk = helper_check_dpkg_owner(dest);
    if ((steel::Val(!(steel::idx_get(dpkg_chk, steel::Val("ok"))).truthy())).truthy()) {
        return steel::make_map({{"exit_code", steel::Val(3.0)}, {"error", steel::idx_get(dpkg_chk, steel::Val("reason"))}});
    }
    steel::Val b_root = ((steel::has_key_val(req, steel::Val("backup_root"))).truthy() ? (steel::idx_get(req, steel::Val("backup_root"))) : (steel::Val("")));
    steel::Val tx_id = ((steel::has_key_val(req, steel::Val("tx_id"))).truthy() ? (steel::idx_get(req, steel::Val("tx_id"))) : (steel::Val("tx-current")));
    steel::Val b_path = helper_backup_path(b_root, tx_id, id);
    if ((steel::Val(op == steel::Val("remove"))).truthy()) {
        steel::Val b = helper_create_backup(dest, b_path);
        if ((steel::Val(!(steel::idx_get(b, steel::Val("ok"))).truthy())).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(1.0)}, {"error", ((steel::Val("failed to back up ") + dest) + steel::Val(" before remove"))}});
        }
        return steel::make_map({{"exit_code", steel::Val(0.0)}, {"error", steel::Val("")}, {"backup_path", steel::idx_get(b, steel::Val("backup_path"))}});
    }
    steel::Val archive = steel::Val((steel::idx_get(req, steel::Val("archive_path"))).to_string());
    if ((steel::Val((steel::Val((steel::has_key_val(req, steel::Val("sha256"))).truthy() && (steel::Val(steel::idx_get(req, steel::Val("sha256")) != steel::Val())).truthy())).truthy() && (steel::Val(steel::len_of(steel::Val((steel::idx_get(req, steel::Val("sha256"))).to_string())) > steel::Val(0.0))).truthy())).truthy()) {
        steel::Val actual = steel::sha256_file_val(archive);
        if ((steel::Val(steel::lower_str(actual) != steel::lower_str(steel::Val((steel::idx_get(req, steel::Val("sha256"))).to_string())))).truthy()) {
            return steel::make_map({{"exit_code", steel::Val(4.0)}, {"error", (((steel::Val("sha256 mismatch: expected ") + steel::Val((steel::idx_get(req, steel::Val("sha256"))).to_string())) + steel::Val(", got ")) + actual)}});
        }
    }
    steel::Val b = helper_create_backup(dest, b_path);
    if ((steel::Val(!(steel::idx_get(b, steel::Val("ok"))).truthy())).truthy()) {
        return steel::make_map({{"exit_code", steel::Val(1.0)}, {"error", (steel::Val("failed to create on-disk backup for ") + dest)}});
    }
    if ((steel::Val(!(steel::mkdir_val(dest)).truthy())).truthy()) {
        (void)(helper_restore_backup(dest, steel::idx_get(b, steel::Val("backup_path"))));
        return steel::make_map({{"exit_code", steel::Val(1.0)}, {"error", (steel::Val("failed to create destination dir ") + dest)}});
    }
    return steel::make_map({{"exit_code", steel::Val(0.0)}, {"error", steel::Val("")}, {"backup_path", steel::idx_get(b, steel::Val("backup_path"))}, {"extract", steel::make_map({{"archive", archive}, {"dest", dest}})}});
}
steel::Val st_fn_helper_execute_request(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return helper_execute_request((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val runner_atomic_write(steel::Val path, steel::Val content) {
    steel::Val tmp = (path + steel::Val(".tmp"));
    if ((steel::Val(!(steel::write_file_str(tmp, content)).truthy())).truthy()) {
        return steel::Val(false);
    }
    return steel::rename_val(tmp, path);
}
steel::Val st_fn_runner_atomic_write(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return runner_atomic_write((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val runner_cli_main() {
    steel::Val state_path = steel::env_val(steel::Val("INFINITY_STATE_PATH"));
    if ((steel::Val((steel::Val(state_path == steel::Val())).truthy() || (steel::Val(steel::len_of(steel::Val((state_path).to_string())) == steel::Val(0.0))).truthy())).truthy()) {
        state_path = steel::Val("infinity-state.json");
    }
    steel::Val cat_path = steel::env_val(steel::Val("INFINITY_CATALOGUE_PATH"));
    if ((steel::Val((steel::Val(cat_path == steel::Val())).truthy() || (steel::Val(steel::len_of(steel::Val((cat_path).to_string())) == steel::Val(0.0))).truthy())).truthy()) {
        cat_path = steel::Val("catalogue.json");
    }
    steel::Val state = db_load_from_path(state_path);
    steel::Val catalogue = steel::make_map({});
    if ((steel::file_exists_val(cat_path)).truthy()) {
        steel::Val raw_cat = steel::read_file_str(cat_path);
        if ((steel::Val(steel::len_of(steel::trim_str(raw_cat)) > steel::Val(0.0))).truthy()) {
            steel::Val parsed = steel::json_parse_val(raw_cat);
            if ((steel::Val((steel::Val(parsed != steel::Val())).truthy() && (steel::Val(steel::type_of(parsed) == steel::Val("map"))).truthy())).truthy()) {
                catalogue = parsed;
            }
        }
    }
    steel::Val argv = steel::args_val();
    steel::Val ts = steel::Val(std::trunc((steel::clock_val()).as_num()));
    steel::Val outcome = cli_dispatch(argv, state, catalogue, ts);
    if ((steel::Val(steel::len_of(steel::Val((steel::idx_get(outcome, steel::Val("output"))).to_string())) > steel::Val(0.0))).truthy()) {
        (void)(steel::print_vals({steel::idx_get(outcome, steel::Val("output"))}));
    }
    if ((steel::idx_get(outcome, steel::Val("save_state"))).truthy()) {
        if ((steel::Val(!(runner_atomic_write(state_path, steel::json_stringify_val(steel::idx_get(outcome, steel::Val("state"))))).truthy())).truthy()) {
            (void)(steel::print_vals({(steel::Val("Error saving state to ") + state_path)}));
            return steel::make_map({{"exit_code", steel::Val(1.0)}, {"actions", steel::make_arr({})}});
        }
    }
    return steel::make_map({{"exit_code", steel::idx_get(outcome, steel::Val("exit_code"))}, {"actions", steel::idx_get(outcome, steel::Val("actions"))}});
}
steel::Val st_fn_runner_cli_main(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return runner_cli_main(); }

steel::Val runner_publish_catalogue(steel::Val cat_path, steel::Val id, steel::Val entry_path) {
    if ((steel::Val(!(steel::file_exists_val(entry_path)).truthy())).truthy()) {
        return steel::make_map({{"exit_code", steel::Val(1.0)}, {"output", (steel::Val("Error: entry file not found: ") + entry_path)}});
    }
    steel::Val entry = steel::json_parse_val(steel::read_file_str(entry_path));
    steel::Val cat = steel::make_map({});
    if ((steel::file_exists_val(cat_path)).truthy()) {
        steel::Val raw = steel::read_file_str(cat_path);
        if ((steel::Val(steel::len_of(steel::trim_str(raw)) > steel::Val(0.0))).truthy()) {
            steel::Val parsed = steel::json_parse_val(raw);
            if ((steel::Val((steel::Val(parsed != steel::Val())).truthy() && (steel::Val(steel::type_of(parsed) == steel::Val("map"))).truthy())).truthy()) {
                cat = parsed;
            }
        }
    }
    steel::Val res = publish_catalogue_entry(cat, id, entry);
    if ((steel::Val(!(steel::idx_get(res, steel::Val("ok"))).truthy())).truthy()) {
        return steel::make_map({{"exit_code", steel::Val(1.0)}, {"output", (steel::Val("publish rejected: ") + steel::idx_get(res, steel::Val("error")))}});
    }
    if ((steel::Val(!(runner_atomic_write(cat_path, steel::json_stringify_val(steel::idx_get(res, steel::Val("catalogue"))))).truthy())).truthy()) {
        return steel::make_map({{"exit_code", steel::Val(1.0)}, {"output", (steel::Val("Error writing catalogue to ") + cat_path)}});
    }
    return steel::make_map({{"exit_code", steel::Val(0.0)}, {"output", (((steel::Val("Published ") + id) + steel::Val(" to ")) + cat_path)}});
}
steel::Val st_fn_runner_publish_catalogue(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return runner_publish_catalogue((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val test_sample_catalogue() {
    steel::Val sha_a = steel::Val("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    steel::Val sha_b = steel::Val("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    return steel::make_map({{"origin-core", steel::make_map({{"version", steel::Val("1.1.0")}, {"install_path", steel::Val("/usr/share/gnome-shell/extensions/Origin@appscreen")}, {"scope", steel::Val("system")}, {"description", steel::Val("Origin shell core")}, {"channel", steel::Val("stable")}, {"url", steel::Val("https://updates.infinityos.uk/origin-core-1.1.0.tar.xz")}, {"sha256", sha_a}, {"depends", steel::make_arr({})}})}, {"origin-cc", steel::make_map({{"version", steel::Val("1.0.0")}, {"install_path", steel::Val("/usr/share/gnome-shell/extensions/OriginUICC@quandz24.extension")}, {"scope", steel::Val("system")}, {"description", steel::Val("Origin control center")}, {"channel", steel::Val("stable")}, {"url", steel::Val("https://updates.infinityos.uk/origin-cc-1.0.0.tar.xz")}, {"sha256", sha_b}, {"depends", steel::make_arr({steel::make_map({{"id", steel::Val("origin-core")}, {"min_version", steel::Val(">=1.1.0")}})})}})}});
}
steel::Val st_fn_test_sample_catalogue(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return test_sample_catalogue(); }

steel::Val test_semver_suite() {
    (void)(steel::assert_val(steel::Val(compare_semver(steel::Val("1.2.3"), steel::Val("1.2.3")) == steel::Val(0.0)), steel::Val("semver eq")));
    (void)(steel::assert_val(steel::Val(compare_semver(steel::Val("1.2.3"), steel::Val("1.2.4")) == steel::Val(-1.0)), steel::Val("semver lt")));
    (void)(steel::assert_val(steel::Val(compare_semver(steel::Val("2.0.0"), steel::Val("1.9.9")) == steel::Val(1.0)), steel::Val("semver gt")));
    (void)(steel::assert_val(steel::Val(compare_semver(steel::Val("1.0.0-beta.2"), steel::Val("1.0.0")) == steel::Val(-1.0)), steel::Val("semver prerelease")));
    (void)(steel::assert_val(steel::Val(compare_semver(steel::Val("v1.0.0"), steel::Val("1.0.0")) == steel::Val(0.0)), steel::Val("semver v-prefix")));
    (void)(steel::assert_val(satisfies_constraint(steel::Val("1.4.0"), steel::Val("^1.2.0")), steel::Val("caret ok")));
    (void)(steel::assert_val(steel::Val(!(satisfies_constraint(steel::Val("2.0.0"), steel::Val("^1.2.0"))).truthy()), steel::Val("caret major reject")));
    (void)(steel::assert_val(satisfies_constraint(steel::Val("1.2.9"), steel::Val("~1.2.3")), steel::Val("tilde ok")));
    (void)(steel::assert_val(steel::Val(!(satisfies_constraint(steel::Val("1.3.0"), steel::Val("~1.2.3"))).truthy()), steel::Val("tilde minor reject")));
    (void)(steel::assert_val(steel::Val(classify_transition(steel::Val(""), steel::Val("1.0.0")) == steel::Val("install")), steel::Val("classify install")));
    (void)(steel::assert_val(steel::Val(classify_transition(steel::Val("1.0.0"), steel::Val("1.1.0")) == steel::Val("upgrade")), steel::Val("classify upgrade")));
    (void)(steel::assert_val(steel::Val(classify_transition(steel::Val("1.1.0"), steel::Val("1.0.0")) == steel::Val("downgrade")), steel::Val("classify downgrade")));
    (void)(steel::assert_val(steel::Val(classify_transition(steel::Val("1.0.0"), steel::Val("1.0.0")) == steel::Val("noop")), steel::Val("classify noop")));
    return steel::Val(true);
}
steel::Val st_fn_test_semver_suite(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return test_semver_suite(); }

steel::Val test_security_policy_suite() {
    steel::Val ok = validate_install_path(steel::Val("/usr/share/icons/Radience"), steel::Val("system"));
    (void)(steel::assert_val(steel::idx_get(ok, steel::Val("ok")), steel::Val("valid system icon path")));
    steel::Val bad_etc = validate_install_path(steel::Val("/etc/shadow"), steel::Val("system"));
    (void)(steel::assert_val(steel::Val(!(steel::idx_get(bad_etc, steel::Val("ok"))).truthy()), steel::Val("block /etc")));
    steel::Val bad_trav = validate_install_path(steel::Val("/usr/share/icons/../../etc/passwd"), steel::Val("system"));
    (void)(steel::assert_val(steel::Val(!(steel::idx_get(bad_trav, steel::Val("ok"))).truthy()), steel::Val("block traversal")));
    steel::Val bad_slash = validate_install_path(steel::Val("/usr/share//icons/Radience"), steel::Val("system"));
    (void)(steel::assert_val(steel::Val(!(steel::idx_get(bad_slash, steel::Val("ok"))).truthy()), steel::Val("block duplicate slash")));
    steel::Val user_ok = validate_install_path(steel::Val("~/.local/share/icons/Radience"), steel::Val("user"));
    (void)(steel::assert_val(steel::idx_get(user_ok, steel::Val("ok")), steel::Val("valid user path")));
    steel::Val zip_ok = validate_archive_manifest(steel::make_arr({steel::Val("usr/file.txt"), steel::Val("meta/info.json")}));
    (void)(steel::assert_val(steel::idx_get(zip_ok, steel::Val("ok")), steel::Val("valid archive entries")));
    steel::Val zip_bad = validate_archive_manifest(steel::make_arr({steel::Val("ok/file.txt"), steel::Val("../escape")}));
    (void)(steel::assert_val(steel::Val(!(steel::idx_get(zip_bad, steel::Val("ok"))).truthy()), steel::Val("block zip-slip")));
    (void)(steel::assert_val(validate_sha256(steel::sha256_val(steel::Val("infinityos"))), steel::Val("valid sha256 output")));
    return steel::Val(true);
}
steel::Val st_fn_test_security_policy_suite(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return test_security_policy_suite(); }

steel::Val test_solver_and_reconciler_suite() {
    steel::Val cat = test_sample_catalogue();
    steel::Val cat_chk = validate_catalogue(cat);
    (void)(steel::assert_val(steel::Val((steel::idx_get(cat_chk, steel::Val("ok"))).truthy() && (steel::Val(steel::idx_get(cat_chk, steel::Val("count")) == steel::Val(2.0))).truthy()), steel::Val("catalogue valid")));
    steel::Val plan = plan_reconcile(steel::make_map({}), cat, steel::make_arr({steel::Val("origin-cc")}));
    (void)(steel::assert_val(steel::idx_get(plan, steel::Val("ok")), steel::Val("reconcile plan ok")));
    (void)(steel::assert_val(steel::Val(steel::len_of(steel::idx_get(plan, steel::Val("actions"))) == steel::Val(2.0)), steel::Val("reconcile installs dep first")));
    (void)(steel::assert_val(steel::Val(steel::idx_get(steel::idx_get(steel::idx_get(plan, steel::Val("actions")), steel::Val(0.0)), steel::Val("id")) == steel::Val("origin-core")), steel::Val("dep ordered first")));
    (void)(steel::assert_val(steel::Val(steel::idx_get(steel::idx_get(steel::idx_get(plan, steel::Val("actions")), steel::Val(1.0)), steel::Val("id")) == steel::Val("origin-cc")), steel::Val("target ordered second")));
    steel::Val cyclic = steel::make_map({{"a", steel::make_map({{"version", steel::Val("1.0.0")}, {"install_path", steel::Val("/usr/share/icons/A")}, {"scope", steel::Val("system")}, {"depends", steel::make_arr({steel::make_map({{"id", steel::Val("b")}})})}})}, {"b", steel::make_map({{"version", steel::Val("1.0.0")}, {"install_path", steel::Val("/usr/share/icons/B")}, {"scope", steel::Val("system")}, {"depends", steel::make_arr({steel::make_map({{"id", steel::Val("a")}})})}})}});
    steel::Val cyc_res = resolve_install_order(cyclic, steel::make_arr({steel::Val("a")}));
    (void)(steel::assert_val(steel::Val((steel::Val(!(steel::idx_get(cyc_res, steel::Val("ok"))).truthy())).truthy() && (steel::contains_val(steel::idx_get(cyc_res, steel::Val("error")), steel::Val("cycle"))).truthy()), steel::Val("cycle detected")));
    steel::Val installed = steel::make_map({{"origin-core", steel::Val("1.1.0")}, {"origin-cc", steel::Val("1.0.0")}});
    steel::Val snap = steel::make_map({{"origin-core", steel::Val("1.0.0")}});
    steel::Val sp = plan_system_snapshot_transition(installed, cat, snap);
    (void)(steel::assert_val(steel::Val((steel::idx_get(sp, steel::Val("ok"))).truthy() && (steel::Val(steel::len_of(steel::idx_get(sp, steel::Val("actions"))) == steel::Val(2.0))).truthy()), steel::Val("snapshot transition count")));
    (void)(steel::assert_val(steel::Val((steel::Val(steel::idx_get(steel::idx_get(steel::idx_get(sp, steel::Val("actions")), steel::Val(0.0)), steel::Val("id")) == steel::Val("origin-cc"))).truthy() && (steel::Val(steel::idx_get(steel::idx_get(steel::idx_get(sp, steel::Val("actions")), steel::Val(0.0)), steel::Val("action")) == steel::Val("remove"))).truthy()), steel::Val("snapshot removes newer")));
    (void)(steel::assert_val(steel::Val((steel::Val(steel::idx_get(steel::idx_get(steel::idx_get(sp, steel::Val("actions")), steel::Val(1.0)), steel::Val("id")) == steel::Val("origin-core"))).truthy() && (steel::Val(steel::idx_get(steel::idx_get(steel::idx_get(sp, steel::Val("actions")), steel::Val(1.0)), steel::Val("action")) == steel::Val("downgrade"))).truthy()), steel::Val("snapshot downgrades")));
    return steel::Val(true);
}
steel::Val st_fn_test_solver_and_reconciler_suite(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return test_solver_and_reconciler_suite(); }

steel::Val test_cli_db_and_ui_suite() {
    steel::Val cat = test_sample_catalogue();
    steel::Val st = db_default_state();
    steel::Val res = cli_dispatch(steel::make_arr({steel::Val("install"), steel::Val("origin-cc")}), st, cat, steel::Val(1700000000.0));
    (void)(steel::assert_val(steel::Val((steel::Val((steel::Val(steel::idx_get(res, steel::Val("exit_code")) == steel::Val(0.0))).truthy() && (steel::Val(steel::len_of(steel::idx_get(res, steel::Val("actions"))) == steel::Val(2.0))).truthy())).truthy() && (steel::idx_get(res, steel::Val("save_state"))).truthy()), steel::Val("cli install")));
    steel::Val rb = cli_dispatch(steel::make_arr({steel::Val("rollback")}), steel::idx_get(res, steel::Val("state")), cat, steel::Val(1700000010.0));
    (void)(steel::assert_val(steel::Val(steel::idx_get(rb, steel::Val("exit_code")) == steel::Val(0.0)), steel::Val("cli rollback")));
    steel::Val list_after = cli_dispatch(steel::make_arr({steel::Val("list"), steel::Val("--json")}), steel::idx_get(rb, steel::Val("state")), cat, steel::Val(1700000020.0));
    (void)(steel::assert_val(steel::Val(steel::idx_get(list_after, steel::Val("output")) == steel::Val("{}")), steel::Val("rollback cleared installed")));
    steel::Val ui0 = ui_initial_state();
    steel::Val ui1 = ui_reduce(ui0, steel::make_map({{"type", steel::Val("toggle_select")}, {"id", steel::Val("origin-cc")}}), db_default_state(), cat);
    steel::Val ui2 = ui_reduce(ui1, steel::make_map({{"type", steel::Val("preview_plan")}}), db_default_state(), cat);
    (void)(steel::assert_val(steel::Val(steel::idx_get(ui2, steel::Val("banner_level")) == steel::Val("success")), steel::Val("ui preview plan success")));
    return steel::Val(true);
}
steel::Val st_fn_test_cli_db_and_ui_suite(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return test_cli_db_and_ui_suite(); }

steel::Val test_helper_policy_suite() {
    steel::Val bad = helper_execute_request(steel::json_stringify_val(steel::make_map({{"op", steel::Val("install")}, {"component_id", steel::Val("origin-core")}, {"scope", steel::Val("system")}, {"install_path", steel::Val("/etc/passwd")}, {"archive_path", steel::Val("x.tar.xz")}})));
    (void)(steel::assert_val(steel::Val(steel::idx_get(bad, steel::Val("exit_code")) == steel::Val(2.0)), steel::Val("helper rejects /etc/passwd")));
    return steel::Val(true);
}
steel::Val st_fn_test_helper_policy_suite(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return test_helper_policy_suite(); }

steel::Val run_all_steel_self_tests() {
    (void)(steel::assert_val(test_semver_suite(), steel::Val("semver suite")));
    (void)(steel::assert_val(test_security_policy_suite(), steel::Val("security suite")));
    (void)(steel::assert_val(test_solver_and_reconciler_suite(), steel::Val("solver suite")));
    (void)(steel::assert_val(test_cli_db_and_ui_suite(), steel::Val("cli/db/ui suite")));
    (void)(steel::assert_val(test_helper_policy_suite(), steel::Val("helper suite")));
    return steel::Val(true);
}
steel::Val st_fn_run_all_steel_self_tests(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return run_all_steel_self_tests(); }

steel::Val steel_compare_semver(steel::Val a, steel::Val b) {
    return compare_semver(a, b);
}
steel::Val st_fn_steel_compare_semver(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_compare_semver((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val steel_satisfies_constraint(steel::Val ver, steel::Val constraint) {
    return satisfies_constraint(ver, constraint);
}
steel::Val st_fn_steel_satisfies_constraint(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_satisfies_constraint((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val steel_validate_path_json(steel::Val path, steel::Val scope) {
    return steel::json_stringify_val(validate_install_path(path, scope));
}
steel::Val st_fn_steel_validate_path_json(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_validate_path_json((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val())); }

steel::Val steel_validate_archive_json(steel::Val entries_json) {
    steel::Val entries = steel::json_parse_val(entries_json);
    return steel::json_stringify_val(validate_archive_manifest(entries));
}
steel::Val st_fn_steel_validate_archive_json(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_validate_archive_json((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val steel_validate_catalogue_json(steel::Val catalogue_json) {
    steel::Val cat = steel::json_parse_val(catalogue_json);
    return steel::json_stringify_val(validate_catalogue(cat));
}
steel::Val st_fn_steel_validate_catalogue_json(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_validate_catalogue_json((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val steel_plan_reconcile(steel::Val installed_json, steel::Val catalogue_json, steel::Val targets_json) {
    steel::Val installed = steel::json_parse_val(installed_json);
    steel::Val catalogue = steel::json_parse_val(catalogue_json);
    steel::Val targets = steel::json_parse_val(targets_json);
    return steel::json_stringify_val(plan_reconcile(installed, catalogue, targets));
}
steel::Val st_fn_steel_plan_reconcile(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_plan_reconcile((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val steel_plan_remove(steel::Val installed_json, steel::Val catalogue_json, steel::Val target_id, steel::Val force) {
    steel::Val installed = steel::json_parse_val(installed_json);
    steel::Val catalogue = steel::json_parse_val(catalogue_json);
    return steel::json_stringify_val(plan_remove(installed, catalogue, target_id, force));
}
steel::Val st_fn_steel_plan_remove(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_plan_remove((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val()), (st_args.size() > 3 ? st_args[3] : steel::Val())); }

steel::Val steel_cli_dispatch(steel::Val args_json, steel::Val state_json, steel::Val catalogue_json, steel::Val timestamp) {
    steel::Val args = steel::json_parse_val(args_json);
    steel::Val state = ((steel::Val(steel::len_of(steel::trim_str(state_json)) > steel::Val(0.0))).truthy() ? (steel::json_parse_val(state_json)) : (db_default_state()));
    steel::Val catalogue = ((steel::Val(steel::len_of(steel::trim_str(catalogue_json)) > steel::Val(0.0))).truthy() ? (steel::json_parse_val(catalogue_json)) : (steel::make_map({})));
    return steel::json_stringify_val(cli_dispatch(args, state, catalogue, timestamp));
}
steel::Val st_fn_steel_cli_dispatch(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_cli_dispatch((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val()), (st_args.size() > 3 ? st_args[3] : steel::Val())); }

steel::Val steel_ui_reduce(steel::Val ui_json, steel::Val event_json, steel::Val state_json, steel::Val catalogue_json) {
    steel::Val ui = ((steel::Val(steel::len_of(steel::trim_str(ui_json)) > steel::Val(0.0))).truthy() ? (steel::json_parse_val(ui_json)) : (ui_initial_state()));
    steel::Val ev = steel::json_parse_val(event_json);
    steel::Val st = ((steel::Val(steel::len_of(steel::trim_str(state_json)) > steel::Val(0.0))).truthy() ? (steel::json_parse_val(state_json)) : (db_default_state()));
    steel::Val cat = ((steel::Val(steel::len_of(steel::trim_str(catalogue_json)) > steel::Val(0.0))).truthy() ? (steel::json_parse_val(catalogue_json)) : (steel::make_map({})));
    return steel::json_stringify_val(ui_reduce(ui, ev, st, cat));
}
steel::Val st_fn_steel_ui_reduce(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_ui_reduce((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val()), (st_args.size() > 3 ? st_args[3] : steel::Val())); }

steel::Val steel_build_manifest_json(steel::Val id, steel::Val version, steel::Val scope, steel::Val install_path, steel::Val sha256, steel::Val signature, steel::Val files_json) {
    steel::Val files = steel::json_parse_val(files_json);
    return steel::json_stringify_val(build_package_manifest(id, version, scope, install_path, sha256, signature, files));
}
steel::Val st_fn_steel_build_manifest_json(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_build_manifest_json((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val()), (st_args.size() > 3 ? st_args[3] : steel::Val()), (st_args.size() > 4 ? st_args[4] : steel::Val()), (st_args.size() > 5 ? st_args[5] : steel::Val()), (st_args.size() > 6 ? st_args[6] : steel::Val())); }

steel::Val steel_publish_entry_json(steel::Val catalogue_json, steel::Val id, steel::Val entry_json) {
    steel::Val cat = ((steel::Val(steel::len_of(steel::trim_str(catalogue_json)) > steel::Val(0.0))).truthy() ? (steel::json_parse_val(catalogue_json)) : (steel::make_map({})));
    steel::Val entry = steel::json_parse_val(entry_json);
    return steel::json_stringify_val(publish_catalogue_entry(cat, id, entry));
}
steel::Val st_fn_steel_publish_entry_json(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_publish_entry_json((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val steel_validate_helper_request_json(steel::Val req_json) {
    steel::Val req = steel::json_parse_val(req_json);
    return steel::json_stringify_val(validate_helper_request(req));
}
steel::Val st_fn_steel_validate_helper_request_json(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_validate_helper_request_json((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val steel_helper_run_json(steel::Val req_json) {
    return steel::json_stringify_val(helper_execute_request(req_json));
}
steel::Val st_fn_steel_helper_run_json(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_helper_run_json((st_args.size() > 0 ? st_args[0] : steel::Val())); }

steel::Val steel_runner_cli_json() {
    return steel::json_stringify_val(runner_cli_main());
}
steel::Val st_fn_steel_runner_cli_json(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_runner_cli_json(); }

steel::Val steel_runner_publish_json(steel::Val cat_path, steel::Val id, steel::Val entry_path) {
    return steel::json_stringify_val(runner_publish_catalogue(cat_path, id, entry_path));
}
steel::Val st_fn_steel_runner_publish_json(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_runner_publish_json((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val steel_plan_snapshot_json(steel::Val installed_json, steel::Val catalogue_json, steel::Val snapshot_json) {
    steel::Val installed = steel::json_parse_val(installed_json);
    steel::Val catalogue = steel::json_parse_val(catalogue_json);
    steel::Val snapshot = steel::json_parse_val(snapshot_json);
    return steel::json_stringify_val(plan_system_snapshot_transition(installed, catalogue, snapshot));
}
steel::Val st_fn_steel_plan_snapshot_json(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_plan_snapshot_json((st_args.size() > 0 ? st_args[0] : steel::Val()), (st_args.size() > 1 ? st_args[1] : steel::Val()), (st_args.size() > 2 ? st_args[2] : steel::Val())); }

steel::Val steel_run_self_tests() {
    return run_all_steel_self_tests();
}
steel::Val st_fn_steel_run_self_tests(steel::Val, const std::vector<steel::Val>& st_args) { (void)st_args; return steel_run_self_tests(); }

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) steel::cli_args_store().emplace_back(argv[i]);
    return 0;
}
