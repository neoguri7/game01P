#pragma once

#include <entt/entt.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace game {

constexpr std::string_view LOGGER_NAME = "game01p";

struct FLogger {
    std::shared_ptr<spdlog::logger> logger;

    static void Initialize(entt::registry& reg, bool enableFileLog = true) {
        if (reg.ctx().contains<FLogger>()) {
            return;
        }

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_pattern("[%T.%e] [%^%l%$] %v");

        std::vector<spdlog::sink_ptr> sinks{consoleSink};

        if (enableFileLog) {
            auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("game01p.log", true);
            fileSink->set_pattern("[%Y-%m-%d %T.%e] [%l] %v");
            sinks.push_back(fileSink);
        }

        auto logger = std::make_shared<spdlog::logger>(std::string(LOGGER_NAME), sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::trace);
        spdlog::register_logger(logger);

        reg.ctx().emplace<FLogger>(FLogger{logger});
    }

    static void Shutdown(entt::registry& reg) {
        if (auto* ctx = reg.ctx().find<FLogger>()) {
            ctx->logger->flush();
            spdlog::drop(std::string(LOGGER_NAME));
        }
        reg.ctx().erase<FLogger>();
    }
};

} // namespace game

#define LOG_TRACE(...)    if (auto* __log = ::game::details::GetLoggerPtr()) __log->trace(__VA_ARGS__)
#define LOG_DEBUG(...)    if (auto* __log = ::game::details::GetLoggerPtr()) __log->debug(__VA_ARGS__)
#define LOG_INFO(...)     if (auto* __log = ::game::details::GetLoggerPtr()) __log->info(__VA_ARGS__)
#define LOG_WARN(...)     if (auto* __log = ::game::details::GetLoggerPtr()) __log->warn(__VA_ARGS__)
#define LOG_ERROR(...)    if (auto* __log = ::game::details::GetLoggerPtr()) __log->error(__VA_ARGS__)
#define LOG_CRITICAL(...) if (auto* __log = ::game::details::GetLoggerPtr()) __log->critical(__VA_ARGS__)

namespace game::details {
    inline std::shared_ptr<spdlog::logger> g_currentLogger;

    inline spdlog::logger* GetLoggerPtr() {
        if (!g_currentLogger) {
            return spdlog::get(std::string(LOGGER_NAME)).get();
        }
        return g_currentLogger.get();
    }
}

inline void SetCurrentGameLogger(std::shared_ptr<spdlog::logger> ptr) {
    ::game::details::g_currentLogger = std::move(ptr);
}
