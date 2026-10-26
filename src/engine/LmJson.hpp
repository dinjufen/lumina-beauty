#ifndef LUMINA_JSON_HPP
#define LUMINA_JSON_HPP

#include <map>
#include <string>
#include <vector>

/**
 * @brief 极简 JSON 值，只用于读取素材文件
 * @remark 支持对象、数组、数字、字符串、布尔和 null；字符串里的 \uXXXX 转成 UTF-8。不追求完整的标准兼容，
 *         素材文件很小，解析一次即可，不考虑性能。
 */
class LmJson {
public:
    enum Type { NUL, BOOL, NUMBER, STRING, ARRAY, OBJECT };

    /**
     * @brief 解析整段文本
     * @param error 失败时写入"第几行：原因"
     */
    static bool Parse(const std::string& text, LmJson& out, std::string& error);

    Type type() const { return m_type; }
    bool IsNumber() const { return m_type == NUMBER; }
    bool IsString() const { return m_type == STRING; }
    bool IsArray() const { return m_type == ARRAY; }
    bool IsObject() const { return m_type == OBJECT; }

    double Number() const { return m_number; }
    const std::string& String() const { return m_string; }
    const std::vector<LmJson>& Array() const { return m_array; }
    const std::map<std::string, LmJson>& Object() const { return m_object; }

    /// 对象的成员，不存在或不是对象时返回 nullptr
    const LmJson* Find(const char* key) const;

private:
    friend class LmJsonParser;
    Type m_type = NUL;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::vector<LmJson> m_array;
    std::map<std::string, LmJson> m_object;
};

#endif // LUMINA_JSON_HPP
