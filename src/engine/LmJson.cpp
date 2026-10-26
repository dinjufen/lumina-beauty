#include "LmJson.hpp"

#include <cctype>
#include <clocale>
#include <cstdlib>
#include <cstring>

class LmJsonParser {
public:
    explicit LmJsonParser(const std::string& text) : m_p(text.c_str()), m_end(text.c_str() + text.size()) {}

    bool Run(LmJson& out, std::string& error) {
        SkipSpace();
        if (!Value(out, 0)) {
            error = Where() + m_error;
            return false;
        }
        SkipSpace();
        if (m_p != m_end) {
            error = Where() + "unexpected content after the end";
            return false;
        }
        return true;
    }

private:
    const int kMaxDepth = 64;

    std::string Where() const { return "line " + std::to_string(m_line) + ": "; }

    bool Fail(const char* message) {
        m_error = message;
        return false;
    }

    void SkipSpace() {
        while (m_p < m_end) {
            if (*m_p == '\n') {
                ++m_line;
            }
            if (*m_p == ' ' || *m_p == '\t' || *m_p == '\r' || *m_p == '\n') {
                ++m_p;
            } else {
                break;
            }
        }
    }

    bool Literal(const char* word) {
        const size_t n = strlen(word);
        if (static_cast<size_t>(m_end - m_p) < n || strncmp(m_p, word, n) != 0) {
            return Fail("unknown value");
        }
        m_p += n;
        return true;
    }

    bool Value(LmJson& out, int depth) {
        if (depth > kMaxDepth) {
            return Fail("nested too deeply");
        }
        if (m_p >= m_end) {
            return Fail("unexpected end of file");
        }
        switch (*m_p) {
            case '{': return ObjectValue(out, depth);
            case '[': return ArrayValue(out, depth);
            case '"': out.m_type = LmJson::STRING; return StringValue(out.m_string);
            case 't': out.m_type = LmJson::BOOL; out.m_bool = true; return Literal("true");
            case 'f': out.m_type = LmJson::BOOL; out.m_bool = false; return Literal("false");
            case 'n': out.m_type = LmJson::NUL; return Literal("null");
            default: return NumberValue(out);
        }
    }

    bool NumberValue(LmJson& out) {
        // 自己截出数字再用 C 本地化解析，不受宿主程序 setlocale 改小数点的影响
        const char* start = m_p;
        if (m_p < m_end && (*m_p == '-' || *m_p == '+')) {
            ++m_p;
        }
        while (m_p < m_end && (isdigit(static_cast<unsigned char>(*m_p)) || *m_p == '.' || *m_p == 'e' ||
                               *m_p == 'E' || *m_p == '-' || *m_p == '+')) {
            ++m_p;
        }
        std::string token(start, m_p);
        if (token.empty() || token == "-" || token == "+") {
            return Fail("expected a value");
        }
        const char* point = localeconv()->decimal_point;
        if (point && point[0] != '.') {
            for (char& c : token) {
                if (c == '.') {
                    c = point[0];
                }
            }
        }
        char* stop = nullptr;
        out.m_type = LmJson::NUMBER;
        out.m_number = strtod(token.c_str(), &stop);
        if (!stop || *stop != '\0') {
            return Fail("invalid number");
        }
        return true;
    }

    static void AppendUtf8(std::string& s, unsigned code) {
        if (code < 0x80) {
            s += static_cast<char>(code);
        } else if (code < 0x800) {
            s += static_cast<char>(0xC0 | (code >> 6));
            s += static_cast<char>(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            s += static_cast<char>(0xE0 | (code >> 12));
            s += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (code >> 18));
            s += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (code & 0x3F));
        }
    }

    bool Hex4(unsigned& code) {
        if (m_end - m_p < 4) {
            return Fail("bad \\u escape");
        }
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = *m_p++;
            code <<= 4;
            if (c >= '0' && c <= '9') code |= c - '0';
            else if (c >= 'a' && c <= 'f') code |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') code |= c - 'A' + 10;
            else return Fail("bad \\u escape");
        }
        return true;
    }

    bool StringValue(std::string& s) {
        ++m_p;  // 开头的引号
        while (m_p < m_end && *m_p != '"') {
            const char c = *m_p++;
            if (c == '\n') {
                return Fail("line break inside a string");
            }
            if (c != '\\') {
                s += c;
                continue;
            }
            if (m_p >= m_end) {
                break;
            }
            const char esc = *m_p++;
            switch (esc) {
                case '"': s += '"'; break;
                case '\\': s += '\\'; break;
                case '/': s += '/'; break;
                case 'b': s += '\b'; break;
                case 'f': s += '\f'; break;
                case 'n': s += '\n'; break;
                case 'r': s += '\r'; break;
                case 't': s += '\t'; break;
                case 'u': {
                    unsigned code = 0;
                    if (!Hex4(code)) {
                        return false;
                    }
                    // 代理对
                    if (code >= 0xD800 && code < 0xDC00 && m_end - m_p >= 6 && m_p[0] == '\\' && m_p[1] == 'u') {
                        m_p += 2;
                        unsigned low = 0;
                        if (!Hex4(low)) {
                            return false;
                        }
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    }
                    AppendUtf8(s, code);
                    break;
                }
                default: return Fail("bad escape in a string");
            }
        }
        if (m_p >= m_end) {
            return Fail("unterminated string");
        }
        ++m_p;  // 结尾的引号
        return true;
    }

    bool ArrayValue(LmJson& out, int depth) {
        out.m_type = LmJson::ARRAY;
        ++m_p;
        SkipSpace();
        if (m_p < m_end && *m_p == ']') {
            ++m_p;
            return true;
        }
        while (true) {
            out.m_array.emplace_back();
            if (!Value(out.m_array.back(), depth + 1)) {
                return false;
            }
            SkipSpace();
            if (m_p < m_end && *m_p == ',') {
                ++m_p;
                SkipSpace();
                continue;
            }
            if (m_p < m_end && *m_p == ']') {
                ++m_p;
                return true;
            }
            return Fail("expected , or ] in an array");
        }
    }

    bool ObjectValue(LmJson& out, int depth) {
        out.m_type = LmJson::OBJECT;
        ++m_p;
        SkipSpace();
        if (m_p < m_end && *m_p == '}') {
            ++m_p;
            return true;
        }
        while (true) {
            if (m_p >= m_end || *m_p != '"') {
                return Fail("expected a \"key\" in an object");
            }
            std::string key;
            if (!StringValue(key)) {
                return false;
            }
            SkipSpace();
            if (m_p >= m_end || *m_p != ':') {
                return Fail("expected : after a key");
            }
            ++m_p;
            SkipSpace();
            LmJson value;
            if (!Value(value, depth + 1)) {
                return false;
            }
            out.m_object[key] = std::move(value);
            SkipSpace();
            if (m_p < m_end && *m_p == ',') {
                ++m_p;
                SkipSpace();
                continue;
            }
            if (m_p < m_end && *m_p == '}') {
                ++m_p;
                return true;
            }
            return Fail("expected , or } in an object");
        }
    }

    const char* m_p;
    const char* m_end;
    int m_line = 1;
    std::string m_error;
};

bool LmJson::Parse(const std::string& text, LmJson& out, std::string& error) {
    out = LmJson();
    std::string body = text;
    // 去掉 UTF-8 BOM，Windows 记事本保存的文件常带
    if (body.size() >= 3 && static_cast<unsigned char>(body[0]) == 0xEF &&
        static_cast<unsigned char>(body[1]) == 0xBB && static_cast<unsigned char>(body[2]) == 0xBF) {
        body.erase(0, 3);
    }
    LmJsonParser parser(body);
    return parser.Run(out, error);
}

const LmJson* LmJson::Find(const char* key) const {
    if (m_type != OBJECT) {
        return nullptr;
    }
    auto it = m_object.find(key);
    return it == m_object.end() ? nullptr : &it->second;
}
