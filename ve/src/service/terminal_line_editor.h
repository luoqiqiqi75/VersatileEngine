#pragma once

#include "terminal_session.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ve::service {

enum class TerminalKey {
    NONE,
    CHARACTER,
    ENTER,
    BACKSPACE,
    TAB,
    CTRL_C,
    CTRL_U,
    LEFT,
    RIGHT,
    UP,
    DOWN,
    DELETE_KEY,
    HOME,
    END,
    EOF_KEY
};

struct TerminalKeyEvent
{
    TerminalKey key = TerminalKey::NONE;
    char ch = '\0';
};

struct TerminalEditResult
{
    std::string output;
    bool redraw = false;
    bool prompt_on_new_line = false;
    bool disconnect = false;
    bool eof = false;
};

class TerminalLineEditor
{
public:
    enum class Mode {
        COOKED,
        CONTROLLED
    };

    explicit TerminalLineEditor(TerminalSession* session, Mode mode = Mode::COOKED)
        : session_(session), mode_(mode)
    {
    }

    void setMode(Mode mode)
    {
        mode_ = mode;
    }

    Mode mode() const
    {
        return mode_;
    }

    const std::string& line() const
    {
        return line_;
    }

    size_t cursor() const
    {
        return cursor_;
    }

    std::string prompt() const
    {
        return session_ ? session_->prompt() : std::string("> ");
    }

    std::string renderedLine() const
    {
        return prompt() + line_;
    }

    void clearLine()
    {
        line_.clear();
        cursor_ = 0;
        history_index_ = -1;
        history_draft_.clear();
    }

    void appendCooked(char ch)
    {
        line_.push_back(ch);
        cursor_ = line_.size();
    }

    TerminalEditResult onKey(const TerminalKeyEvent& ev)
    {
        TerminalEditResult out;

        switch (ev.key) {
            case TerminalKey::CHARACTER:
                if (ev.ch >= 0x20 && ev.ch < 0x7f) {
                    resetHistoryBrowse();
                    line_.insert(cursor_, 1, ev.ch);
                    ++cursor_;
                    out.redraw = (mode_ == Mode::CONTROLLED);
                }
                return out;

            case TerminalKey::ENTER:
                return submitLine();

            case TerminalKey::BACKSPACE:
                if (cursor_ > 0) {
                    resetHistoryBrowse();
                    line_.erase(cursor_ - 1, 1);
                    --cursor_;
                    out.redraw = (mode_ == Mode::CONTROLLED);
                }
                return out;

            case TerminalKey::TAB:
                if (mode_ == Mode::CONTROLLED) {
                    return completeAtCursor();
                }
                line_.insert(cursor_, 1, '\t');
                ++cursor_;
                return out;

            case TerminalKey::CTRL_C:
                resetHistoryBrowse();
                clearLine();
                out.output = "^C\n";
                prefixOutputIfControlled(out.output);
                out.prompt_on_new_line = true;
                return out;

            case TerminalKey::CTRL_U:
                if (!line_.empty()) {
                    resetHistoryBrowse();
                    line_.erase(0, cursor_);
                    cursor_ = 0;
                    out.redraw = (mode_ == Mode::CONTROLLED);
                }
                return out;

            case TerminalKey::LEFT:
                if (mode_ == Mode::CONTROLLED && cursor_ > 0) {
                    --cursor_;
                    out.redraw = true;
                }
                return out;

            case TerminalKey::RIGHT:
                if (mode_ == Mode::CONTROLLED && cursor_ < line_.size()) {
                    ++cursor_;
                    out.redraw = true;
                }
                return out;

            case TerminalKey::HOME:
                if (mode_ == Mode::CONTROLLED && cursor_ != 0) {
                    cursor_ = 0;
                    out.redraw = true;
                }
                return out;

            case TerminalKey::END:
                if (mode_ == Mode::CONTROLLED && cursor_ != line_.size()) {
                    cursor_ = line_.size();
                    out.redraw = true;
                }
                return out;

            case TerminalKey::DELETE_KEY:
                if (mode_ == Mode::CONTROLLED && cursor_ < line_.size()) {
                    resetHistoryBrowse();
                    line_.erase(cursor_, 1);
                    out.redraw = true;
                }
                return out;

            case TerminalKey::UP:
                if (mode_ == Mode::CONTROLLED && browseHistory(-1)) {
                    out.redraw = true;
                }
                return out;

            case TerminalKey::DOWN:
                if (mode_ == Mode::CONTROLLED && browseHistory(1)) {
                    out.redraw = true;
                }
                return out;

            case TerminalKey::EOF_KEY:
                out.eof = true;
                out.disconnect = true;
                return out;

            case TerminalKey::NONE:
                return out;
        }

        return out;
    }

private:
    // CONTROLLED mode keeps prompt+input on one terminal row (\r redraw). Any printed
    // output must start with a newline so it does not append to that row (e.g. ls).
    void prefixOutputIfControlled(std::string& text) const
    {
        if (mode_ == Mode::CONTROLLED && !text.empty() && text.front() != '\n') {
            text.insert(text.begin(), '\n');
        }
    }

    static std::string commonPrefix(const std::vector<std::string>& items)
    {
        if (items.empty()) {
            return {};
        }

        std::string prefix = items.front();
        for (size_t i = 1; i < items.size(); ++i) {
            size_t j = 0;
            while (j < prefix.size() && j < items[i].size() && prefix[j] == items[i][j]) {
                ++j;
            }
            prefix.resize(j);
        }
        return prefix;
    }

    static std::string formatMatches(const std::vector<std::string>& matches)
    {
        if (matches.empty()) {
            return {};
        }

        std::string out = "\n";
        for (const auto& match : matches) {
            out += "  " + match + "\n";
        }
        return out;
    }

    void resetHistoryBrowse()
    {
        history_index_ = -1;
        history_draft_.clear();
    }

    bool browseHistory(int direction)
    {
        if (!session_) {
            return false;
        }

        const auto& hist = session_->history();
        if (hist.empty()) {
            return false;
        }

        if (direction < 0) {
            if (history_index_ < 0) {
                history_draft_ = line_;
                history_index_ = static_cast<int>(hist.size()) - 1;
            } else if (history_index_ > 0) {
                --history_index_;
            }
        } else {
            if (history_index_ < 0) {
                return false;
            }
            if (history_index_ + 1 < static_cast<int>(hist.size())) {
                ++history_index_;
            } else {
                history_index_ = -1;
                line_ = history_draft_;
                cursor_ = line_.size();
                history_draft_.clear();
                return true;
            }
        }

        line_ = hist[static_cast<size_t>(history_index_)];
        cursor_ = line_.size();
        return true;
    }

    TerminalEditResult completeAtCursor()
    {
        TerminalEditResult out;
        if (!session_) {
            return out;
        }

        std::string head = line_.substr(0, cursor_);
        auto matches = session_->complete(head);
        if (matches.empty()) {
            return out;
        }

        size_t word_start = head.find_last_of(' ');
        size_t replace_from = (word_start == std::string::npos) ? 0 : (word_start + 1);
        std::string prefix = head.substr(replace_from);
        std::string common = commonPrefix(matches);

        if (common.size() > prefix.size()) {
            std::string suffix = common.substr(prefix.size());
            line_.insert(cursor_, suffix);
            cursor_ += suffix.size();
        }

        bool is_first_word = (word_start == std::string::npos);
        bool single_match = (matches.size() == 1);
        bool ends_with_slash = !matches[0].empty() && matches[0].back() == '/';

        if (single_match && is_first_word && !ends_with_slash) {
            if (cursor_ == line_.size() || line_[cursor_] != ' ') {
                line_.insert(cursor_, 1, ' ');
                ++cursor_;
            }
        }

        if (matches.size() > 1) {
            out.output = formatMatches(matches);
        }
        out.redraw = true;
        return out;
    }

    bool applySubmittedTabs(std::string& match_output)
    {
        if (!session_) {
            return false;
        }

        bool had_tab = false;
        std::string current;
        current.reserve(line_.size() + 16);

        for (char ch : line_) {
            if (ch != '\t') {
                current.push_back(ch);
                continue;
            }

            had_tab = true;
            auto matches = session_->complete(current);
            if (matches.empty()) {
                continue;
            }

            size_t word_start = current.find_last_of(' ');
            std::string prefix = (word_start == std::string::npos) ? current : current.substr(word_start + 1);
            std::string common = commonPrefix(matches);
            if (common.size() > prefix.size()) {
                current += common.substr(prefix.size());
            }

            bool is_first_word = (word_start == std::string::npos);
            bool single_match = (matches.size() == 1);
            bool ends_with_slash = !matches[0].empty() && matches[0].back() == '/';

            if (single_match && is_first_word && !ends_with_slash) {
                current.push_back(' ');
            } else if (matches.size() > 1) {
                match_output += formatMatches(matches);
            }
        }

        if (!had_tab) {
            return false;
        }

        line_ = std::move(current);
        cursor_ = line_.size();
        return true;
    }

    TerminalEditResult submitLine()
    {
        TerminalEditResult out;

        if (line_.empty()) {
            out.prompt_on_new_line = true;
            return out;
        }

        if (mode_ == Mode::COOKED) {
            std::string match_output;
            if (applySubmittedTabs(match_output)) {
                out.output = std::move(match_output);
                out.prompt_on_new_line = true;
                return out;
            }
        }

        std::string line = std::move(line_);
        clearLine();

        std::string result = session_ ? session_->execute(line) : std::string{};
        if (!result.empty() && result[0] == '\x04') {
            out.output = "bye\n";
            prefixOutputIfControlled(out.output);
            out.disconnect = true;
            return out;
        }

        if (!result.empty()) {
            out.output = std::move(result);
            if (out.output.back() != '\n') {
                out.output.push_back('\n');
            }
            prefixOutputIfControlled(out.output);
        }

        out.prompt_on_new_line = true;
        return out;
    }

private:
    TerminalSession* session_ = nullptr;
    Mode mode_ = Mode::COOKED;
    std::string line_;
    size_t cursor_ = 0;
    int history_index_ = -1;
    std::string history_draft_;
};

} // namespace ve::service
