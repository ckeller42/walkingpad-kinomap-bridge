// The keyboard grid. See keyboard_layout.h.
#include "keyboard_layout.h"

namespace devmenu {
namespace {

using Kind = Key::Kind;

// Characters per row; a page row is cols 0..9 except where noted below.
// Letters: row 2 holds shift, then these seven, then 123 and SP.
const char* const kLetters[kKeyRows] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
const char* const kLettersUp[kKeyRows] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
const char* const kSym1[kKeyRows] = {"1234567890", "!@#$%^&*(", "-_=+:/?"};
const char* const kSym2[kKeyRows] = {"~`|\\<>[]{}", ".,;'\")", ""};

// One-character strings with static storage, so a Key's label can point at them.
const char* glyph(char c) {
    static char table[128][2];
    const uint8_t i = (uint8_t)c & 0x7F;
    table[i][0] = (char)i;
    return table[i];
}

Key ch(char c) { return Key{c, glyph(c), Kind::Char}; }
Key special(const char* label, Kind kind) { return Key{0, label, kind}; }

}  // namespace

Key keyAt(Page page, bool shift, uint8_t row, uint8_t col) {
    if (row >= kKeyRows || col >= kKeyCols) return special("", Kind::Blank);

    // The right-hand column is the same on every page.
    if (col == kKeyCols - 1 && row == 1) return special("DEL", Kind::Del);
    if (col == kKeyCols - 1 && row == 2) return special("SP", Kind::Space);

    const char* const* chars =
        page == Page::Letters ? (shift ? kLettersUp : kLetters)
                              : (page == Page::Symbols1 ? kSym1 : kSym2);

    if (row == 2) {
        if (col == 0) {
            switch (page) {
                case Page::Letters: return special(shift ? "SHF" : "shf", Kind::Shift);
                case Page::Symbols1: return special("#+=", Kind::ToSymbols2);
                case Page::Symbols2: return special("123", Kind::ToSymbols1);
            }
        }
        if (col == kKeyCols - 2) {
            return page == Page::Letters ? special("123", Kind::ToSymbols1)
                                         : special("ABC", Kind::ToLetters);
        }
        // Columns 1..7 hold the third row's characters.
        if (page == Page::Symbols2) return special("", Kind::Blank);
        return ch(chars[2][col - 1]);
    }

    // Rows 0 and 1: characters up to the end of the string, blanks after.
    const char* s = chars[row];
    for (uint8_t i = 0; i < col; ++i) {
        if (!s[i]) return special("", Kind::Blank);
    }
    return s[col] ? ch(s[col]) : special("", Kind::Blank);
}

}  // namespace devmenu
