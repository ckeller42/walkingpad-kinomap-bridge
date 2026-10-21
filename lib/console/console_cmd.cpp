#include "console_cmd.h"

#include <ctype.h>
#include <string.h>

namespace console {

namespace {
// Lower-cases and collapses whitespace so "  SIM   On " == "sim on".
size_t normalise(const char* in, char* out, size_t cap) {
    size_t n = 0;
    bool pendingSpace = false;
    for (; *in; ++in) {
        if (isspace((unsigned char)*in)) { pendingSpace = n > 0; continue; }
        if (pendingSpace && n + 1 < cap) out[n++] = ' ';
        pendingSpace = false;
        if (n + 1 < cap) out[n++] = (char)tolower((unsigned char)*in);
    }
    out[n] = 0;
    return n;
}
}  // namespace

Cmd parse(const char* line) {
    char s[LineBuffer::kMax + 1];
    if (normalise(line, s, sizeof s) == 0) return Cmd::Empty;
    struct Entry { const char* text; Cmd cmd; };
    static const Entry kTable[] = {
        {"status", Cmd::Status},   {"arm", Cmd::Arm},         {"disarm", Cmd::Disarm},
        {"start", Cmd::Start},     {"stop", Cmd::Stop},       {"sim on", Cmd::SimOn},
        {"sim off", Cmd::SimOff},  {"wizard", Cmd::Wizard},   {"wizard next", Cmd::WizardNext},
        {"wizard back", Cmd::WizardBack}, {"shot", Cmd::Shot}, {"help", Cmd::Help},
    };
    for (const Entry& e : kTable)
        if (strcmp(s, e.text) == 0) return e.cmd;
    return Cmd::Unknown;
}

bool LineBuffer::feed(char c) {
    if (done_) { n_ = 0; over_ = false; done_ = false; buf_[0] = 0; }
    if (c == '\r') return false;
    if (c == '\n') {
        buf_[n_] = 0;
        done_ = true;
        return true;
    }
    if (n_ < kMax) buf_[n_++] = c;
    else over_ = true;
    return false;
}

}  // namespace console
