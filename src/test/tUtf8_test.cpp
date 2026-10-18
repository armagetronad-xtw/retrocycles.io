#include "doctest.h"
#include "tUtf8.h"

#include <string>

// Tests for tUtf8.h: UTF-8 chat text next to the game's Latin-1

namespace
{
// "안녕" (hangul), "こんにちは" (hiragana), "你好" (Han), "é" as UTF-8, a smiling face
std::string const korean = "\xec\x95\x88\xeb\x85\x95";
std::string const japanese = "\xe3\x81\x93\xe3\x82\x93\xe3\x81\xab\xe3\x81\xa1\xe3\x81\xaf";
std::string const chinese = "\xe4\xbd\xa0\xe5\xa5\xbd";
std::string const eAcute = "\xc3\xa9";
std::string const smile = "\xf0\x9f\x98\x80";
} // namespace

TEST_SUITE("tUtf8")
{
    TEST_CASE("sequences are read only when well formed")
    {
        std::uint32_t cp = 0;
        CHECK(tUtf8Sequence(korean, 0, cp) == 3);
        CHECK(cp == 0xc548);
        CHECK(tUtf8Sequence(smile, 0, cp) == 4);
        CHECK(cp == 0x1f600);
        CHECK(tUtf8Sequence("a", 0, cp) == 0);              // ASCII is a byte
        CHECK(tUtf8Sequence("\xe9t\xe9", 0, cp) == 0);       // Latin-1 "été" is bytes
        CHECK(tUtf8Sequence("\xc0\xaf", 0, cp) == 0);        // overlong
        CHECK(tUtf8Sequence("\xed\xa0\x80", 0, cp) == 0);    // a surrogate
        CHECK(tUtf8Sequence(korean.substr(0, 2), 0, cp) == 0); // cut short
    }

    TEST_CASE("length and cut count characters, never splitting one")
    {
        CHECK(tUtf8Length(korean) == 2);
        CHECK(tUtf8Length("gg " + korean) == 5);
        CHECK(tUtf8Length("\xe9t\xe9") == 3); // Latin-1 counts bytes, as before
        CHECK(tUtf8Cut("gg " + korean, 4) == "gg " + korean.substr(0, 3));
        CHECK(tUtf8Cut(smile + "x", 1) == smile);
        CHECK(tUtf8Cut("abc", 10) == "abc");
    }

    TEST_CASE("the net filter keeps UTF-8 whole and filters bytes as NetFilter does")
    {
        // the old filter turns 0x80-0x9f into '_', which is half of UTF-8's continuation bytes
        CHECK(tUtf8NetFilter(korean + " " + japanese + " " + chinese) == korean + " " + japanese + " " + chinese);
        CHECK(tUtf8NetFilter(eAcute + smile) == eAcute + smile);
        CHECK(tUtf8NetFilter("a\tb\nc") == "a b c");
        CHECK(tUtf8NetFilter("\x01x\x85y") == "_x_y");
        CHECK(tUtf8NetFilter("caf\xe9") == "caf\xe9"); // a Latin-1 byte stays
        // C1 controls and bidirectional overrides are filtered even when well formed
        CHECK(tUtf8NetFilter("\xc2\x85") == "_");
        CHECK(tUtf8NetFilter("a\xe2\x80\xae" "b") == "a_b");
        CHECK(tUtf8NetFilter("a\xe2\x81\xa6" "b") == "a_b");
    }

    TEST_CASE("wide characters and their script")
    {
        CHECK_FALSE(tUtf8HasWide("plain"));
        CHECK_FALSE(tUtf8HasWide(eAcute));
        CHECK(tUtf8HasWide(korean));
        CHECK(tUtf8ScriptOf("gg") == tUtf8Script_None);
        CHECK(tUtf8ScriptOf(eAcute) == tUtf8Script_None);
        CHECK(tUtf8ScriptOf(smile) == tUtf8Script_Other);
        CHECK(tUtf8ScriptOf(korean) == tUtf8Script_Korean);
        CHECK(tUtf8ScriptOf(japanese) == tUtf8Script_Japanese);
        CHECK(tUtf8ScriptOf(chinese) == tUtf8Script_Chinese);
        // kana makes Han Japanese, hangul wins over both
        CHECK(tUtf8ScriptOf(chinese + japanese) == tUtf8Script_Japanese);
        CHECK(tUtf8ScriptOf(chinese + korean) == tUtf8Script_Korean);
    }

    TEST_CASE("folding to Latin-1")
    {
        CHECK(tUtf8ToLatin1("caf" + eAcute) == "caf\xe9");
        CHECK(tUtf8ToLatin1("caf\xe9") == "caf\xe9");
        CHECK(tUtf8ToLatin1("hi " + smile) == "hi ?");
        CHECK(tUtf8ToLatin1(korean) == "??");
    }

    TEST_CASE("a line folds each CJK stretch into its word")
    {
        auto fold = [](std::string const& s) { return tUtf8FoldRuns(s, "[Korean]", "[Japanese]", "[Chinese]"); };
        CHECK(fold("* Cycler42 " + korean + " " + korean + " *") == "* Cycler42 [Korean] *");
        CHECK(fold("a " + japanese + ", b") == "a [Japanese], b");
        CHECK(fold(chinese + "\xe3\x80\x82" + " ok") == "[Chinese] ok"); // CJK full stop joins
        CHECK(fold("caf" + eAcute + " " + smile) == "caf\xe9 ?");
        CHECK(fold("plain") == "plain");
    }
}
