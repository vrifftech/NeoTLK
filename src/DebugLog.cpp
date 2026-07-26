// SPDX-License-Identifier: GPL-3.0-or-later

#include "neotlk/DebugLog.hpp"

#include <filesystem>
#include <system_error>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace neotlk {
namespace {

std::filesystem::path logPathFromString(const std::string& path) {
    return std::filesystem::path(path);
}

std::string asciiLowerLocal(std::string value) {
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c + ('a' - 'A'));
        }
    }
    return value;
}

bool pathLooksLikeTlkFile(const std::filesystem::path& path) {
    const std::string extension = asciiLowerLocal(path.extension().string());
    return extension == ".tlk";
}

bool existingPathIsSafeRegularLogTarget(const std::filesystem::path& path) {
    if (path.empty() || pathLooksLikeTlkFile(path)) {
        return false;
    }

    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, ec);
    if (ec || !std::filesystem::exists(status)) {
        return true;
    }

    if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        return false;
    }

    ec.clear();
    const auto linkCount = std::filesystem::hard_link_count(path, ec);
    if (!ec && linkCount > 1) {
        return false;
    }

    const auto permissions = status.permissions();
    const auto writeBits = std::filesystem::perms::owner_write |
                           std::filesystem::perms::group_write |
                           std::filesystem::perms::others_write;
    if (permissions != std::filesystem::perms::unknown && (permissions & writeBits) == std::filesystem::perms::none) {
        return false;
    }

#ifndef _WIN32
    if (::access(path.c_str(), W_OK) != 0) {
        return false;
    }
#endif

    return true;
}

} // namespace

void DebugLog::open(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    output_.close();
    if (path.find('\0') != std::string::npos) {
        return;
    }
    const std::filesystem::path logPath = logPathFromString(path);
    if (!existingPathIsSafeRegularLogTarget(logPath)) {
        return;
    }
    output_.open(logPath, std::ios::out | std::ios::app);
}

void DebugLog::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    output_.close();
}

bool DebugLog::isOpen() const noexcept { return output_.is_open(); }

void DebugLog::writeLine(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (output_) {
        output_ << text << '\n';
        output_.flush();
    }
}

DebugLog& globalDebugLog() {
    static DebugLog log;
    return log;
}

void DBP(const std::string& text) { globalDebugLog().writeLine(text); }

} // namespace neotlk
