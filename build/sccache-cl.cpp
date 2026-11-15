// Native Win32 compiler wrapper for MSVC + sccache + MSBuild FileTracker (D00 T04 s7).
// Invokes sccache ahead of the resolved target cl.exe, propagates stdout/stderr and
// exit status unchanged, and on exit 0 replays FileTracker reads/writes across @rsp
// files, sources, /sourceDependencies JSON, and /Fo objects so cache hits keep
// MSBuild CL.read.*.tlog and CL.write.*.tlog dependencies exact.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct HandleGuard {
    HANDLE h = INVALID_HANDLE_VALUE;
    HandleGuard() = default;
    explicit HandleGuard(HANDLE handle) : h(handle) {}
    ~HandleGuard() {
        if (h != INVALID_HANDLE_VALUE && h != nullptr) {
            CloseHandle(h);
        }
    }
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;
    HandleGuard(HandleGuard&& other) noexcept : h(other.h) {
        other.h = INVALID_HANDLE_VALUE;
    }
    HandleGuard& operator=(HandleGuard&& other) noexcept {
        if (this != &other) {
            if (h != INVALID_HANDLE_VALUE && h != nullptr) {
                CloseHandle(h);
            }
            h = other.h;
            other.h = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    [[nodiscard]] bool valid() const noexcept {
        return h != INVALID_HANDLE_VALUE && h != nullptr;
    }
};

std::wstring format_win32_error(DWORD code) {
    wchar_t* buf = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD len = FormatMessageW(flags, nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                     reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring msg;
    if (len > 0 && buf != nullptr) {
        msg.assign(buf, len);
        while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n')) {
            msg.pop_back();
        }
    } else {
        msg = L"Win32 error " + std::to_wstring(code);
    }
    if (buf != nullptr) {
        LocalFree(buf);
    }
    return msg;
}

std::wstring get_env_var(const wchar_t* name) {
    DWORD needed = GetEnvironmentVariableW(name, nullptr, 0);
    if (needed == 0) {
        return {};
    }
    std::wstring value(needed, L'\0');
    DWORD written = GetEnvironmentVariableW(name, value.data(), needed);
    if (written == 0 || written >= needed) {
        return {};
    }
    value.resize(written);
    return value;
}

bool is_existing_file(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool is_existing_dir(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring to_full_path(const std::wstring& path) {
    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0) {
        return path;
    }
    std::wstring out(needed, L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), needed, out.data(), nullptr);
    if (written == 0 || written >= needed) {
        return path;
    }
    out.resize(written);
    return out;
}

std::wstring to_lower_ascii(std::wstring_view s) {
    std::wstring out(s);
    for (wchar_t& c : out) {
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    return out;
}

struct FileIdentity {
    bool valid = false;
    DWORD volume = 0;
    DWORD index_high = 0;
    DWORD index_low = 0;
};

FileIdentity query_file_identity(const std::wstring& path) {
    FileIdentity id;
    HandleGuard h(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.valid()) {
        return id;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileInformationByHandle(h.h, &info) != FALSE) {
        id.valid = true;
        id.volume = info.dwVolumeSerialNumber;
        id.index_high = info.nFileIndexHigh;
        id.index_low = info.nFileIndexLow;
    }
    return id;
}

bool same_file(const std::wstring& a_full, const FileIdentity& a_id, const std::wstring& b_path) {
    const std::wstring b_full = to_full_path(b_path);
    if (_wcsicmp(a_full.c_str(), b_full.c_str()) == 0) {
        return true;
    }
    if (!a_id.valid) {
        return false;
    }
    const FileIdentity b_id = query_file_identity(b_full);
    return b_id.valid && a_id.volume == b_id.volume &&
           a_id.index_high == b_id.index_high && a_id.index_low == b_id.index_low;
}

std::wstring get_self_exe_path() {
    std::vector<wchar_t> buf(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            return {};
        }
        if (n < buf.size() - 1) {
            return to_full_path(std::wstring(buf.data(), n));
        }
        if (buf.size() >= 32768) {
            return {};
        }
        buf.resize(buf.size() * 2, L'\0');
    }
}

bool resolve_real_cl(std::wstring& out_cl, std::wstring& out_err) {
    const std::wstring override_cl = get_env_var(L"RECTO_REAL_CL_EXE");
    if (!override_cl.empty()) {
        if (!is_existing_file(override_cl)) {
            out_err = L"RECTO_REAL_CL_EXE points to a missing file: " + override_cl;
            return false;
        }
        out_cl = to_full_path(override_cl);
        return true;
    }

    const std::wstring self_full = get_self_exe_path();
    const FileIdentity self_id = self_full.empty() ? FileIdentity{} : query_file_identity(self_full);
    const std::wstring path_env = get_env_var(L"PATH");
    if (path_env.empty()) {
        out_err = L"PATH is empty; cannot locate real cl.exe";
        return false;
    }

    size_t pos = 0;
    while (pos <= path_env.size()) {
        const size_t sep = path_env.find(L';', pos);
        const size_t end = (sep == std::wstring::npos) ? path_env.size() : sep;
        std::wstring entry = path_env.substr(pos, end - pos);
        pos = (sep == std::wstring::npos) ? path_env.size() + 1 : sep + 1;

        while (!entry.empty() && (entry.front() == L' ' || entry.front() == L'\t')) {
            entry.erase(entry.begin());
        }
        while (!entry.empty() && (entry.back() == L' ' || entry.back() == L'\t')) {
            entry.pop_back();
        }
        if (entry.size() >= 2 && entry.front() == L'"' && entry.back() == L'"') {
            entry = entry.substr(1, entry.size() - 2);
        }
        if (entry.empty()) {
            continue;
        }
        if (entry.back() != L'\\' && entry.back() != L'/') {
            entry.push_back(L'\\');
        }
        const std::wstring candidate = entry + L"cl.exe";
        if (!is_existing_file(candidate)) {
            continue;
        }
        if (!self_full.empty() && same_file(self_full, self_id, candidate)) {
            continue;
        }
        out_cl = to_full_path(candidate);
        return true;
    }

    out_err = L"could not locate real cl.exe on PATH (excluding wrapper " + self_full + L")";
    return false;
}

std::wstring_view extract_command_tail(const wchar_t* cmd_line) {
    if (cmd_line == nullptr) {
        return {};
    }
    const wchar_t* p = cmd_line;
    while (*p == L' ' || *p == L'\t') {
        ++p;
    }
    if (*p == L'"') {
        ++p;
        while (*p != L'\0' && *p != L'"') {
            ++p;
        }
        if (*p == L'"') {
            ++p;
        }
    } else {
        while (*p != L'\0' && *p != L' ' && *p != L'\t') {
            ++p;
        }
    }
    while (*p == L' ' || *p == L'\t') {
        ++p;
    }
    return std::wstring_view(p);
}

// Splits a Windows command or response-file string using Microsoft C runtime rules
// (identical semantics to scripts/check-ui-boundary.py::command_line_tokens).
std::vector<std::wstring> tokenize_crt(std::wstring_view text) {
    std::vector<std::wstring> tokens;
    std::wstring current;
    bool quoted = false;
    bool started = false;
    size_t i = 0;

    while (i < text.size()) {
        const wchar_t c = text[i];
        if (c == L'\\') {
            size_t j = i;
            while (j < text.size() && text[j] == L'\\') {
                ++j;
            }
            const size_t backslashes = j - i;
            if (j < text.size() && text[j] == L'"') {
                current.append(backslashes / 2, L'\\');
                if ((backslashes % 2) != 0) {
                    current.push_back(L'"');
                    ++j;
                }
            } else {
                current.append(backslashes, L'\\');
            }
            i = j;
            started = true;
        } else if (c == L'"') {
            if (quoted && i + 1 < text.size() && text[i + 1] == L'"') {
                current.push_back(L'"');
                i += 2;
            } else {
                quoted = !quoted;
                ++i;
            }
            started = true;
        } else if ((c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') && !quoted) {
            if (started) {
                tokens.push_back(std::move(current));
                current.clear();
                started = false;
            }
            ++i;
        } else {
            current.push_back(c);
            started = true;
            ++i;
        }
    }
    if (started) {
        tokens.push_back(std::move(current));
    }
    return tokens;
}

bool read_all_bytes(const std::wstring& path, std::vector<unsigned char>& out_bytes, std::wstring& out_err) {
    HandleGuard h(CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.valid()) {
        const DWORD code = GetLastError();
        out_err = L"failed to open '" + path + L"' for reading: " + format_win32_error(code);
        return false;
    }

    LARGE_INTEGER size{};
    if (GetFileSizeEx(h.h, &size) == FALSE) {
        const DWORD code = GetLastError();
        out_err = L"failed to query size of '" + path + L"': " + format_win32_error(code);
        return false;
    }
    if (size.QuadPart < 0 || size.QuadPart > 64LL * 1024LL * 1024LL) {
        out_err = L"file too large to read for tracking: '" + path + L"'";
        return false;
    }

    out_bytes.resize(static_cast<size_t>(size.QuadPart));
    size_t total = 0;
    while (total < out_bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<size_t>(out_bytes.size() - total, 1024U * 1024U));
        DWORD read_now = 0;
        if (ReadFile(h.h, out_bytes.data() + total, chunk, &read_now, nullptr) == FALSE) {
            const DWORD code = GetLastError();
            out_err = L"failed to read '" + path + L"': " + format_win32_error(code);
            return false;
        }
        if (read_now == 0) {
            break;
        }
        total += read_now;
    }
    out_bytes.resize(total);
    return true;
}

bool decode_text_bytes(const std::vector<unsigned char>& bytes, std::wstring& out_text, std::wstring& out_err) {
    if (bytes.empty()) {
        out_text.clear();
        return true;
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        const size_t wchar_count = (bytes.size() - 2) / 2;
        out_text.resize(wchar_count);
        if (wchar_count > 0) {
            std::memcpy(out_text.data(), bytes.data() + 2, wchar_count * sizeof(wchar_t));
        }
        return true;
    }

    size_t offset = 0;
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) {
        offset = 3;
    }
    const char* ptr = reinterpret_cast<const char*>(bytes.data() + offset);
    const int len = static_cast<int>(bytes.size() - offset);
    if (len <= 0) {
        out_text.clear();
        return true;
    }

    UINT cp = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int needed = MultiByteToWideChar(cp, flags, ptr, len, nullptr, 0);
    if (needed == 0 && offset == 0) {
        cp = CP_ACP;
        flags = 0;
        needed = MultiByteToWideChar(cp, flags, ptr, len, nullptr, 0);
    }
    if (needed <= 0) {
        out_err = L"failed to decode response file text: " + format_win32_error(GetLastError());
        return false;
    }
    out_text.resize(static_cast<size_t>(needed));
    if (MultiByteToWideChar(cp, flags, ptr, len, out_text.data(), needed) <= 0) {
        out_err = L"failed to convert response file text: " + format_win32_error(GetLastError());
        return false;
    }
    return true;
}

bool expand_tokens_recursive(const std::vector<std::wstring>& raw_tokens,
                             int depth,
                             std::vector<std::wstring>& out_expanded,
                             std::vector<std::wstring>& out_rsp_files,
                             std::wstring& out_err) {
    if (depth > 32) {
        out_err = L"response file nesting exceeded maximum depth (32)";
        return false;
    }
    for (const std::wstring& tok : raw_tokens) {
        if (tok.size() > 1 && tok[0] == L'@') {
            const std::wstring rsp_path = tok.substr(1);
            out_rsp_files.push_back(rsp_path);
            std::vector<unsigned char> bytes;
            if (!read_all_bytes(rsp_path, bytes, out_err)) {
                return false;
            }
            std::wstring decoded;
            if (!decode_text_bytes(bytes, decoded, out_err)) {
                return false;
            }
            const std::vector<std::wstring> inner = tokenize_crt(decoded);
            if (!expand_tokens_recursive(inner, depth + 1, out_expanded, out_rsp_files, out_err)) {
                return false;
            }
        } else {
            out_expanded.push_back(tok);
        }
    }
    return true;
}

bool touch_read(const std::wstring& path, std::wstring& out_err) {
    HandleGuard h(CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.valid()) {
        out_err = L"failed to record read access on '" + path + L"': " + format_win32_error(GetLastError());
        return false;
    }
    unsigned char byte = 0;
    DWORD got = 0;
    (void)ReadFile(h.h, &byte, 1, &got, nullptr);
    return true;
}

bool touch_write(const std::wstring& path, std::wstring& out_err) {
    HandleGuard h(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.valid()) {
        out_err = L"failed to record write access on expected output '" + path + L"': " + format_win32_error(GetLastError());
        return false;
    }
    if (SetFilePointerEx(h.h, LARGE_INTEGER{}, nullptr, FILE_END) != FALSE) {
        DWORD written = 0;
        const char dummy = 0;
        if (WriteFile(h.h, &dummy, 0, &written, nullptr) == FALSE) {
            out_err = L"failed to mark write access on '" + path + L"': " + format_win32_error(GetLastError());
            return false;
        }
    }
    return true;
}

bool utf8_to_wide(std::string_view utf8, std::wstring& out_wide, std::wstring& out_err) {
    if (utf8.empty()) {
        out_wide.clear();
        return true;
    }
    const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        out_err = L"invalid UTF-8 string in /sourceDependencies JSON";
        return false;
    }
    out_wide.resize(static_cast<size_t>(needed));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                            utf8.data(), static_cast<int>(utf8.size()), out_wide.data(), needed) <= 0) {
        out_err = L"UTF-8 conversion failed in /sourceDependencies JSON";
        return false;
    }
    return true;
}

void append_utf8_codepoint(uint32_t cp, std::string& out) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0U | ((cp >> 6) & 0x1FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0U | ((cp >> 12) & 0x0FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | ((cp >> 18) & 0x07U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
}

// Strict RFC 8259 parser tailored to MSVC /sourceDependencies JSON reports.
// Rejects missing/malformed syntax, trailing tokens, non-object roots, and
// non-string or malformed dependency fields.
struct SourceDepsJsonParser {
    std::string_view s;
    size_t pos = 0;
    std::wstring err;

    void skip_ws() {
        while (pos < s.size()) {
            const unsigned char c = static_cast<unsigned char>(s[pos]);
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++pos;
            } else {
                break;
            }
        }
    }

    bool parse_hex4(uint32_t& out) {
        if (pos + 4 > s.size()) {
            err = L"truncated \\u escape in JSON";
            return false;
        }
        uint32_t v = 0;
        for (size_t k = 0; k < 4; ++k) {
            const char c = s[pos + k];
            uint32_t d = 0;
            if (c >= '0' && c <= '9') {
                d = static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                d = static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                d = static_cast<uint32_t>(c - 'A' + 10);
            } else {
                err = L"invalid hex digit in \\u escape in JSON";
                return false;
            }
            v = (v << 4) | d;
        }
        pos += 4;
        out = v;
        return true;
    }

    bool parse_string(std::string& out) {
        skip_ws();
        if (pos >= s.size() || s[pos] != '"') {
            err = L"expected JSON string";
            return false;
        }
        ++pos;
        out.clear();
        while (pos < s.size()) {
            const unsigned char ch = static_cast<unsigned char>(s[pos++]);
            if (ch == '"') {
                return true;
            }
            if (ch < 0x20) {
                err = L"unescaped control character inside JSON string";
                return false;
            }
            if (ch == '\\') {
                if (pos >= s.size()) {
                    err = L"unterminated escape in JSON string";
                    return false;
                }
                const char esc = s[pos++];
                switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t u1 = 0;
                    if (!parse_hex4(u1)) {
                        return false;
                    }
                    if (u1 >= 0xD800U && u1 <= 0xDBFFU) {
                        if (pos + 2 > s.size() || s[pos] != '\\' || s[pos + 1] != 'u') {
                            err = L"unpaired high surrogate in JSON string";
                            return false;
                        }
                        pos += 2;
                        uint32_t u2 = 0;
                        if (!parse_hex4(u2)) {
                            return false;
                        }
                        if (u2 < 0xDC00U || u2 > 0xDFFFU) {
                            err = L"invalid low surrogate in JSON string";
                            return false;
                        }
                        const uint32_t cp = 0x10000U + (((u1 - 0xD800U) << 10) | (u2 - 0xDC00U));
                        append_utf8_codepoint(cp, out);
                    } else if (u1 >= 0xDC00U && u1 <= 0xDFFFU) {
                        err = L"unexpected low surrogate in JSON string";
                        return false;
                    } else {
                        append_utf8_codepoint(u1, out);
                    }
                    break;
                }
                default:
                    err = L"invalid JSON escape sequence";
                    return false;
                }
            } else {
                out.push_back(static_cast<char>(ch));
            }
        }
        err = L"unterminated JSON string";
        return false;
    }

    bool skip_number() {
        const size_t start = pos;
        if (pos < s.size() && s[pos] == '-') {
            ++pos;
        }
        if (pos >= s.size()) {
            err = L"invalid JSON number";
            return false;
        }
        if (s[pos] == '0') {
            ++pos;
        } else if (s[pos] >= '1' && s[pos] <= '9') {
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
                ++pos;
            }
        } else {
            err = L"invalid JSON number";
            return false;
        }
        if (pos < s.size() && s[pos] == '.') {
            ++pos;
            if (pos >= s.size() || s[pos] < '0' || s[pos] > '9') {
                err = L"invalid JSON fraction";
                return false;
            }
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
                ++pos;
            }
        }
        if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
            if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
                ++pos;
            }
            if (pos >= s.size() || s[pos] < '0' || s[pos] > '9') {
                err = L"invalid JSON exponent";
                return false;
            }
            while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
                ++pos;
            }
        }
        return pos > start;
    }

    bool skip_value(int depth) {
        if (depth > 64) {
            err = L"JSON nesting depth limit exceeded";
            return false;
        }
        skip_ws();
        if (pos >= s.size()) {
            err = L"unexpected end of JSON";
            return false;
        }
        const char c = s[pos];
        if (c == '"') {
            std::string dummy;
            return parse_string(dummy);
        }
        if (c == '{') {
            ++pos;
            skip_ws();
            if (pos < s.size() && s[pos] == '}') {
                ++pos;
                return true;
            }
            for (;;) {
                std::string key;
                if (!parse_string(key)) {
                    return false;
                }
                skip_ws();
                if (pos >= s.size() || s[pos] != ':') {
                    err = L"expected ':' in JSON object";
                    return false;
                }
                ++pos;
                if (!skip_value(depth + 1)) {
                    return false;
                }
                skip_ws();
                if (pos < s.size() && s[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (pos < s.size() && s[pos] == '}') {
                    ++pos;
                    return true;
                }
                err = L"expected ',' or '}' in JSON object";
                return false;
            }
        }
        if (c == '[') {
            ++pos;
            skip_ws();
            if (pos < s.size() && s[pos] == ']') {
                ++pos;
                return true;
            }
            for (;;) {
                if (!skip_value(depth + 1)) {
                    return false;
                }
                skip_ws();
                if (pos < s.size() && s[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (pos < s.size() && s[pos] == ']') {
                    ++pos;
                    return true;
                }
                err = L"expected ',' or ']' in JSON array";
                return false;
            }
        }
        if (s.substr(pos).starts_with("true")) {
            pos += 4;
            return true;
        }
        if (s.substr(pos).starts_with("false")) {
            pos += 5;
            return true;
        }
        if (s.substr(pos).starts_with("null")) {
            pos += 4;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return skip_number();
        }
        err = L"unexpected token in JSON value";
        return false;
    }

    bool parse_string_array(std::vector<std::string>& out_items) {
        skip_ws();
        if (pos >= s.size() || s[pos] != '[') {
            err = L"expected JSON array";
            return false;
        }
        ++pos;
        skip_ws();
        if (pos < s.size() && s[pos] == ']') {
            ++pos;
            return true;
        }
        for (;;) {
            std::string item;
            if (!parse_string(item)) {
                return false;
            }
            if (!item.empty()) {
                out_items.push_back(std::move(item));
            }
            skip_ws();
            if (pos < s.size() && s[pos] == ',') {
                ++pos;
                continue;
            }
            if (pos < s.size() && s[pos] == ']') {
                ++pos;
                return true;
            }
            err = L"expected ',' or ']' in JSON string array";
            return false;
        }
    }

    bool parse_imported_entries(std::vector<std::string>& out_paths) {
        skip_ws();
        if (pos >= s.size() || s[pos] != '[') {
            err = L"expected JSON array for ImportedModules/ImportedHeaderUnits";
            return false;
        }
        ++pos;
        skip_ws();
        if (pos < s.size() && s[pos] == ']') {
            ++pos;
            return true;
        }
        for (;;) {
            skip_ws();
            if (pos >= s.size()) {
                err = L"unterminated JSON array";
                return false;
            }
            if (s[pos] == '"') {
                std::string item;
                if (!parse_string(item)) {
                    return false;
                }
                if (!item.empty()) {
                    out_paths.push_back(std::move(item));
                }
            } else if (s[pos] == '{') {
                ++pos;
                skip_ws();
                if (pos < s.size() && s[pos] == '}') {
                    ++pos;
                } else {
                    for (;;) {
                        std::string k;
                        if (!parse_string(k)) {
                            return false;
                        }
                        skip_ws();
                        if (pos >= s.size() || s[pos] != ':') {
                            err = L"expected ':' in imported entry object";
                            return false;
                        }
                        ++pos;
                        if (k == "BMI" || k == "Source" || k == "Header") {
                            std::string v;
                            if (!parse_string(v)) {
                                return false;
                            }
                            if (!v.empty()) {
                                out_paths.push_back(std::move(v));
                            }
                        } else if (!skip_value(2)) {
                            return false;
                        }
                        skip_ws();
                        if (pos < s.size() && s[pos] == ',') {
                            ++pos;
                            continue;
                        }
                        if (pos < s.size() && s[pos] == '}') {
                            ++pos;
                            break;
                        }
                        err = L"expected ',' or '}' in imported entry object";
                        return false;
                    }
                }
            } else {
                err = L"unexpected element type in ImportedModules/ImportedHeaderUnits";
                return false;
            }
            skip_ws();
            if (pos < s.size() && s[pos] == ',') {
                ++pos;
                continue;
            }
            if (pos < s.size() && s[pos] == ']') {
                ++pos;
                return true;
            }
            err = L"expected ',' or ']' in imported array";
            return false;
        }
    }

    bool parse_root(std::vector<std::wstring>& out_paths) {
        skip_ws();
        if (pos >= s.size() || s[pos] != '{') {
            err = L"/sourceDependencies JSON root must be an object";
            return false;
        }
        ++pos;
        skip_ws();
        if (pos < s.size() && s[pos] == '}') {
            err = L"/sourceDependencies JSON root missing required 'Data' object";
            return false;
        }

        bool saw_version = false;
        bool saw_data = false;
        bool saw_source = false;
        bool saw_includes = false;
        std::vector<std::string> utf8_paths;

        for (;;) {
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            skip_ws();
            if (pos >= s.size() || s[pos] != ':') {
                err = L"expected ':' after JSON root key";
                return false;
            }
            ++pos;
            if (key == "Version") {
                std::string ver;
                if (!parse_string(ver) || ver.empty()) {
                    err = L"/sourceDependencies JSON 'Version' must be a non-empty string";
                    return false;
                }
                saw_version = true;
            } else if (key == "Data") {
                saw_data = true;
                skip_ws();
                if (pos >= s.size() || s[pos] != '{') {
                    err = L"/sourceDependencies JSON 'Data' must be an object";
                    return false;
                }
                ++pos;
                skip_ws();
                if (pos < s.size() && s[pos] == '}') {
                    ++pos;
                } else {
                    for (;;) {
                        std::string dkey;
                        if (!parse_string(dkey)) {
                            return false;
                        }
                        skip_ws();
                        if (pos >= s.size() || s[pos] != ':') {
                            err = L"expected ':' in 'Data' object";
                            return false;
                        }
                        ++pos;
                        if (dkey == "Source" || dkey == "PCH") {
                            std::string val;
                            if (!parse_string(val)) {
                                return false;
                            }
                            if (dkey == "Source") {
                                if (val.empty()) {
                                    err = L"/sourceDependencies JSON 'Data.Source' is empty";
                                    return false;
                                }
                                saw_source = true;
                                utf8_paths.push_back(std::move(val));
                            }
                        } else if (dkey == "Includes") {
                            if (!parse_string_array(utf8_paths)) {
                                return false;
                            }
                            saw_includes = true;
                        } else if (dkey == "ImportedModules" || dkey == "ImportedHeaderUnits") {
                            if (!parse_imported_entries(utf8_paths)) {
                                return false;
                            }
                        } else if (!skip_value(2)) {
                            return false;
                        }
                        skip_ws();
                        if (pos < s.size() && s[pos] == ',') {
                            ++pos;
                            continue;
                        }
                        if (pos < s.size() && s[pos] == '}') {
                            ++pos;
                            break;
                        }
                        err = L"expected ',' or '}' in 'Data' object";
                        return false;
                    }
                }
            } else if (!skip_value(1)) {
                return false;
            }

            skip_ws();
            if (pos < s.size() && s[pos] == ',') {
                ++pos;
                continue;
            }
            if (pos < s.size() && s[pos] == '}') {
                ++pos;
                break;
            }
            err = L"expected ',' or '}' in root JSON object";
            return false;
        }

        skip_ws();
        if (pos != s.size()) {
            err = L"trailing characters after /sourceDependencies JSON object";
            return false;
        }
        if (!saw_version || !saw_data || !saw_source || !saw_includes) {
            err = L"/sourceDependencies JSON missing required Version/Data/Source/Includes fields";
            return false;
        }

        for (const std::string& p8 : utf8_paths) {
            std::wstring pw;
            if (!utf8_to_wide(p8, pw, err)) {
                return false;
            }
            if (!pw.empty()) {
                out_paths.push_back(std::move(pw));
            }
        }
        return true;
    }
};

struct ParsedCompileArgs {
    std::vector<std::wstring> sources;
    bool has_fo = false;
    std::wstring fo_value;
    bool has_source_deps = false;
    std::wstring source_deps_value;
    bool syntax_only = false;
    bool help_only = false;
};

bool is_switch_token(const std::wstring& tok) {
    return !tok.empty() && (tok[0] == L'/' || tok[0] == L'-');
}

std::wstring strip_switch_prefix(const std::wstring& tok) {
    return is_switch_token(tok) ? tok.substr(1) : std::wstring{};
}

bool takes_separate_arg(const std::wstring& sw) {
    static const wchar_t* const kExact[] = {
        L"I", L"FI", L"FU", L"AI", L"D", L"U",
        L"Fo", L"Fd", L"Fe", L"Fa", L"Fi", L"Fm", L"Fp", L"FR", L"Fr", L"Fc", L"Fl",
        L"sourceDependencies", L"sourceDependencies:directives",
        L"external:I", L"external:env", L"imsvc", L"Xclang", L"analyze:log",
        L"wd", L"we", L"wo", L"w1", L"w2", L"w3", L"w4",
        L"Yc", L"Yu", L"YX",
        L"Tc", L"Tp"
    };
    for (const wchar_t* item : kExact) {
        if (sw == item) {
            return true;
        }
    }
    return false;
}

bool parse_compile_args(const std::vector<std::wstring>& tokens, ParsedCompileArgs& out, std::wstring& out_err) {
    for (size_t i = 0; i < tokens.size(); ++i) {
        const std::wstring& tok = tokens[i];
        if (!is_switch_token(tok)) {
            out.sources.push_back(tok);
            continue;
        }
        const std::wstring sw = strip_switch_prefix(tok);
        if (sw == L"link") {
            break;
        }
        if (sw == L"?" || sw == L"help" || sw == L"-help" || sw == L"version" || sw == L"-version") {
            out.help_only = true;
            continue;
        }
        if (sw == L"Zs") {
            out.syntax_only = true;
            continue;
        }
        if (sw == L"Fo") {
            if (i + 1 >= tokens.size()) {
                out_err = L"missing value after /Fo switch";
                return false;
            }
            out.has_fo = true;
            out.fo_value = tokens[++i];
            continue;
        }
        if (sw.size() > 2 && sw.compare(0, 2, L"Fo") == 0) {
            out.has_fo = true;
            out.fo_value = (sw.size() > 3 && sw[2] == L':') ? sw.substr(3) : sw.substr(2);
            continue;
        }
        if (sw == L"sourceDependencies") {
            if (i + 1 >= tokens.size()) {
                out_err = L"missing value after /sourceDependencies switch";
                return false;
            }
            out.has_source_deps = true;
            out.source_deps_value = tokens[++i];
            continue;
        }
        if (sw.compare(0, 19, L"sourceDependencies:") == 0) {
            if (sw == L"sourceDependencies:directives") {
                if (i + 1 < tokens.size()) {
                    ++i;
                }
                continue;
            }
            const std::wstring val = sw.substr(19);
            if (!val.starts_with(L"directives")) {
                out.has_source_deps = true;
                out.source_deps_value = val;
            }
            continue;
        }
        if (sw.size() > 18 && sw.compare(0, 18, L"sourceDependencies") == 0 && sw[18] != L':') {
            out.has_source_deps = true;
            out.source_deps_value = sw.substr(18);
            continue;
        }
        if (sw == L"Tc" || sw == L"Tp") {
            if (i + 1 < tokens.size()) {
                out.sources.push_back(tokens[++i]);
            }
            continue;
        }
        if (sw.size() > 2 && (sw.compare(0, 2, L"Tc") == 0 || sw.compare(0, 2, L"Tp") == 0)) {
            out.sources.push_back(sw.substr(2));
            continue;
        }
        if (takes_separate_arg(sw) && i + 1 < tokens.size()) {
            ++i;
        }
    }
    return true;
}

std::wstring resolve_output_path(const std::wstring& value,
                                 const fs::path& src_path,
                                 const wchar_t* ext,
                                 bool completes_without_trailing_sep) {
    if (value.empty()) {
        return {};
    }
    const bool trailing_sep = (value.back() == L'\\' || value.back() == L'/');
    const bool treat_as_dir = trailing_sep || (!completes_without_trailing_sep && is_existing_dir(value));
    if (treat_as_dir) {
        const fs::path base(value);
        const fs::path leaf = completes_without_trailing_sep
            ? fs::path(src_path.stem().wstring() + ext)
            : fs::path(src_path.filename().wstring() + ext);
        return (base / leaf).lexically_normal().wstring();
    }
    fs::path p(value);
    if (completes_without_trailing_sep && !p.has_extension()) {
        p.replace_extension(ext);
    }
    return p.lexically_normal().wstring();
}

int replay_file_tracking(std::wstring_view raw_tail) {
    std::wstring err;
    const std::vector<std::wstring> raw_tokens = tokenize_crt(raw_tail);
    std::vector<std::wstring> tokens;
    std::vector<std::wstring> rsp_files;
    if (!expand_tokens_recursive(raw_tokens, 0, tokens, rsp_files, err)) {
        std::fwprintf(stderr, L"sccache-cl error: %ls\n", err.c_str());
        return 3;
    }

    ParsedCompileArgs args;
    if (!parse_compile_args(tokens, args, err)) {
        std::fwprintf(stderr, L"sccache-cl error: %ls\n", err.c_str());
        return 3;
    }

    if (args.help_only || args.syntax_only || args.sources.empty()) {
        return 0;
    }

    std::unordered_set<std::wstring> seen_reads;
    auto record_read = [&](const std::wstring& p) -> bool {
        if (p.empty()) {
            return true;
        }
        const std::wstring key = to_lower_ascii(to_full_path(p));
        if (!seen_reads.insert(key).second) {
            return true;
        }
        if (!touch_read(p, err)) {
            std::fwprintf(stderr, L"sccache-cl error: %ls\n", err.c_str());
            return false;
        }
        return true;
    };

    for (const std::wstring& rsp : rsp_files) {
        if (!record_read(rsp)) {
            return 3;
        }
    }

    for (const std::wstring& src : args.sources) {
        if (!record_read(src)) {
            return 3;
        }

        const fs::path src_path(src);
        std::wstring obj_path;
        if (args.has_fo) {
            obj_path = resolve_output_path(args.fo_value, src_path, L".obj", true);
        } else {
            obj_path = (src_path.stem().wstring() + L".obj");
        }

        if (args.has_source_deps) {
            const std::wstring json_path = resolve_output_path(args.source_deps_value, src_path, L".json", false);
            if (json_path.empty()) {
                std::fwprintf(stderr, L"sccache-cl error: empty /sourceDependencies path\n");
                return 3;
            }
            if (!is_existing_file(json_path)) {
                std::fwprintf(stderr, L"sccache-cl error: missing expected /sourceDependencies JSON '%ls'\n",
                              json_path.c_str());
                return 3;
            }
            std::vector<unsigned char> json_bytes;
            if (!read_all_bytes(json_path, json_bytes, err)) {
                std::fwprintf(stderr, L"sccache-cl error: %ls\n", err.c_str());
                return 3;
            }
            if (json_bytes.empty()) {
                std::fwprintf(stderr, L"sccache-cl error: empty /sourceDependencies JSON '%ls'\n",
                              json_path.c_str());
                return 3;
            }
            const std::string_view json_view(reinterpret_cast<const char*>(json_bytes.data()), json_bytes.size());
            SourceDepsJsonParser parser{json_view, 0, {}};
            std::vector<std::wstring> dep_paths;
            if (!parser.parse_root(dep_paths)) {
                std::fwprintf(stderr, L"sccache-cl error: malformed /sourceDependencies JSON '%ls': %ls\n",
                              json_path.c_str(), parser.err.c_str());
                return 3;
            }
            for (const std::wstring& dep : dep_paths) {
                if (!record_read(dep)) {
                    return 3;
                }
            }
            if (!touch_write(json_path, err)) {
                std::fwprintf(stderr, L"sccache-cl error: %ls\n", err.c_str());
                return 3;
            }
        }

        if (!touch_write(obj_path, err)) {
            std::fwprintf(stderr, L"sccache-cl error: %ls\n", err.c_str());
            return 3;
        }
    }
    return 0;
}

}  // namespace

int wmain() {
    // Unset VS_UNICODE_OUTPUT before calling sccache: MSBuild passes a process-local
    // pipe handle number in that variable, which is invalid inside the out-of-process
    // sccache daemon and otherwise swallows compiler errors and warnings.
    SetEnvironmentVariableW(L"VS_UNICODE_OUTPUT", nullptr);

    const std::wstring sccache_exe = get_env_var(L"RECTO_SCCACHE_EXE");
    if (sccache_exe.empty() || !is_existing_file(sccache_exe)) {
        std::fwprintf(stderr, L"sccache-cl error: RECTO_SCCACHE_EXE is unset or missing ('%ls')\n",
                      sccache_exe.c_str());
        return 2;
    }

    std::wstring real_cl;
    std::wstring err;
    if (!resolve_real_cl(real_cl, err)) {
        std::fwprintf(stderr, L"sccache-cl error: %ls\n", err.c_str());
        return 2;
    }

    const std::wstring_view tail = extract_command_tail(GetCommandLineW());
    std::wstring cmd;
    cmd.reserve(sccache_exe.size() + real_cl.size() + tail.size() + 8);
    cmd.push_back(L'"');
    cmd.append(sccache_exe);
    cmd.append(L"\" \"");
    cmd.append(real_cl);
    cmd.push_back(L'"');
    if (!tail.empty()) {
        cmd.push_back(L' ');
        cmd.append(tail);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(sccache_exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi) == FALSE) {
        const std::wstring launch_err = format_win32_error(GetLastError());
        std::fwprintf(stderr, L"sccache-cl error: failed to launch '%ls': %ls\n",
                      sccache_exe.c_str(), launch_err.c_str());
        return 2;
    }

    HandleGuard proc(pi.hProcess);
    HandleGuard thread(pi.hThread);
    if (WaitForSingleObject(proc.h, INFINITE) != WAIT_OBJECT_0) {
        const std::wstring wait_err = format_win32_error(GetLastError());
        std::fwprintf(stderr, L"sccache-cl error: waiting for sccache failed: %ls\n", wait_err.c_str());
        return 2;
    }

    DWORD exit_code = 1;
    if (GetExitCodeProcess(proc.h, &exit_code) == FALSE) {
        const std::wstring code_err = format_win32_error(GetLastError());
        std::fwprintf(stderr, L"sccache-cl error: GetExitCodeProcess failed: %ls\n", code_err.c_str());
        return 2;
    }

    if (exit_code != 0) {
        return static_cast<int>(exit_code);
    }

    return replay_file_tracking(tail);
}
