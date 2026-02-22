#pragma once

#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprutils/cli/Logger.hpp>

using Hyprutils::CLI::eLogLevel;

inline constexpr auto TRACE = Log::TRACE;
inline constexpr auto DEBUG = Log::DEBUG;
inline constexpr auto INFO  = Log::INFO;
inline constexpr auto WARN  = Log::WARN;
inline constexpr auto ERR   = Log::ERR;
inline constexpr auto CRIT  = Log::CRIT;

template <typename... Args>
void hsLog(eLogLevel level, std::format_string<Args...> fmt, Args&&... args) {
	auto msg = std::vformat(fmt.get(), std::make_format_args(args...));
	Log::logger->log(level, "[hyprsplit] {}", msg);
}
