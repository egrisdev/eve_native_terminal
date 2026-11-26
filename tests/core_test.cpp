// Unit tests for src/core.h. Runs natively on any OS:
//   mkdir -p build && g++ -std=c++17 -Wall -Wextra -Werror -Isrc tests/core_test.cpp -o build/core_test && build/core_test
#include "core.h"

#include <cstdio>
#include <initializer_list>
#include <string>

using evewt::TelnetProtocol;
using evewt::Target;
namespace tn = evewt::telnet;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

static std::string bytes(std::initializer_list<int> list) {
    std::string out;
    for (int b : list) out.push_back(static_cast<char>(b));
    return out;
}

static std::string hex(const std::string& s) {
    std::string out;
    char buf[4];
    for (unsigned char c : s) {
        std::snprintf(buf, sizeof buf, "%02X ", c);
        out += buf;
    }
    return out;
}

#define CHECK_BYTES(actual, expected)                                         \
    do {                                                                      \
        const std::string a_ = (actual), e_ = (expected);                     \
        if (a_ != e_) {                                                       \
            std::printf("FAIL %s:%d  %s\n   got:  %s\n   want: %s\n", __FILE__, \
                        __LINE__, #actual, hex(a_).c_str(), hex(e_).c_str()); \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

// Feeds server bytes and returns {screen, reply}.
struct Fed {
    std::string screen, reply;
};
static Fed feed(TelnetProtocol& p, const std::string& in) {
    Fed f;
    p.receive(in.data(), in.size(), f.screen, f.reply);
    return f;
}

// ------------------------------------------------------------------ URL

static void testUrls() {
    Target t;
    CHECK(evewt::parseTelnetUrl(L"telnet://eve.local:32769/", t));
    CHECK(t.host == L"eve.local" && t.port == 32769);

    CHECK(evewt::parseTelnetUrl(L"TELNET://10.0.0.5:2001", t));
    CHECK(t.host == L"10.0.0.5" && t.port == 2001);

    CHECK(evewt::parseTelnetUrl(L"telnet://eve.local", t));
    CHECK(t.host == L"eve.local" && t.port == 23);

    CHECK(evewt::parseTelnetUrl(L"telnet://eve.local:/", t));
    CHECK(t.port == 23);

    CHECK(evewt::parseTelnetUrl(L"telnet://[fe80::1]:32770/", t));
    CHECK(t.host == L"fe80::1" && t.port == 32770);

    CHECK(evewt::parseTelnetUrl(L"telnet://eve.local:32769?x#y", t));
    CHECK(t.port == 32769);

    // Anything that could smuggle options or wt.exe commands is refused.
    const wchar_t* bad[] = {
        L"",
        L"http://eve.local:23/",
        L"telnet:eve.local:23",
        L"telnet://",
        L"telnet://:23",
        L"telnet://-batch:23",
        L"telnet://.eve:23",
        L"telnet://eve;calc:23",
        L"telnet://eve local:23",
        L"telnet://eve\"x:23",
        L"telnet://eve%20x:23",
        L"telnet://user@eve.local:23",
        L"telnet://eve.local:0",
        L"telnet://eve.local:65536",
        L"telnet://eve.local:123456",
        L"telnet://eve.local:23a",
        L"telnet://eve.local:-1",
        L"telnet://[fe80::1:23",
        L"telnet://[fe80::1]x",
        L"telnet://[]:23",
        L"telnet://[g::1]:23",
        L"telnet://\x00e9ve:23",
    };
    for (const wchar_t* url : bad) {
        Target ignored;
        if (evewt::parseTelnetUrl(url, ignored)) {
            std::printf("FAIL accepted bad URL: %ls\n", url);
            ++failures;
        }
    }

    unsigned port = 0;
    CHECK(evewt::parsePort(L"1", port) && port == 1);
    CHECK(evewt::parsePort(L"65535", port) && port == 65535);
    CHECK(!evewt::parsePort(L"", port));
    CHECK(!evewt::parsePort(L"0", port));
    CHECK(!evewt::parsePort(L"99999", port));

    CHECK(evewt::isValidHost(L"R1-core.lab"));
    CHECK(evewt::isValidHost(L"::1"));
    CHECK(!evewt::isValidHost(L"-n"));
    CHECK(!evewt::isValidHost(L"a;b"));
}

// ------------------------------------------------------------------ telnet

static void testStartRequestsLikePlink() {
    TelnetProtocol p;
    std::string out;
    p.start(out);
    CHECK_BYTES(out, bytes({tn::IAC, tn::WILL, tn::TTYPE,
                            tn::IAC, tn::DO, tn::ECHO,
                            tn::IAC, tn::WILL, tn::SGA,
                            tn::IAC, tn::DO, tn::SGA}));
}

static void testPlainDataAndEscapedIac() {
    TelnetProtocol p;
    Fed f = feed(p, "R1>" + bytes({tn::IAC, tn::IAC}) + "x");
    CHECK_BYTES(f.screen, "R1>" + bytes({0xFF}) + "x");
    CHECK(f.reply.empty());
}

static void testAnswersToOurRequestsAreNotReAcknowledged() {
    TelnetProtocol p;
    std::string ignored;
    p.start(ignored);
    // QEMU/IOL-style greeting: server agrees to echo and SGA, asks for NAWS.
    Fed f = feed(p, bytes({tn::IAC, tn::WILL, tn::ECHO,
                           tn::IAC, tn::WILL, tn::SGA,
                           tn::IAC, tn::DO, tn::SGA}));
    CHECK(f.reply.empty());
    CHECK(p.remoteEnabled(tn::ECHO));
    CHECK(p.remoteEnabled(tn::SGA));
    CHECK(p.localEnabled(tn::SGA));
}

static void testUnrequestedOptionsFromServer() {
    TelnetProtocol p;
    // Accept the server echoing; refuse LINEMODE (34) and an unknown option.
    Fed f = feed(p, bytes({tn::IAC, tn::WILL, tn::ECHO,
                           tn::IAC, tn::WILL, 34,
                           tn::IAC, tn::DO, 99}));
    CHECK_BYTES(f.reply, bytes({tn::IAC, tn::DO, tn::ECHO,
                                tn::IAC, tn::DONT, 34,
                                tn::IAC, tn::WONT, 99}));
    CHECK(p.remoteEnabled(tn::ECHO));
    // Repeating WILL ECHO once enabled must not start a loop.
    CHECK(feed(p, bytes({tn::IAC, tn::WILL, tn::ECHO})).reply.empty());
}

static void testDisableEnabledOption() {
    TelnetProtocol p;
    feed(p, bytes({tn::IAC, tn::WILL, tn::ECHO}));
    Fed f = feed(p, bytes({tn::IAC, tn::WONT, tn::ECHO}));
    CHECK_BYTES(f.reply, bytes({tn::IAC, tn::DONT, tn::ECHO}));
    CHECK(!p.remoteEnabled(tn::ECHO));
    // Already off: no reply.
    CHECK(feed(p, bytes({tn::IAC, tn::WONT, tn::ECHO})).reply.empty());
}

static void testRefusalOfOurRequestIsSilent() {
    TelnetProtocol p;
    std::string ignored;
    p.start(ignored);
    Fed f = feed(p, bytes({tn::IAC, tn::DONT, tn::TTYPE, tn::IAC, tn::WONT, tn::ECHO}));
    CHECK(f.reply.empty());
    CHECK(!p.localEnabled(tn::TTYPE));
    CHECK(!p.remoteEnabled(tn::ECHO));
}

// EVE-NG consoles are serial lines with no window size, and a window-size
// report that reaches the device as data reads as keystrokes: 120x30 is
// "x" plus Ctrl+^ (IOS's escape key), which prints "% No open connections".
static void testWindowSizeIsRefused() {
    TelnetProtocol p;
    std::string out;
    p.start(out);
    CHECK(out.find(static_cast<char>(tn::NAWS)) == std::string::npos);
    Fed f = feed(p, bytes({tn::IAC, tn::DO, tn::NAWS}));
    CHECK_BYTES(f.reply, bytes({tn::IAC, tn::WONT, tn::NAWS}));
    CHECK(!p.localEnabled(tn::NAWS));
}

static void testTerminalType() {
    TelnetProtocol p("XTERM-256COLOR");
    std::string ignored;
    p.start(ignored);
    feed(p, bytes({tn::IAC, tn::DO, tn::TTYPE}));
    Fed f = feed(p, bytes({tn::IAC, tn::SB, tn::TTYPE, tn::TTYPE_SEND, tn::IAC, tn::SE}));
    CHECK_BYTES(f.reply, bytes({tn::IAC, tn::SB, tn::TTYPE, tn::TTYPE_IS}) + "XTERM-256COLOR" +
                             bytes({tn::IAC, tn::SE}));

    // Not agreed: SEND is ignored.
    TelnetProtocol q;
    CHECK(feed(q, bytes({tn::IAC, tn::SB, tn::TTYPE, tn::TTYPE_SEND, tn::IAC, tn::SE})).reply.empty());
}

static void testSequencesSplitAcrossReads() {
    TelnetProtocol p;
    std::string screen, reply;
    const std::string all = "ab" + bytes({tn::IAC, tn::WILL, tn::ECHO}) + "cd" +
                            bytes({tn::IAC, tn::SB, 99, 1, tn::IAC, tn::IAC, 2, tn::IAC, tn::SE}) + "ef";
    for (char c : all) p.receive(&c, 1, screen, reply);
    CHECK_BYTES(screen, "abcdef");
    CHECK_BYTES(reply, bytes({tn::IAC, tn::DO, tn::ECHO}));
}

static void testCrNulBecomesCr() {
    TelnetProtocol p;
    Fed f = feed(p, "a\r" + bytes({0}) + "b\r\nc" + bytes({0}));
    CHECK_BYTES(f.screen, "a\rb\r\nc" + bytes({0}));

    // CR at the end of one read, NUL at the start of the next.
    std::string screen, reply;
    p.receive("x\r", 2, screen, reply);
    const char nul = 0;
    p.receive(&nul, 1, screen, reply);
    CHECK_BYTES(screen, "x\r");
}

static void testOtherCommandsAreDropped() {
    TelnetProtocol p;
    Fed f = feed(p, "a" + bytes({tn::IAC, tn::NOP, tn::IAC, 249 /*GA*/, tn::IAC, 242 /*DM*/}) + "b");
    CHECK_BYTES(f.screen, "ab");
    CHECK(f.reply.empty());
}

static void testMalformedSubnegotiationEnds() {
    TelnetProtocol p;
    // SB interrupted by IAC WILL ECHO: the WILL is still processed.
    Fed f = feed(p, bytes({tn::IAC, tn::SB, tn::TTYPE, 1, tn::IAC, tn::WILL, tn::ECHO}) + "z");
    CHECK_BYTES(f.screen, "z");
    CHECK_BYTES(f.reply, bytes({tn::IAC, tn::DO, tn::ECHO}));
}

static void testKeyboardEncoding() {
    TelnetProtocol p;
    std::string out;
    const std::string keys = "sh run\r" + bytes({0xFF, 0x1B}) + "[A";
    p.encodeKeyboard(keys.data(), keys.size(), out);
    CHECK_BYTES(out, "sh run\r" + bytes({0, 0xFF, 0xFF, 0x1B}) + "[A");

    // Once we send in binary mode, CR goes out bare (same as plink).
    feed(p, bytes({tn::IAC, tn::DO, tn::BINARY}));
    out.clear();
    p.encodeKeyboard("\r", 1, out);
    CHECK_BYTES(out, "\r");
}

static void testBinaryFromServerKeepsNul() {
    TelnetProtocol p;
    Fed f = feed(p, bytes({tn::IAC, tn::WILL, tn::BINARY}));
    CHECK_BYTES(f.reply, bytes({tn::IAC, tn::DO, tn::BINARY}));
    f = feed(p, "\r" + bytes({0}));
    CHECK_BYTES(f.screen, "\r" + bytes({0}));
}

static void testCompleteUtf8Prefix() {
    using evewt::completeUtf8Prefix;
    CHECK(completeUtf8Prefix("") == 0);
    CHECK(completeUtf8Prefix("R1#") == 3);
    const std::string box = bytes({0xE2, 0x94, 0x80});  // U+2500 box drawing
    CHECK(completeUtf8Prefix("a" + box) == 4);
    CHECK(completeUtf8Prefix("a" + box.substr(0, 1)) == 1);  // lead byte only: hold it
    CHECK(completeUtf8Prefix("a" + box.substr(0, 2)) == 1);  // lead + 1 of 2 continuations
    const std::string emoji = bytes({0xF0, 0x9F, 0x98, 0x80});
    CHECK(completeUtf8Prefix(emoji) == 4);
    CHECK(completeUtf8Prefix("x" + emoji.substr(0, 3)) == 1);
    CHECK(completeUtf8Prefix(bytes({0xC3})) == 0);
    // Not UTF-8 at all: never hold back more than an apparent lead byte.
    CHECK(completeUtf8Prefix(bytes({'a', 0x80, 0x80, 0x80, 0x80})) == 5);
    CHECK(completeUtf8Prefix(bytes({0xE9, 'x'})) == 2);  // Latin-1 e-acute then ASCII
}

static std::string scanTitle(std::initializer_list<std::string> chunks) {
    evewt::TitleScanner scanner;
    for (const std::string& chunk : chunks)
        if (scanner.feed(chunk.data(), chunk.size())) break;
    return scanner.found() ? scanner.title() : "<none>";
}

static void testTitleScanner() {
    CHECK(scanTitle({"\x1b]0;R1\x07"}) == "R1");
    CHECK(scanTitle({"\x1b]2;SW1\x1b\\"}) == "SW1");                    // ST terminator
    CHECK(scanTitle({"\x1b[2J\x1b[H", "banner\r\n\x1b]0;R3\x07"}) == "R3");  // after other sequences
    CHECK(scanTitle({"\x1b]", "0;R", "1", "0\x07"}) == "R10");           // split across reads
    CHECK(scanTitle({"\x1b]1;icon\x07\x1b]0;R2\x07"}) == "R2");          // icon-name OSC ignored
    CHECK(scanTitle({"\x1b]0;  R4 \x07"}) == "R4");                      // trimmed
    CHECK(scanTitle({"\x1b]0;\x07", "\x1b]0;R5\x07"}) == "R5");          // empty title skipped
    CHECK(scanTitle({"\x1b]0;R\x01" "6\x07"}) == "R6");                  // control bytes dropped
    CHECK(scanTitle({"Router>"}) == "<none>");
    CHECK(scanTitle({"\x1b]0;unterminated"}) == "<none>");
    CHECK(scanTitle({"\x1b]0;a\x1b[0m\x1b]0;R7\x07"}) == "R7");          // broken OSC, then a good one
    CHECK(scanTitle({std::string("\x1b]0;") + std::string(300, 'x') + "\x07"}).size() == 256);
}

static void testNaturalCompare() {
    using evewt::naturalCompare;
    CHECK(naturalCompare("R2", "R10") < 0);
    CHECK(naturalCompare("R10", "R9") > 0);
    CHECK(naturalCompare("r1", "R1") == 0);
    CHECK(naturalCompare("R01", "R1") == 0);
    CHECK(naturalCompare("R1", "SW1") < 0);
    CHECK(naturalCompare("R", "R1") < 0);
    CHECK(naturalCompare("SW10", "SW2") > 0);
    CHECK(naturalCompare("a2b", "a10a") < 0);
    CHECK(naturalCompare("Core-R2", "Core-R12") < 0);
    CHECK(naturalCompare("", "") == 0);
}

int main() {
    testUrls();
    testCompleteUtf8Prefix();
    testTitleScanner();
    testNaturalCompare();
    testStartRequestsLikePlink();
    testPlainDataAndEscapedIac();
    testAnswersToOurRequestsAreNotReAcknowledged();
    testUnrequestedOptionsFromServer();
    testDisableEnabledOption();
    testRefusalOfOurRequestIsSilent();
    testWindowSizeIsRefused();
    testTerminalType();
    testSequencesSplitAcrossReads();
    testCrNulBecomesCr();
    testOtherCommandsAreDropped();
    testMalformedSubnegotiationEnds();
    testKeyboardEncoding();
    testBinaryFromServerKeepsNul();

    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("core tests passed\n");
    return 0;
}
