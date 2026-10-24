#ifndef LUMINA_LOG_HPP
#define LUMINA_LOG_HPP

#include <sstream>
#include "lumina/lumina.h"

/**
 * @brief 流式日志：LM_LOG_I << "x = " << x;
 * @remark 语句结束时整行交给 lumina_set_log_callback 设置的回调，没有回调时警告和错误写到 stderr
 */
class LmLogLine {
public:
    explicit LmLogLine(lumina_log_level level) : m_level(level) {}
    ~LmLogLine();

    template <typename T>
    LmLogLine& operator<<(const T& value) {
        m_stream << value;
        return *this;
    }

private:
    lumina_log_level m_level;
    std::ostringstream m_stream;
};

#define LM_LOG_I LmLogLine(LUMINA_LOG_INFO)
#define LM_LOG_W LmLogLine(LUMINA_LOG_WARN)
#define LM_LOG_E LmLogLine(LUMINA_LOG_ERROR)
#define LM_LOG_D LmLogLine(LUMINA_LOG_INFO)

#endif // LUMINA_LOG_HPP
