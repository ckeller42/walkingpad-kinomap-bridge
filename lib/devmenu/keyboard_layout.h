#pragma once
/**
 * @file keyboard_layout.h
 * @brief The on-screen keyboard's key grid, as data.
 */
// The grid the Wi-Fi password keyboard draws and hit-tests. Both the touch
// handler and the renderer ask keyAt(), so what is drawn is what is hit.
// Arduino-free and unit-tested: a host test walks every key and proves every
// printable ASCII character can be typed.

#include <stdint.h>

namespace devmenu {

/** @brief Which keyboard page is showing. */
enum class Page : uint8_t {
    Letters,   ///< a-z (A-Z with shift).
    Symbols1,  ///< Digits and the common symbols.
    Symbols2,  ///< The remaining punctuation.
};

constexpr uint8_t kKeyRows = 3;   ///< Rows in the grid.
constexpr uint8_t kKeyCols = 10;  ///< Columns in the grid.

/** @brief One cell of the grid. */
struct Key {
    /** @brief What pressing the cell does. */
    enum class Kind : uint8_t {
        Char,        ///< Types @ref ch.
        Blank,       ///< Empty cell; drawn as a key, does nothing.
        Del,         ///< Backspace.
        Shift,       ///< Shift the next letter.
        Space,       ///< Types a space.
        ToLetters,   ///< Go to Page::Letters.
        ToSymbols1,  ///< Go to Page::Symbols1.
        ToSymbols2,  ///< Go to Page::Symbols2.
    };

    char ch;            ///< The character typed; meaningful for Kind::Char only.
    const char* label;  ///< What to draw. Static storage, never freed.
    Kind kind;          ///< What the cell does.
};

/**
 * @brief The key at one cell of the grid.
 *
 * The single source for both drawing and hit-testing.
 *
 * @param page  Which page is showing.
 * @param shift Whether shift is pending; it uppercases letters and nothing else.
 * @param row   Zero-based row, below @ref kKeyRows.
 * @param col   Zero-based column, below @ref kKeyCols.
 * @return The cell, or a Key::Kind::Blank when out of range.
 */
Key keyAt(Page page, bool shift, uint8_t row, uint8_t col);

}  // namespace devmenu
