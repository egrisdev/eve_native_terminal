// evewt-console.exe: the telnet client that runs inside each Windows Terminal tab.
//
//   evewt-console.exe <host> <port>
//
// The console runs in virtual-terminal mode both ways. Keys arrive as the
// escape sequences a router expects: arrows recall history, and Ctrl+C is a
// key, not a signal that kills the client. Everything the router sends goes
// straight to Windows Terminal, including the node-name title EVE-NG sets.
//
// On connect it presses Enter once, so the router prompt appears. When the
// connection drops (node stopped or reloaded), the tab stays open and Enter
// reconnects, like SecureCRT.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <string>

#include "core.h"

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif

namespace {

std::string toUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = static_cast<int>(text.size());
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), size, &out[0], n, nullptr, nullptr);
    return out;
}

std::wstring errorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = n ? std::wstring(buffer, n) : L"error " + std::to_wstring(code);
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ' ||
                             text.back() == L'.'))
        text.pop_back();
    return text;
}

// Bytes for keys that have no character, matching PuTTY's defaults. Only
// needed if the console didn't translate the key itself (no VT input mode).
const wchar_t* keySequence(WORD virtualKey) {
    switch (virtualKey) {
    case VK_UP: return L"\x1b[A";
    case VK_DOWN: return L"\x1b[B";
    case VK_RIGHT: return L"\x1b[C";
    case VK_LEFT: return L"\x1b[D";
    case VK_HOME: return L"\x1b[1~";
    case VK_INSERT: return L"\x1b[2~";
    case VK_DELETE: return L"\x1b[3~";
    case VK_END: return L"\x1b[4~";
    case VK_PRIOR: return L"\x1b[5~";
    case VK_NEXT: return L"\x1b[6~";
    default: return nullptr;
    }
}

// The tab's console: VT mode both ways while we run, restored afterwards.
class Console {
public:
    Console() : in_(GetStdHandle(STD_INPUT_HANDLE)), out_(GetStdHandle(STD_OUTPUT_HANDLE)) {
        haveIn_ = GetConsoleMode(in_, &oldIn_) != 0;
        haveOut_ = GetConsoleMode(out_, &oldOut_) != 0;
        oldInCodePage_ = GetConsoleCP();
        oldOutCodePage_ = GetConsoleOutputCP();

        // No line, echo or processed input: every key, Ctrl+C included, goes to the device.
        if (!SetConsoleMode(in_, ENABLE_VIRTUAL_TERMINAL_INPUT)) SetConsoleMode(in_, 0);
        if (haveOut_) {
            const DWORD vt = oldOut_ | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            if (!SetConsoleMode(out_, vt | DISABLE_NEWLINE_AUTO_RETURN)) SetConsoleMode(out_, vt);
        }
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
    }

    ~Console() {
        if (haveIn_) SetConsoleMode(in_, oldIn_);
        if (haveOut_) SetConsoleMode(out_, oldOut_);
        SetConsoleCP(oldInCodePage_);
        SetConsoleOutputCP(oldOutCodePage_);
    }

    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    HANDLE input() const { return in_; }

    // Writes device output. A UTF-8 character split across network reads is
    // held back until its last bytes arrive, so it never renders as garbage.
    void write(const std::string& bytes) {
        std::string data = std::move(utf8Tail_);
        data += bytes;
        const size_t complete = evewt::completeUtf8Prefix(data);
        utf8Tail_ = data.substr(complete);

        size_t done = 0;
        while (done < complete) {
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(complete - done, 65536));
            if (!WriteFile(out_, data.data() + done, chunk, &written, nullptr) || written == 0) return;
            done += written;
        }
    }

    // A grey status line between sessions.
    void status(const std::wstring& text) { write(toUtf8(L"\r\n\x1b[90m" + text + L"\x1b[0m\r\n")); }

    // Reads the pending input events and appends the typed bytes to keys.
    // False if the console is gone.
    bool read(std::string& keys) {
        INPUT_RECORD records[128];
        DWORD count = 0;
        if (!ReadConsoleInputW(in_, records, 128, &count)) return false;

        std::wstring text;
        if (pendingHighSurrogate_) {
            text.push_back(pendingHighSurrogate_);
            pendingHighSurrogate_ = 0;
        }
        for (DWORD i = 0; i < count; ++i) {
            const INPUT_RECORD& record = records[i];
            if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) continue;

            const KEY_EVENT_RECORD& key = record.Event.KeyEvent;
            std::wstring one;
            if (key.uChar.UnicodeChar) {
                const DWORD state = key.dwControlKeyState;
                const bool alt = (state & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
                const bool ctrl = (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
                if (alt && !ctrl) one.push_back(L'\x1b');  // Alt+key sends Esc first; AltGr is Ctrl+Alt
                one.push_back(key.uChar.UnicodeChar);
            } else if (const wchar_t* sequence = keySequence(key.wVirtualKeyCode)) {
                one = sequence;
            }
            for (WORD n = 0; n < std::max<WORD>(key.wRepeatCount, 1); ++n) text += one;
        }
        // Keep half of a surrogate pair until its other half arrives.
        if (!text.empty() && IS_HIGH_SURROGATE(text.back())) {
            pendingHighSurrogate_ = text.back();
            text.pop_back();
        }
        keys += toUtf8(text);
        return true;
    }

private:
    HANDLE in_, out_;
    DWORD oldIn_ = 0, oldOut_ = 0;
    UINT oldInCodePage_ = 0, oldOutCodePage_ = 0;
    bool haveIn_ = false, haveOut_ = false;
    wchar_t pendingHighSurrogate_ = 0;
    std::string utf8Tail_;
};

// Owns a connected socket and the event that signals it. Outgoing bytes are
// queued and sent as fast as the socket takes them, so a device that stops
// reading (e.g. mid-paste) never stops us from reading what it sends.
class Connection {
public:
    Connection(SOCKET socket, WSAEVENT event) : socket_(socket), event_(event) {}
    ~Connection() {
        if (event_ != WSA_INVALID_EVENT) WSACloseEvent(event_);
        if (socket_ != INVALID_SOCKET) closesocket(socket_);
    }
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    SOCKET socket() const { return socket_; }
    WSAEVENT event() const { return event_; }

    void queue(const std::string& bytes) { pending_ += bytes; }

    // Sends as much of the queue as the socket accepts right now. When it is
    // full, FD_WRITE signals the event once there is room again. False if the
    // connection failed.
    bool flush() {
        while (!pending_.empty()) {
            const int chunk = static_cast<int>(std::min<size_t>(pending_.size(), 65536));
            const int n = send(socket_, pending_.data(), chunk, 0);
            if (n == SOCKET_ERROR) return WSAGetLastError() == WSAEWOULDBLOCK;
            pending_.erase(0, static_cast<size_t>(n));
        }
        return true;
    }

private:
    SOCKET socket_;
    WSAEVENT event_;
    std::string pending_;
};

SOCKET connectTo(const std::wstring& host, unsigned port, std::wstring& error) {
    ADDRINFOW hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    ADDRINFOW* addresses = nullptr;
    const int rc = GetAddrInfoW(host.c_str(), std::to_wstring(port).c_str(), &hints, &addresses);
    if (rc != 0) {
        error = L"Can't resolve " + host + L": " + errorText(static_cast<DWORD>(rc));
        return INVALID_SOCKET;
    }
    SOCKET s = INVALID_SOCKET;
    int lastError = 0;
    for (ADDRINFOW* a = addresses; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) {
            lastError = WSAGetLastError();
            continue;
        }
        if (connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0) break;
        lastError = WSAGetLastError();
        closesocket(s);
        s = INVALID_SOCKET;
    }
    FreeAddrInfoW(addresses);
    if (s == INVALID_SOCKET)
        error = L"Can't connect to " + host + L":" + std::to_wstring(port) + L": " +
                errorText(static_cast<DWORD>(lastError));
    return s;
}

enum class SessionEnd { Disconnected, ConsoleGone };

constexpr DWORD kWakeDelayMs = 300;

// Connects, then relays between the console and the device until the connection ends.
SessionEnd runSession(Console& console, const std::wstring& host, unsigned port) {
    std::wstring error;
    const SOCKET s = connectTo(host, port, error);
    if (s == INVALID_SOCKET) {
        console.status(error);
        return SessionEnd::Disconnected;
    }
    Connection connection(s, WSACreateEvent());
    BOOL noDelay = TRUE;  // send each keystroke immediately
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof noDelay);
    if (connection.event() == WSA_INVALID_EVENT ||
        WSAEventSelect(s, connection.event(), FD_READ | FD_WRITE | FD_CLOSE) != 0) {
        console.status(L"Socket error: " + errorText(static_cast<DWORD>(WSAGetLastError())));
        return SessionEnd::Disconnected;
    }

    evewt::TelnetProtocol telnet;
    std::string toServer, toScreen;
    telnet.start(toServer);
    connection.queue(toServer);
    if (!connection.flush()) {
        console.status(L"Connection lost.");
        return SessionEnd::Disconnected;
    }

    // A fresh console stays blank until the device prints something, so press
    // Enter once shortly after connecting to bring up the prompt (e.g. "R4>").
    // The delay lets option negotiation settle first; typing first cancels it.
    const ULONGLONG wakeAt = GetTickCount64() + kWakeDelayMs;
    bool wakeDone = false;

    // Keyboard first, so typing (Ctrl+C included) still gets through during heavy output.
    const HANDLE waits[2] = {console.input(), connection.event()};
    for (;;) {
        DWORD timeout = INFINITE;
        if (!wakeDone) {
            const ULONGLONG now = GetTickCount64();
            timeout = now >= wakeAt ? 0 : static_cast<DWORD>(wakeAt - now);
        }
        const DWORD which = WaitForMultipleObjects(2, waits, FALSE, timeout);
        toServer.clear();

        if (which == WAIT_TIMEOUT) {
            wakeDone = true;
            telnet.encodeKeyboard("\r", 1, toServer);
        } else if (which == WAIT_OBJECT_0) {
            std::string keys;
            if (!console.read(keys)) return SessionEnd::ConsoleGone;
            if (!keys.empty()) wakeDone = true;
            telnet.encodeKeyboard(keys.data(), keys.size(), toServer);
        } else if (which == WAIT_OBJECT_0 + 1) {
            WSANETWORKEVENTS events = {};
            WSAEnumNetworkEvents(s, connection.event(), &events);  // also resets the event
            // FD_CLOSE is reported only once and nothing re-signals after it,
            // so once the device has closed, read everything that's left now.
            const bool peerClosed = (events.lNetworkEvents & FD_CLOSE) != 0;
            bool closed = false;
            char buffer[16384];
            // Otherwise read a bounded batch, so typing isn't starved during heavy
            // output; if more is waiting, recv re-signals the event.
            for (int batch = 0; peerClosed || batch < 8; ++batch) {
                const int n = recv(s, buffer, sizeof buffer, 0);
                if (n > 0) {
                    toScreen.clear();
                    telnet.receive(buffer, static_cast<size_t>(n), toScreen, toServer);
                    console.write(toScreen);
                    continue;
                }
                closed = (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK);
                break;
            }
            if (closed || peerClosed) {
                console.status(L"Connection closed.");
                return SessionEnd::Disconnected;
            }
        } else {
            return SessionEnd::ConsoleGone;
        }

        // Also runs on FD_WRITE wake-ups, sending whatever was waiting for room.
        connection.queue(toServer);
        if (!connection.flush()) {
            console.status(L"Connection lost.");
            return SessionEnd::Disconnected;
        }
    }
}

// After a disconnect: Enter reconnects, Ctrl+C quits. The tab title is
// prefixed with [closed] meanwhile, so dead tabs stand out.
bool waitToReconnect(Console& console) {
    wchar_t title[512] = {};
    GetConsoleTitleW(title, 512);
    SetConsoleTitleW((L"[closed] " + std::wstring(title)).c_str());
    console.status(L"Press Enter to reconnect, or Ctrl+C to close the tab.");
    // Discard anything typed before the prompt, so a stray Enter doesn't reconnect.
    FlushConsoleInputBuffer(console.input());

    for (;;) {
        if (WaitForSingleObject(console.input(), INFINITE) != WAIT_OBJECT_0) return false;
        std::string keys;
        if (!console.read(keys)) return false;
        const size_t hit = keys.find_first_of("\r\x03");
        if (hit == std::string::npos) continue;
        if (keys[hit] == '\x03') return false;
        SetConsoleTitleW(title);
        return true;
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    unsigned port = 0;
    if (argc != 3 || !evewt::isValidHost(argv[1]) || !evewt::parsePort(argv[2], port)) {
        Console console;
        console.status(L"Usage: evewt-console.exe <host> <port>");
        return 2;
    }
    const std::wstring host = argv[1];
    // Shown until EVE-NG sends the node name.
    SetConsoleTitleW((host + L":" + std::to_wstring(port)).c_str());

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Console console;
        console.status(L"Windows networking (Winsock) failed to start.");
        return 1;
    }

    int exitCode = 0;
    {
        Console console;
        for (;;) {
            if (runSession(console, host, port) == SessionEnd::ConsoleGone) {
                exitCode = 1;
                break;
            }
            if (!waitToReconnect(console)) break;
        }
    }
    WSACleanup();
    return exitCode;
}
