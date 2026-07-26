// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace neotlk {

class DebugLog {
public:
    DebugLog() = default;
    explicit DebugLog(const std::string& path) { open(path); }
    void open(const std::string& path);
    void close();
    bool isOpen() const noexcept;
    void writeLine(const std::string& text);

private:
    mutable std::mutex mutex_;
    std::ofstream output_;
};

DebugLog& globalDebugLog();
void DBP(const std::string& text);

} // namespace neotlk
