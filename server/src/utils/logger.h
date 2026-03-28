//
// Created by hyp on 2026/3/28.
//

#pragma once
#include <string>

enum class LogLevel {
    Info,
    Warn,
    Error,
    Fatal
};

void log_message(LogLevel level, const std::string &module, const std::string &message);

void log_info(const std::string &module, const std::string &message);

void log_warn(const std::string &module, const std::string &message);

void log_error(const std::string &module, const std::string &message);

void log_fatal(const std::string &module, const std::string &message);
