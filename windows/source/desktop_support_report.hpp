#pragma once
#include "../vendor/native/support/support_report.hpp"
#include <functional>

namespace outrun::desktop::support {
struct Actions {
    std::function<std::optional<std::filesystem::path>()> choose_export;
    std::function<bool(const std::string&)> open_browser;
};
class Draft {
public:
    Draft(bool enabled, std::filesystem::path runtime_log);
    void inputs(std::string title, std::string description, bool include_log);
    bool prepare();
    void review(bool accepted);
    bool open(const Actions& actions);
    const std::optional<dbce::support::PreparedReport>& report() const { return report_; }
    const std::optional<dbce::support::BrowserSubmission>& submission() const { return submission_; }
    bool reviewed() const { return reviewed_; }
    const std::string& message() const { return message_; }
private:
    bool enabled_, include_log_{}, reviewed_{};
    std::filesystem::path runtime_log_;
    std::string title_, description_, message_;
    std::optional<dbce::support::PreparedReport> report_;
    std::optional<dbce::support::BrowserSubmission> submission_;
};
} // namespace outrun::desktop::support
