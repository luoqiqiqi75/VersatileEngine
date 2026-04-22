// terminal_utf8.h — UTF-8 utility functions for terminal input/output
#pragma once

#include <cstdint>
#include <string>

namespace ve::service::utf8 {

// Get the byte length of a UTF-8 character from its leading byte
// Returns 0 for invalid lead bytes
inline size_t charByteLen(unsigned char lead) {
    if ((lead & 0x80) == 0) return 1;      // 0xxxxxxx
    if ((lead & 0xE0) == 0xC0) return 2;   // 110xxxxx
    if ((lead & 0xF0) == 0xE0) return 3;   // 1110xxxx
    if ((lead & 0xF8) == 0xF0) return 4;   // 11110xxx
    return 0;  // Invalid
}

// Get the byte length of the UTF-8 character at position pos in str
// Returns 0 if pos is out of bounds or invalid
inline size_t charByteLen(const std::string& str, size_t pos) {
    if (pos >= str.size()) return 0;
    return charByteLen(static_cast<unsigned char>(str[pos]));
}

// Get the byte length of the previous UTF-8 character before pos
// Returns 0 if pos is 0 or invalid
inline size_t prevCharByteLen(const std::string& str, size_t pos) {
    if (pos == 0 || pos > str.size()) return 0;

    // Scan backwards to find the start of the character
    size_t scan = pos - 1;
    while (scan > 0 && (static_cast<unsigned char>(str[scan]) & 0xC0) == 0x80) {
        --scan;  // Skip continuation bytes (10xxxxxx)
    }

    size_t len = charByteLen(str, scan);
    if (scan + len == pos) {
        return len;
    }
    return 0;  // Invalid sequence
}

// Decode a UTF-8 character to Unicode codepoint
inline uint32_t decodeChar(const std::string& str, size_t pos) {
    if (pos >= str.size()) return 0;

    unsigned char lead = static_cast<unsigned char>(str[pos]);
    size_t len = charByteLen(lead);

    if (len == 0 || pos + len > str.size()) return 0;

    uint32_t codepoint = 0;

    if (len == 1) {
        codepoint = lead;
    } else if (len == 2) {
        codepoint = ((lead & 0x1F) << 6) | (str[pos + 1] & 0x3F);
    } else if (len == 3) {
        codepoint = ((lead & 0x0F) << 12) | ((str[pos + 1] & 0x3F) << 6) | (str[pos + 2] & 0x3F);
    } else if (len == 4) {
        codepoint = ((lead & 0x07) << 18) | ((str[pos + 1] & 0x3F) << 12) |
                    ((str[pos + 2] & 0x3F) << 6) | (str[pos + 3] & 0x3F);
    }

    return codepoint;
}

// Get the display width of a UTF-8 character (1 or 2 columns)
// CJK characters are 2 columns wide
inline int charDisplayWidth(const std::string& str, size_t pos) {
    uint32_t cp = decodeChar(str, pos);
    if (cp == 0) return 0;

    // CJK Unified Ideographs
    if (cp >= 0x4E00 && cp <= 0x9FFF) return 2;
    // CJK Symbols and Punctuation
    if (cp >= 0x3000 && cp <= 0x303F) return 2;
    // Hiragana
    if (cp >= 0x3040 && cp <= 0x309F) return 2;
    // Katakana
    if (cp >= 0x30A0 && cp <= 0x30FF) return 2;
    // Hangul Syllables
    if (cp >= 0xAC00 && cp <= 0xD7AF) return 2;
    // Fullwidth Forms
    if (cp >= 0xFF00 && cp <= 0xFFEF) return 2;

    return 1;
}

// Count the number of UTF-8 characters in a string
inline size_t charCount(const std::string& str) {
    size_t count = 0;
    size_t pos = 0;
    while (pos < str.size()) {
        size_t len = charByteLen(str, pos);
        if (len == 0) break;  // Invalid sequence
        pos += len;
        ++count;
    }
    return count;
}

// Calculate the total display width of a string
inline size_t displayWidth(const std::string& str) {
    size_t width = 0;
    size_t pos = 0;
    while (pos < str.size()) {
        size_t len = charByteLen(str, pos);
        if (len == 0) break;
        width += charDisplayWidth(str, pos);
        pos += len;
    }
    return width;
}

// Calculate display width from byte position 'from' to 'to'
inline size_t displayWidth(const std::string& str, size_t from, size_t to) {
    if (from >= to || from >= str.size()) return 0;
    to = std::min(to, str.size());

    size_t width = 0;
    size_t pos = from;
    while (pos < to) {
        size_t len = charByteLen(str, pos);
        if (len == 0 || pos + len > to) break;
        width += charDisplayWidth(str, pos);
        pos += len;
    }
    return width;
}

} // namespace ve::service::utf8
