//
// Created by hyp on 2026/3/28.
//

#include "logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

namespace {
    const std::filesystem::path &log_file_path() {
        static const std::filesystem::path path = std::filesystem::path("logs") / "server.log";
        return path;
    }

    std::mutex &log_mutex() {
        static std::mutex mutex;
        return mutex;
    }

    std::ofstream &log_file_stream() {
        static std::ofstream stream;
        static bool initialized = false;
        if (!initialized) {
            std::filesystem::create_directories(log_file_path().parent_path());
            stream.open(log_file_path(), std::ios::out | std::ios::app);
            initialized = true;
        }
        return stream;
    }

    const char *level_to_string(LogLevel level) {
        switch (level) {
            case LogLevel::Info:
                return "INFO";
            case LogLevel::Warn:
                return "WARN";
            case LogLevel::Error:
                return "ERROR";
            case LogLevel::Fatal:
                return "FATAL";
        }
        return "INFO";
    }

    std::string current_timestamp_string() {
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
}

void log_message(LogLevel level, const std::string &module, const std::string &message) {
    std::lock_guard<std::mutex> lock(log_mutex());
    std::ostream &stream = (level == LogLevel::Error || level == LogLevel::Fatal) ? std::cerr : std::cout;
    std::ostringstream line;
    line << "[" << current_timestamp_string() << "]"
         << "[" << level_to_string(level) << "]"
         << "[" << module << "] "
         << message;

    stream << line.str() << "\n";
    std::ofstream &file = log_file_stream();
    if (file.is_open()) {
        file << line.str() << "\n";
        file.flush();
    }
}

void log_info(const std::string &module, const std::string &message) {
    log_message(LogLevel::Info, module, message);
}

void log_warn(const std::string &module, const std::string &message) {
    log_message(LogLevel::Warn, module, message);
}

void log_error(const std::string &module, const std::string &message) {
    log_message(LogLevel::Error, module, message);
}

void log_fatal(const std::string &module, const std::string &message) {
    log_message(LogLevel::Fatal, module, message);
}
