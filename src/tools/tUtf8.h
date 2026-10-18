/*
SPDX-License-Identifier: GPL-2.0-or-later

This file is part of the browser and platform additions to Armagetron Advanced.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

// tUtf8.h: UTF-8 chat text next to the game's Latin-1. The game's strings are
// Latin-1 bytes; browser clients in official rooms send chat as UTF-8 so Korean, Chinese and
// Japanese survive. These helpers keep such text intact through the server's chat path,
// count and cut it by characters, and fold it back to Latin-1 for clients that cannot show
// it. Header only, no state: the server's chat code (ePlayer.cpp) uses it, and on the main
// branch so does the browser client's API (gBrowserApi.cpp).
//
// A string here is bytes. Where they form a valid UTF-8 sequence for a code point above
// U+007F they are read as that code point; any other byte is a byte (Latin-1). That reading
// is what lets a Latin-1 name from a desktop client and a UTF-8 message from a browser sit in
// one line.

#ifndef ArmageTron_TUTF8_H
#define ArmageTron_TUTF8_H

#include <cstddef>
#include <cstdint>
#include <string>

//! the length (2 to 4) of the valid UTF-8 sequence at text[at] and its code point, or 0 if
//! there is none (ASCII, a stray byte, an overlong form, a surrogate, past U+10FFFF)
inline int tUtf8Sequence( std::string const & text, std::size_t at, std::uint32_t & codePoint )
{
    auto const byte = [&]( std::size_t i ) { return static_cast< unsigned char >( text[i] ); };
    unsigned char const lead = byte( at );
    int length = 0;
    std::uint32_t cp = 0;
    if ( lead >= 0xc2 && lead <= 0xdf )
        length = 2, cp = lead & 0x1f;
    else if ( lead >= 0xe0 && lead <= 0xef )
        length = 3, cp = lead & 0x0f;
    else if ( lead >= 0xf0 && lead <= 0xf4 )
        length = 4, cp = lead & 0x07;
    else
        return 0;
    if ( at + length > text.size() )
        return 0;
    for ( int k = 1; k < length; ++k )
    {
        if ( ( byte( at + k ) & 0xc0 ) != 0x80 )
            return 0;
        cp = ( cp << 6 ) | ( byte( at + k ) & 0x3f );
    }
    if ( ( length == 3 && ( cp < 0x800 || ( cp >= 0xd800 && cp <= 0xdfff ) ) ) || ( length == 4 && ( cp < 0x10000 || cp > 0x10ffff ) ) )
        return 0;
    codePoint = cp;
    return length;
}

//! characters, a UTF-8 sequence counting as one (the chat's length limits count these)
inline std::size_t tUtf8Length( std::string const & text )
{
    std::size_t count = 0;
    for ( std::size_t i = 0; i < text.size(); ++count )
    {
        std::uint32_t cp;
        int const length = tUtf8Sequence( text, i, cp );
        i += length ? length : 1;
    }
    return count;
}

//! the first maxChars characters, never cutting a sequence in two
inline std::string tUtf8Cut( std::string const & text, std::size_t maxChars )
{
    std::size_t i = 0;
    for ( std::size_t count = 0; i < text.size() && count < maxChars; ++count )
    {
        std::uint32_t cp;
        int const length = tUtf8Sequence( text, i, cp );
        i += length ? length : 1;
    }
    return text.substr( 0, i );
}

//! whether the text has a character the game's Latin-1 cannot hold (above U+00FF)
inline bool tUtf8HasWide( std::string const & text )
{
    for ( std::size_t i = 0; i < text.size(); )
    {
        std::uint32_t cp = 0;
        int const length = tUtf8Sequence( text, i, cp );
        if ( length && cp > 0xff )
            return true;
        i += length ? length : 1;
    }
    return false;
}

//! code points that are kept out of chat even when well formed: C1 controls, the line and
//! paragraph separators, and the bidirectional overrides and isolates (which can make a line
//! read as something it is not)
inline bool tUtf8Unwanted( std::uint32_t cp )
{
    return ( cp >= 0x80 && cp <= 0x9f ) || cp == 0x2028 || cp == 0x2029 || ( cp >= 0x202a && cp <= 0x202e ) || ( cp >= 0x2066 && cp <= 0x2069 );
}

//! the server's filter for a chat line from a UTF-8 client (tString::NetFilter's rules for
//! single bytes: printable ASCII and the visible Latin-1 half stay, tab and newline become
//! blanks, anything else '_'), except that well formed UTF-8 sequences stay whole
inline std::string tUtf8NetFilter( std::string const & text )
{
    std::string out;
    out.reserve( text.size() );
    for ( std::size_t i = 0; i < text.size(); )
    {
        std::uint32_t cp = 0;
        int const length = tUtf8Sequence( text, i, cp );
        if ( length )
        {
            if ( tUtf8Unwanted( cp ) )
                out += '_';
            else
                out.append( text, i, length );
            i += length;
            continue;
        }
        unsigned char const c = static_cast< unsigned char >( text[i++] );
        if ( c == 0 )
            break;
        if ( c == '\n' || c == '\t' || c == 0xa0 )
            out += ' ';
        else if ( ( c >= 32 && c <= 126 ) || c >= 161 )
            out += static_cast< char >( c );
        else
            out += '_';
    }
    return out;
}

//! the scripts a wide message is named by when it is folded for a Latin-1 client
enum tUtf8Script
{
    tUtf8Script_None,     //!< nothing above U+00FF
    tUtf8Script_Other,    //!< wide characters, none of them CJK (emoji, Cyrillic, ...)
    tUtf8Script_Chinese,  //!< Han ideographs without kana or hangul
    tUtf8Script_Japanese, //!< kana (with or without Han)
    tUtf8Script_Korean    //!< hangul
};

//! which script names a message: hangul over kana over Han; Other when it has wide
//! characters but none of those
inline tUtf8Script tUtf8ScriptOf( std::string const & text )
{
    bool wide = false, han = false, kana = false, hangul = false;
    for ( std::size_t i = 0; i < text.size(); )
    {
        std::uint32_t cp = 0;
        int const length = tUtf8Sequence( text, i, cp );
        i += length ? length : 1;
        if ( !length || cp <= 0xff )
            continue;
        wide = true;
        if ( ( cp >= 0xac00 && cp <= 0xd7af ) || ( cp >= 0x1100 && cp <= 0x11ff ) || ( cp >= 0x3130 && cp <= 0x318f ) )
            hangul = true;
        else if ( ( cp >= 0x3040 && cp <= 0x30ff ) || ( cp >= 0x31f0 && cp <= 0x31ff ) || ( cp >= 0xff66 && cp <= 0xff9f ) )
            kana = true;
        else if ( ( cp >= 0x4e00 && cp <= 0x9fff ) || ( cp >= 0x3400 && cp <= 0x4dbf ) || ( cp >= 0xf900 && cp <= 0xfaff ) || ( cp >= 0x20000 && cp <= 0x2ffff ) )
            han = true;
    }
    if ( hangul )
        return tUtf8Script_Korean;
    if ( kana )
        return tUtf8Script_Japanese;
    if ( han )
        return tUtf8Script_Chinese;
    return wide ? tUtf8Script_Other : tUtf8Script_None;
}

//! the text in Latin-1: a UTF-8 sequence up to U+00FF becomes its byte, a wider one '?';
//! other bytes stay as they are
inline std::string tUtf8ToLatin1( std::string const & text )
{
    std::string out;
    out.reserve( text.size() );
    for ( std::size_t i = 0; i < text.size(); )
    {
        std::uint32_t cp = 0;
        int const length = tUtf8Sequence( text, i, cp );
        if ( length )
        {
            out += cp <= 0xff ? static_cast< char >( cp ) : '?';
            i += length;
        }
        else
            out += text[i++];
    }
    return out;
}

//! a whole line (a /me, an announcement) for a Latin-1 client: each stretch of CJK, with the
//! blanks and punctuation inside it, becomes "[Korean]" / "[Japanese]" / "[Chinese]"
//! (the words given), other wide characters '?'
inline std::string tUtf8FoldRuns( std::string const & text, std::string const & korean, std::string const & japanese, std::string const & chinese )
{
    std::string out;
    std::string run;      // the raw bytes of the CJK stretch being read
    std::string pending;  // blanks and punctuation after it, kept if the stretch ends there
    auto flush = [&]() {
        if ( !run.empty() )
        {
            tUtf8Script const script = tUtf8ScriptOf( run );
            out += script == tUtf8Script_Korean ? korean : script == tUtf8Script_Japanese ? japanese : chinese;
            run.clear();
        }
        out += tUtf8ToLatin1( pending );
        pending.clear();
    };
    for ( std::size_t i = 0; i < text.size(); )
    {
        std::uint32_t cp = 0;
        int const length = tUtf8Sequence( text, i, cp );
        std::string const piece = text.substr( i, length ? length : 1 );
        i += piece.size();
        bool const cjk = length && cp > 0xff && tUtf8ScriptOf( piece ) > tUtf8Script_Other;
        // CJK punctuation and full-width forms join the stretch around them
        bool const joins = length && ( ( cp >= 0x3000 && cp <= 0x303f ) || ( cp >= 0xff00 && cp <= 0xffef ) );
        if ( cjk || ( joins && !run.empty() ) )
        {
            run += pending + piece;
            pending.clear();
        }
        else if ( !run.empty() && piece.size() == 1 && ( piece[0] == ' ' || std::string( ",.!?:;-" ).find( piece[0] ) != std::string::npos ) )
            pending += piece;
        else
        {
            flush();
            out += tUtf8ToLatin1( piece );
        }
    }
    flush();
    return out;
}

#endif // ArmageTron_TUTF8_H
