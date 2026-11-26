// Portable core of EVE Windows Terminal: telnet:// URL validation and the
// telnet protocol (RFC 854/855 with RFC 1143-style negotiation, TTYPE).
// No Windows headers here, so tests/core_test.cpp runs it on any OS.
#pragma once

#include <cstddef>
#include <string>
#include <utility>

namespace evewt {

// ------------------------------------------------------------------ URL

struct Target {
    std::wstring host;
    unsigned port = 23;
};

inline bool isAsciiDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }
inline bool isAsciiAlnum(wchar_t c) {
    return isAsciiDigit(c) || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

// DNS name or IPv4 address. It must start with a letter or digit, so it can
// never be read as a command-line option.
inline bool isValidHostName(const std::wstring& host) {
    if (host.empty() || host.size() > 253 || !isAsciiAlnum(host[0])) return false;
    for (wchar_t c : host)
        if (!isAsciiAlnum(c) && c != L'.' && c != L'-') return false;
    return true;
}

// Bare IPv6 address (what sits inside [ ] in a URL).
inline bool isValidIPv6(const std::wstring& host) {
    if (host.size() < 2 || host.size() > 45 || host.find(L':') == std::wstring::npos) return false;
    for (wchar_t c : host) {
        const bool hex = isAsciiDigit(c) || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
        if (!hex && c != L':' && c != L'.') return false;
    }
    return true;
}

inline bool isValidHost(const std::wstring& host) {
    return isValidHostName(host) || isValidIPv6(host);
}

inline bool parsePort(const std::wstring& text, unsigned& port) {
    if (text.empty() || text.size() > 5) return false;
    unsigned value = 0;
    for (wchar_t c : text) {
        if (!isAsciiDigit(c)) return false;
        value = value * 10 + static_cast<unsigned>(c - L'0');
    }
    if (value < 1 || value > 65535) return false;
    port = value;
    return true;
}

// Parses telnet://host[:port][/...] as EVE-NG emits it. Any web page can fire a
// telnet:// link, so anything beyond a plain host and numeric port is refused.
inline bool parseTelnetUrl(const std::wstring& url, Target& out) {
    static const wchar_t scheme[] = L"telnet://";
    const size_t schemeLen = sizeof scheme / sizeof scheme[0] - 1;
    if (url.size() < schemeLen) return false;
    for (size_t i = 0; i < schemeLen; ++i) {
        wchar_t c = url[i];
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        if (c != scheme[i]) return false;
    }

    const size_t end = url.find_first_of(L"/?#", schemeLen);
    const std::wstring authority =
        url.substr(schemeLen, end == std::wstring::npos ? std::wstring::npos : end - schemeLen);

    Target target;
    std::wstring portText;
    if (!authority.empty() && authority[0] == L'[') {
        const size_t close = authority.find(L']');
        if (close == std::wstring::npos) return false;
        target.host = authority.substr(1, close - 1);
        if (!isValidIPv6(target.host)) return false;
        const std::wstring rest = authority.substr(close + 1);
        if (!rest.empty()) {
            if (rest[0] != L':') return false;
            portText = rest.substr(1);
        }
    } else {
        const size_t colon = authority.find(L':');
        target.host = authority.substr(0, colon);
        if (!isValidHostName(target.host)) return false;
        if (colon != std::wstring::npos) portText = authority.substr(colon + 1);
    }
    if (!portText.empty() && !parsePort(portText, target.port)) return false;

    out = std::move(target);
    return true;
}

// ------------------------------------------------------------------ UTF-8

// Length of the part of `bytes` that doesn't end in the middle of a UTF-8
// character. A network read can split a character; the caller holds back the
// rest (at most 3 bytes) until the next read. Bytes that aren't UTF-8 at all
// are never held for longer than that.
inline size_t completeUtf8Prefix(const std::string& bytes) {
    const size_t n = bytes.size();
    for (size_t back = 1; back <= 3 && back <= n; ++back) {
        const unsigned char c = static_cast<unsigned char>(bytes[n - back]);
        if ((c & 0xC0) == 0x80) continue;  // continuation byte: keep looking for the lead
        const size_t length = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        return back < length ? n - back : n;
    }
    return n;
}

// ------------------------------------------------------------------ node names

// Finds the first window title in a terminal stream: OSC 0 or 2, ended by
// BEL or ESC \. EVE-NG sends the node name this way right after connecting.
class TitleScanner {
public:
    // Feeds bytes for the screen (after telnet decoding). True once a title is found.
    bool feed(const char* data, size_t len) {
        for (size_t i = 0; i < len && !found_; ++i) step(static_cast<unsigned char>(data[i]));
        return found_;
    }
    bool found() const { return found_; }
    const std::string& title() const { return title_; }

private:
    enum class State { Text, Esc, Param, Body, BodyEsc };
    static constexpr size_t kMaxTitle = 256;

    void step(unsigned char c) {
        switch (state_) {
        case State::Text:
            if (c == 0x1B) state_ = State::Esc;
            return;
        case State::Esc:
            if (c == ']') {
                param_.clear();
                state_ = State::Param;
            } else if (c != 0x1B) {
                state_ = State::Text;
            }
            return;
        case State::Param:
            if (c >= '0' && c <= '9' && param_.size() < 4) {
                param_.push_back(static_cast<char>(c));
            } else if (c == ';') {
                body_.clear();
                state_ = State::Body;
            } else {
                state_ = c == 0x1B ? State::Esc : State::Text;
            }
            return;
        case State::Body:
            if (c == 0x07) finish();
            else if (c == 0x1B) state_ = State::BodyEsc;
            else if (c >= 0x20 && body_.size() < kMaxTitle) body_.push_back(static_cast<char>(c));
            return;
        case State::BodyEsc:
            if (c == '\\') {
                finish();
            } else {  // not a terminator: abandon this OSC, treat the ESC as a new sequence
                state_ = State::Esc;
                step(c);
            }
            return;
        }
    }

    void finish() {
        state_ = State::Text;
        if (param_ != "0" && param_ != "2") return;
        const size_t first = body_.find_first_not_of(' ');
        if (first == std::string::npos) return;
        title_ = body_.substr(first, body_.find_last_not_of(' ') - first + 1);
        found_ = true;
    }

    State state_ = State::Text;
    std::string param_, body_, title_;
    bool found_ = false;
};

// Orders names the way people expect: case-insensitive, with digit runs
// compared as numbers, so R2 sorts before R10. Returns <0, 0 or >0.
inline int naturalCompare(const std::string& a, const std::string& b) {
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (digit(a[i]) && digit(b[j])) {
            while (i < a.size() && a[i] == '0') ++i;  // leading zeros don't count
            while (j < b.size() && b[j] == '0') ++j;
            const size_t si = i, sj = j;
            while (i < a.size() && digit(a[i])) ++i;
            while (j < b.size() && digit(b[j])) ++j;
            const size_t li = i - si, lj = j - sj;
            if (li != lj) return li < lj ? -1 : 1;  // more digits = bigger number
            const int c = a.compare(si, li, b, sj, lj);
            if (c != 0) return c < 0 ? -1 : 1;
        } else {
            const char x = lower(a[i]), y = lower(b[j]);
            if (x != y) return static_cast<unsigned char>(x) < static_cast<unsigned char>(y) ? -1 : 1;
            ++i;
            ++j;
        }
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

// ------------------------------------------------------------------ telnet

namespace telnet {
constexpr unsigned char SE = 240, NOP = 241, SB = 250, WILL = 251, WONT = 252, DO = 253,
                        DONT = 254, IAC = 255;
constexpr unsigned char BINARY = 0, ECHO = 1, SGA = 3, TTYPE = 24, NAWS = 31;
constexpr unsigned char TTYPE_IS = 0, TTYPE_SEND = 1;
}  // namespace telnet

// The telnet protocol as a pure byte transformer: the caller moves bytes
// between the socket and the console, this class decides what they mean.
class TelnetProtocol {
public:
    explicit TelnetProtocol(std::string terminalType = "XTERM-256COLOR")
        : terminalType_(std::move(terminalType)) {}

    // Options to request right after connecting: what plink asks for, minus
    // window size (see acceptLocal).
    void start(std::string& toServer) {
        using namespace telnet;
        request(local_, WILL, TTYPE, toServer);
        request(remote_, DO, ECHO, toServer);
        request(local_, WILL, SGA, toServer);
        request(remote_, DO, SGA, toServer);
    }

    // Bytes from the server: text for the screen goes to toScreen, protocol
    // replies to toServer. Sequences may be split across calls.
    void receive(const char* data, size_t len, std::string& toScreen, std::string& toServer) {
        for (size_t i = 0; i < len; ++i) receiveByte(static_cast<unsigned char>(data[i]), toScreen, toServer);
    }

    // Keyboard bytes to wire bytes: IAC doubled, CR sent as CR NUL unless we
    // agreed to send binary (the same rules plink uses).
    void encodeKeyboard(const char* data, size_t len, std::string& toServer) const {
        const bool binary = local_[telnet::BINARY] == State::Yes;
        for (size_t i = 0; i < len; ++i) {
            const unsigned char c = static_cast<unsigned char>(data[i]);
            toServer.push_back(static_cast<char>(c));
            if (c == telnet::IAC) toServer.push_back(static_cast<char>(telnet::IAC));
            else if (c == '\r' && !binary) toServer.push_back('\0');
        }
    }

    bool remoteEnabled(unsigned char option) const { return remote_[option] == State::Yes; }
    bool localEnabled(unsigned char option) const { return local_[option] == State::Yes; }

private:
    enum class State : unsigned char { No, Yes, WantYes };
    enum class Parse : unsigned char { Data, Iac, Option, Sub, SubIac };
    static constexpr size_t kMaxSub = 512;

    // Options the server may use (WILL from the server) and options we will
    // use (DO from the server). Window size (NAWS) is refused on purpose: EVE-NG
    // consoles are serial lines with no window size, and a size report that
    // reaches the device as data reads as keystrokes. 120x30 is "x" and
    // Ctrl+^ (IOS's escape key), which prints "% No open connections".
    static bool acceptRemote(unsigned char o) {
        return o == telnet::BINARY || o == telnet::ECHO || o == telnet::SGA;
    }
    static bool acceptLocal(unsigned char o) {
        return o == telnet::BINARY || o == telnet::SGA || o == telnet::TTYPE;
    }

    static void appendCommand(std::string& out, unsigned char cmd, unsigned char option) {
        out.push_back(static_cast<char>(telnet::IAC));
        out.push_back(static_cast<char>(cmd));
        out.push_back(static_cast<char>(option));
    }

    static void request(State* side, unsigned char cmd, unsigned char option, std::string& out) {
        if (side[option] != State::No) return;
        side[option] = State::WantYes;
        appendCommand(out, cmd, option);
    }

    void handleOption(unsigned char cmd, unsigned char option, std::string& out) {
        using namespace telnet;
        switch (cmd) {
        case WILL:
            if (remote_[option] == State::Yes) return;
            if (remote_[option] == State::WantYes) {  // agreement to our DO
                remote_[option] = State::Yes;
            } else if (acceptRemote(option)) {
                remote_[option] = State::Yes;
                appendCommand(out, DO, option);
            } else {
                appendCommand(out, DONT, option);
            }
            return;
        case WONT:
            if (remote_[option] == State::Yes) appendCommand(out, DONT, option);
            remote_[option] = State::No;  // a refusal of our DO needs no reply
            return;
        case DO:
            if (local_[option] == State::Yes) return;
            if (local_[option] == State::WantYes) {  // agreement to our WILL
                local_[option] = State::Yes;
            } else if (acceptLocal(option)) {
                local_[option] = State::Yes;
                appendCommand(out, WILL, option);
            } else {
                appendCommand(out, WONT, option);
            }
            return;
        case DONT:
            if (local_[option] == State::Yes) appendCommand(out, WONT, option);
            local_[option] = State::No;
            return;
        }
    }

    void handleSubnegotiation(std::string& out) {
        using namespace telnet;
        if (sub_.size() >= 2 && static_cast<unsigned char>(sub_[0]) == TTYPE &&
            static_cast<unsigned char>(sub_[1]) == TTYPE_SEND && local_[TTYPE] == State::Yes) {
            out.push_back(static_cast<char>(IAC));
            out.push_back(static_cast<char>(SB));
            out.push_back(static_cast<char>(TTYPE));
            out.push_back(static_cast<char>(TTYPE_IS));
            out += terminalType_;
            out.push_back(static_cast<char>(IAC));
            out.push_back(static_cast<char>(SE));
        }
    }

    // The byte after an IAC.
    void handleCommand(unsigned char c, std::string& screen) {
        using namespace telnet;
        switch (c) {
        case IAC:
            screen.push_back(static_cast<char>(IAC));
            parse_ = Parse::Data;
            return;
        case WILL: case WONT: case DO: case DONT:
            command_ = c;
            parse_ = Parse::Option;
            return;
        case SB:
            sub_.clear();
            parse_ = Parse::Sub;
            return;
        default:  // NOP, GA, DM, AYT, ...: nothing to show
            parse_ = Parse::Data;
            return;
        }
    }

    void receiveByte(unsigned char c, std::string& screen, std::string& out) {
        using namespace telnet;
        switch (parse_) {
        case Parse::Data:
            if (c == IAC) {
                parse_ = Parse::Iac;
                afterCr_ = false;
                return;
            }
            // Outside binary mode a bare CR arrives as CR NUL; drop the NUL.
            if (c == 0 && afterCr_ && remote_[BINARY] != State::Yes) {
                afterCr_ = false;
                return;
            }
            afterCr_ = (c == '\r');
            screen.push_back(static_cast<char>(c));
            return;
        case Parse::Iac:
            handleCommand(c, screen);
            return;
        case Parse::Option:
            handleOption(command_, c, out);
            parse_ = Parse::Data;
            return;
        case Parse::Sub:
            if (c == IAC) parse_ = Parse::SubIac;
            else if (sub_.size() < kMaxSub) sub_.push_back(static_cast<char>(c));
            return;
        case Parse::SubIac:
            if (c == SE) {
                handleSubnegotiation(out);
                parse_ = Parse::Data;
            } else if (c == IAC) {
                if (sub_.size() < kMaxSub) sub_.push_back(static_cast<char>(IAC));
                parse_ = Parse::Sub;
            } else {
                // Malformed: drop the subnegotiation, treat this as a command.
                handleCommand(c, screen);
            }
            return;
        }
    }

    std::string terminalType_;
    State remote_[256] = {};
    State local_[256] = {};
    Parse parse_ = Parse::Data;
    unsigned char command_ = 0;
    bool afterCr_ = false;
    std::string sub_;
};

}  // namespace evewt
