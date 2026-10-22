#include "desktop_support_report.hpp"
#include <exception>
#include <utility>

namespace outrun::desktop::support {
Draft::Draft(bool enabled, std::filesystem::path runtime_log)
    : enabled_(enabled), runtime_log_(std::move(runtime_log)) {}
void Draft::inputs(std::string title, std::string description, bool include_log) {
    if (title_ == title && description_ == description && include_log_ == include_log) return;
    title_ = std::move(title); description_ = std::move(description); include_log_ = include_log;
    report_.reset(); submission_.reset(); reviewed_ = false; message_.clear();
}
bool Draft::prepare() {
    report_.reset(); submission_.reset(); reviewed_ = false; message_.clear();
    if (!enabled_) { message_ = "Reporting is disabled in tests and playback."; return false; }
    try {
        const dbce::support::ReportProduct product("OutRun 2006 desktop (decomp)",
            "Unversioned desktop source build; compiled " __DATE__ " " __TIME__, "d-b-c-e/outrun2-decomp", "F6 Help");
        report_ = dbce::support::prepare(product, title_, description_,
            include_log_ ? std::vector<std::filesystem::path>{runtime_log_} : std::vector<std::filesystem::path>{});
        return true;
    } catch (const std::exception&) {
        report_.reset(); submission_.reset();
        message_ = "Could not prepare report. Enter a title (up to 200 characters) and description (up to 10000). "
            "If runtime.log is selected, it must exist and contain valid local text; deselect it to try without logs.";
        return false;
    }
}
void Draft::review(bool accepted) {
    reviewed_ = accepted && report_.has_value(); submission_.reset();
    if (!reviewed_) return;
    try { submission_.emplace(report_->create_browser_submission(reviewed_)); }
    catch (const std::exception&) {
        report_.reset(); reviewed_ = false;
        message_ = "Could not prepare the browser draft. Shorten the title and prepare and review again.";
    }
}
bool Draft::open(const Actions& actions) {
    if (!enabled_ || !report_ || !reviewed_ || !submission_) {
        message_ = "Prepare and review the current report before opening GitHub. Reporting is disabled in tests and playback.";
        return false;
    }
    try {
        const auto& submission = *submission_;
        if (submission.needs_attachment) {
            std::optional<std::filesystem::path> path;
            try { path = actions.choose_export(); }
            catch (const std::exception&) { message_ = "Export dialog failed. Retry or cancel; no browser draft opened."; return false; }
            if (!path) { message_ = "Export canceled. No browser draft opened; the reviewed preview is retained."; return false; }
            try { report_->write_review_file(*path); }
            catch (const std::exception&) {
                message_ = "Could not export. Choose a new .txt file in an existing local folder. Existing files are never overwritten; "
                    "a failed write may leave a partial file. No browser draft opened.";
                return false;
            }
            message_ = "Reviewed report exported. " + submission.instructions;
        } else message_ = submission.instructions;
        if (!actions.open_browser(submission.url)) {
            message_ += " Browser launch failed. Retry the reviewed action; use a new filename if exporting again.";
            return false;
        }
        return true;
    } catch (const std::exception&) {
        message_ = "Could not open the reviewed draft. Shorten the title and prepare and review again. No issue was submitted.";
        return false;
    }
}
} // namespace outrun::desktop::support
