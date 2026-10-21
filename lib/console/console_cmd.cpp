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
        {"wizard back", Cmd::WizardBack}, {"cp", Cmd::Cp}, {"shot", Cmd::Shot}, {"help", Cmd::Help},
    };
    for (const Entry& e : kTable)
        if (strcmp(s, e.text) == 0) return e.cmd;
    if (strcmp(s, "speed up") == 0) return Cmd::SpeedUp;
    if (strcmp(s, "speed down") == 0) return Cmd::SpeedDown;
    if (strncmp(s, "speed ", 6) == 0) return Cmd::SpeedSet;
    return Cmd::Unknown;
}

SpeedArg parseSpeedArg(const char* line, unsigned minCenti, unsigned maxCenti, unsigned char& tenthKmh) {
    char s[LineBuffer::kMax + 1];
    normalise(line, s, sizeof s);
    if (strncmp(s, "speed ", 6) != 0) return SpeedArg::Bad;
    const char* p = s + 6;
    unsigned whole = 0, frac = 0, fracDigits = 0, wholeDigits = 0;
    for (; isdigit((unsigned char)*p); ++p) {
        if (++wholeDigits > 3) return SpeedArg::Bad;
        whole = whole * 10 + (unsigned)(*p - '0');
    }
    if (wholeDigits == 0) return SpeedArg::Bad;
    if (*p == '.') {
        for (++p; isdigit((unsigned char)*p); ++p) {
            if (++fracDigits > 2) return SpeedArg::Bad;
            frac = frac * 10 + (unsigned)(*p - '0');
        }
        if (fracDigits == 0) return SpeedArg::Bad;
        if (fracDigits == 1) frac *= 10;
    }
    if (*p != 0) return SpeedArg::Bad;
    const unsigned centi = whole * 100 + frac;
    if (centi % 10 != 0) return SpeedArg::Bad;
    if (centi < minCenti || centi > maxCenti) return SpeedArg::OutOfRange;
    tenthKmh = (unsigned char)(centi / 10);
    return SpeedArg::Ok;
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
