#pragma once
#include <sstream>
#include <string>
#include <Util.h>

namespace reshade::log {
    enum class level {
        info,
        warning,
        error,
        debug
    };

    inline void message(level lvl, const char* msg) {
        if (!msg) return;
        switch (lvl) {
            case level::error:
                LOG_ERROR("[MFG Unlock] {}", msg);
                break;
            case level::warning:
                LOG_WARN("[MFG Unlock] {}", msg);
                break;
            case level::debug:
                LOG_DEBUG("[MFG Unlock] {}", msg);
                break;
            case level::info:
            default:
                LOG_INFO("[MFG Unlock] {}", msg);
                break;
        }
    }
}
