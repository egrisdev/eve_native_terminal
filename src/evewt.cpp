// evewt.exe: the telnet:// handler Windows runs for EVE-NG console links.
//
//   evewt.exe telnet://eve.local:32769/   open the console as a tab
//   evewt.exe --setup                     per-user setup (the MSI runs this after installing)
//   evewt.exe --unregister                per-user cleanup (the MSI runs this when uninstalling)
//
// A GUI-subsystem program that never shows a window, so nothing flashes. It
// validates the link and opens evewt-console.exe (next to this exe) as a new
// tab in the Windows Terminal window named "eve". When several links arrive at
// once (Console to All Nodes), the tabs open sorted by node name.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "core.h"

namespace {

const wchar_t kAppName[] = L"EVE Windows Terminal";
const wchar_t kWindowName[] = L"eve";
const wchar_t kProgId[] = L"EVEwt.telnet";  // must match EVE-WT.wxs
const wchar_t kTelnetAssociation[] =
    L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\telnet";

// ------------------------------------------------------------------ setup

std::wstring readString(HKEY root, const std::wstring& subKey, const wchar_t* name) {
    wchar_t buffer[1024];
    DWORD size = sizeof buffer;
    if (RegGetValueW(root, subKey.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS)
        return {};
    return buffer;
}

// The ProgId Windows uses for telnet:// links, if the user picked one in
// Default apps. Newer Windows 11 builds keep it under UserChoiceLatest.
std::wstring telnetDefault() {
    const std::wstring base = kTelnetAssociation;
    for (const wchar_t* sub : {L"\\UserChoice", L"\\UserChoiceLatest", L"\\UserChoiceLatest\\ProgId"}) {
        const std::wstring progId = readString(HKEY_CURRENT_USER, base + sub, L"ProgId");
        if (!progId.empty()) return progId;
    }
    return {};
}

// The hand-made .reg setup that came before this app put its keys in HKCU,
// which override the installed ones in HKLM. Remove them.
void removeManualSetup() {
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\EVEwt.telnet");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\EVEwt");
    RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", L"EVEwt");
    const std::wstring command =
        readString(HKEY_CURRENT_USER, L"Software\\Classes\\telnet\\shell\\open\\command", nullptr);
    if (command.find(L"eve-wt") != std::wstring::npos)
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\telnet");
}

bool shellOpen(const wchar_t* target) {
    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof info;
    // Opening an ms-settings: page completes asynchronously, and this process
    // exits right after, which can silently cancel it. NOASYNC waits for it.
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = target;
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&info) != FALSE;
}

void openDefaultApps() {
    // Windows 11 opens straight to this app's page; older builds open the main page.
    if (!shellOpen(L"ms-settings:defaultapps?registeredAppMachine=EVEwt"))
        shellOpen(L"ms-settings:defaultapps");
}

// After install: clear the old manual setup, then, because Windows doesn't let
// installers choose default apps, walk the user to the page where they do.
int runSetup() {
    removeManualSetup();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    if (telnetDefault() == kProgId) return 0;

    MessageBoxW(nullptr,
                L"One last step: Windows doesn't let installers choose default apps.\n\n"
                L"In the Settings page that opens next, set TELNET to \"EVE Windows Terminal\".\n"
                L"(Windows 10: Choose default apps by protocol > TELNET.)",
                kAppName, MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
    openDefaultApps();
    return 0;
}

// Before uninstall: if telnet links point at this app, clear that choice so they
// fall back to the system handler (e.g. PuTTY from the EVE client pack).
int runUnregister() {
    if (telnetDefault() == kProgId) {
        const std::wstring base = kTelnetAssociation;
        RegDeleteTreeW(HKEY_CURRENT_USER, (base + L"\\UserChoice").c_str());
        RegDeleteTreeW(HKEY_CURRENT_USER, (base + L"\\UserChoiceLatest").c_str());
    }
    removeManualSetup();
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return 0;
}

// ------------------------------------------------------------------ open a console

int fail(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), kAppName, MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    return 1;
}

bool fileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring exeDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, &path[0], static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    return path.substr(0, path.find_last_of(L'\\') + 1);
}

std::wstring findWindowsTerminal() {
    wchar_t found[MAX_PATH];
    const DWORD n = SearchPathW(nullptr, L"wt.exe", nullptr, MAX_PATH, found, nullptr);
    if (n > 0 && n < MAX_PATH) return found;

    // Its App Execution Alias, in case that folder isn't on PATH.
    wchar_t localAppData[MAX_PATH];
    const DWORD m = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (m > 0 && m < MAX_PATH) {
        const std::wstring alias = std::wstring(localAppData) + L"\\Microsoft\\WindowsApps\\wt.exe";
        if (fileExists(alias)) return alias;
    }
    return {};
}

// Holds a mutex for its lifetime (also if a previous owner died holding it).
class MutexLock {
public:
    MutexLock(HANDLE mutex, DWORD timeoutMs) : mutex_(mutex) {
        if (mutex_) {
            const DWORD r = WaitForSingleObject(mutex_, timeoutMs);
            owned_ = (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED);
        }
    }
    ~MutexLock() {
        if (owned_) ReleaseMutex(mutex_);
    }
    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;

    bool owned() const { return owned_; }

private:
    HANDLE mutex_;
    bool owned_ = false;
};

bool processAlive(DWORD pid) {
    const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return GetLastError() == ERROR_ACCESS_DENIED;  // exists, just not ours to open
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

// ------------------------------------------------------------------ bursts

// "Console to All Nodes" starts one evewt.exe per node within a few hundred
// milliseconds. The first one becomes the leader; the others add their node to
// a shared queue and exit. Once no node has arrived for kQuietMs, the leader
// takes the whole batch and opens it sorted by node name.
constexpr DWORD kQuietMs = 500;
constexpr DWORD kMaxCollectMs = 3000;
constexpr DWORD kProbeMs = 1500;
constexpr LONG kMaxBatch = 64;  // also keeps probeNames within select()'s 64 sockets

struct QueuedNode {
    wchar_t host[256];
    unsigned port;
};

struct BurstQueue {
    LONG leaderActive;
    DWORD leaderPid;
    LONG count;
    LONG generation;  // bumped on every add, so the leader sees when arrivals stop
    QueuedNode nodes[kMaxBatch];
};

std::vector<evewt::Target> collectBurst(HANDLE mutex, BurstQueue& queue, const evewt::Target& self) {
    const ULONGLONG start = GetTickCount64();
    ULONGLONG lastArrival = start;
    LONG seen = -1;
    for (;;) {
        Sleep(50);
        MutexLock lock(mutex, 10000);
        const ULONGLONG now = GetTickCount64();
        if (!lock.owned()) {
            // Never touch the queue unlocked. If the lock stays stuck, open our own
            // node; the flag still names this (soon dead) process, so the next
            // arrival takes over the rest.
            if (now - start > 30000) return {self};
            continue;
        }
        if (queue.generation != seen) {
            seen = queue.generation;
            lastArrival = now;
        }
        if (now - lastArrival < kQuietMs && now - start < kMaxCollectMs) continue;

        std::vector<evewt::Target> batch;
        for (LONG i = 0; i < queue.count; ++i) {
            if (!queue.nodes[i].host[0]) continue;
            evewt::Target target;
            target.host = queue.nodes[i].host;
            target.port = queue.nodes[i].port;
            batch.push_back(target);
        }
        queue.count = 0;
        queue.leaderActive = 0;  // under the lock, so no node is ever left behind
        return batch;
    }
}

// Returns the nodes this process should open: the whole burst if it is the
// leader, or nothing if another evewt.exe will open this node.
std::vector<evewt::Target> joinBurst(const evewt::Target& target) {
    const HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\EVEwt.BurstLock");
    const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                              sizeof(BurstQueue), L"Local\\EVEwt.Burst");
    auto* queue = mapping ? static_cast<BurstQueue*>(
                                MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BurstQueue)))
                          : nullptr;

    bool queued = false, leader = false;
    if (mutex && queue) {
        MutexLock lock(mutex, 10000);
        if (lock.owned() && queue->count < kMaxBatch) {
            QueuedNode& slot = queue->nodes[queue->count];
            const size_t length = target.host.copy(slot.host, 255);
            slot.host[length] = L'\0';
            slot.port = target.port;
            ++queue->count;  // only once the slot is filled
            ++queue->generation;
            queued = true;
            // Lead if nobody is, or if the leader died mid-burst (its queued
            // nodes are still here and get picked up).
            if (!queue->leaderActive || !processAlive(queue->leaderPid)) {
                queue->leaderActive = 1;
                queue->leaderPid = GetCurrentProcessId();
                leader = true;
            }
        }
    }

    std::vector<evewt::Target> batch;
    if (!queued) batch.push_back(target);  // couldn't use the queue: open it on its own
    else if (leader) batch = collectBurst(mutex, *queue, target);

    if (queue) UnmapViewOfFile(queue);
    if (mapping) CloseHandle(mapping);
    if (mutex) CloseHandle(mutex);
    return batch;
}

// ------------------------------------------------------------------ node names

struct Node {
    evewt::Target target;
    std::string name;  // as EVE-NG sends it; empty if it didn't arrive in time
};

struct Address {
    sockaddr_storage storage = {};
    int length = 0;  // 0: couldn't resolve
};

// Resolves a host once for all its nodes, preferring IPv4: an unreachable
// IPv6 address would otherwise stall the probe until its deadline.
Address resolve(const std::wstring& host) {
    Address result;
    ADDRINFOW hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    ADDRINFOW* list = nullptr;
    if (GetAddrInfoW(host.c_str(), nullptr, &hints, &list) != 0) return result;
    const ADDRINFOW* pick = list;
    for (const ADDRINFOW* a = list; a; a = a->ai_next) {
        if (a->ai_family == AF_INET) {
            pick = a;
            break;
        }
    }
    if (pick && pick->ai_addrlen <= sizeof result.storage) {
        std::memcpy(&result.storage, pick->ai_addr, pick->ai_addrlen);
        result.length = static_cast<int>(pick->ai_addrlen);
    }
    FreeAddrInfoW(list);
    return result;
}

// Starts a non-blocking connect; INVALID_SOCKET if it failed outright.
SOCKET startConnect(Address address, unsigned port, bool& connected) {
    if (address.length == 0) return INVALID_SOCKET;
    const u_short networkPort = htons(static_cast<u_short>(port));
    if (address.storage.ss_family == AF_INET)
        reinterpret_cast<sockaddr_in*>(&address.storage)->sin_port = networkPort;
    else
        reinterpret_cast<sockaddr_in6*>(&address.storage)->sin6_port = networkPort;

    SOCKET s = socket(address.storage.ss_family, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return s;
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    if (connect(s, reinterpret_cast<const sockaddr*>(&address.storage), address.length) == 0) {
        connected = true;
    } else if (WSAGetLastError() != WSAEWOULDBLOCK) {
        closesocket(s);
        s = INVALID_SOCKET;
    }
    return s;
}

// Clean close, so a console that takes one client at a time (QEMU) is free
// again for the real tab that connects right after.
void hangUp(SOCKET s) {
    shutdown(s, SD_BOTH);
    closesocket(s);
}

struct Probe {
    SOCKET socket = INVALID_SOCKET;
    bool connected = false;
    evewt::TelnetProtocol telnet;
    evewt::TitleScanner title;

    // Negotiate exactly like the real client, so the device side sees nothing new.
    void start() {
        std::string out;
        telnet.start(out);
        send(socket, out.data(), static_cast<int>(out.size()), 0);
    }
};

// The links only carry host and port; EVE-NG sends each node's name as the
// terminal title right after a client connects. So connect to all the consoles
// at once, read until each title arrives (or timeoutMs passes), and hang up.
void probeNames(std::vector<Node>& nodes, DWORD timeoutMs) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;  // name lookups count too

    std::vector<Probe> probes(nodes.size());
    std::vector<std::pair<std::wstring, Address>> resolved;
    for (size_t i = 0; i < nodes.size() && GetTickCount64() < deadline; ++i) {
        const std::wstring& host = nodes[i].target.host;
        auto known = std::find_if(resolved.begin(), resolved.end(),
                                  [&](const std::pair<std::wstring, Address>& r) { return r.first == host; });
        if (known == resolved.end()) {
            resolved.emplace_back(host, resolve(host));
            known = std::prev(resolved.end());
        }
        probes[i].socket = startConnect(known->second, nodes[i].target.port, probes[i].connected);
        if (probes[i].connected) probes[i].start();
    }

    for (;;) {
        fd_set readable, writable, failed;
        FD_ZERO(&readable);
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        int watched = 0;
        for (const Probe& p : probes) {
            if (p.socket == INVALID_SOCKET) continue;
            if (p.connected) {
                FD_SET(p.socket, &readable);
            } else {
                FD_SET(p.socket, &writable);  // a non-blocking connect completes as writable...
                FD_SET(p.socket, &failed);    // ...or fails as an exception
            }
            ++watched;
        }
        const ULONGLONG now = GetTickCount64();
        if (watched == 0 || now >= deadline) break;
        const ULONGLONG left = deadline - now;
        timeval wait = {static_cast<long>(left / 1000), static_cast<long>((left % 1000) * 1000)};
        if (select(0, &readable, &writable, &failed, &wait) <= 0) break;

        for (size_t i = 0; i < probes.size(); ++i) {
            Probe& p = probes[i];
            if (p.socket == INVALID_SOCKET) continue;
            bool done = false;
            if (!p.connected) {
                if (FD_ISSET(p.socket, &failed)) {
                    done = true;
                } else if (FD_ISSET(p.socket, &writable)) {
                    p.connected = true;
                    p.start();
                }
            } else if (FD_ISSET(p.socket, &readable)) {
                char buffer[4096];
                const int n = recv(p.socket, buffer, sizeof buffer, 0);
                if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) continue;  // spurious wake-up
                if (n <= 0) {
                    done = true;
                } else {
                    std::string screen, reply;
                    p.telnet.receive(buffer, static_cast<size_t>(n), screen, reply);
                    if (!reply.empty()) send(p.socket, reply.data(), static_cast<int>(reply.size()), 0);
                    if (p.title.feed(screen.data(), screen.size())) {
                        nodes[i].name = p.title.title();
                        done = true;
                    }
                }
            }
            if (done) {
                hangUp(p.socket);
                p.socket = INVALID_SOCKET;
            }
        }
    }
    for (Probe& p : probes)
        if (p.socket != INVALID_SOCKET) hangUp(p.socket);
    WSACleanup();
}

// ------------------------------------------------------------------ open tabs

int openConsoles(const std::vector<evewt::Target>& targets) {
    const std::wstring console = exeDirectory() + L"evewt-console.exe";
    if (!fileExists(console))
        return fail(L"evewt-console.exe is missing next to evewt.exe.\n\n"
                    L"Repair EVE Windows Terminal in Settings > Apps.");
    const std::wstring wt = findWindowsTerminal();
    if (wt.empty())
        return fail(L"Windows Terminal (wt.exe) was not found.\n\n"
                    L"Install it from the Microsoft Store, or run:\n"
                    L"winget install Microsoft.WindowsTerminal");

    std::vector<Node> nodes;
    for (const evewt::Target& target : targets) {
        const bool duplicate = std::any_of(nodes.begin(), nodes.end(), [&](const Node& n) {
            return n.target.port == target.port && n.target.host == target.host;
        });
        if (!duplicate) nodes.push_back({target, {}});
    }
    if (nodes.size() > 1) {
        probeNames(nodes, kProbeMs);
        // By name, R2 before R10; nodes whose name never arrived go last, by port.
        std::stable_sort(nodes.begin(), nodes.end(), [](const Node& a, const Node& b) {
            if (a.name.empty() != b.name.empty()) return b.name.empty();
            if (!a.name.empty()) {
                const int c = evewt::naturalCompare(a.name, b.name);
                if (c != 0) return c < 0;
            }
            return a.target.port < b.target.port;
        });
    }

    // One wt.exe call opens many tabs (new-tab ... ; new-tab ...) in exactly that
    // order. Split into several calls only to stay under Windows' 32767-character
    // command-line limit. Host and port are validated (no spaces, quotes or ';');
    // the install path is quoted.
    std::vector<std::wstring> commandLines;
    std::wstring current;
    for (const Node& node : nodes) {
        const std::wstring tab = L"new-tab \"" + console + L"\" " + node.target.host + L" " +
                                 std::to_wstring(node.target.port);
        if (!current.empty() && current.size() + tab.size() > 30000) {
            commandLines.push_back(current);
            current.clear();
        }
        current += current.empty() ? L"\"" + wt + L"\" -w " + kWindowName + L" " + tab : L" ; " + tab;
    }
    commandLines.push_back(current);

    DWORD launchError = 0;
    const HANDLE gateMutex = CreateMutexW(nullptr, FALSE, L"Local\\EVEwt.LaunchGate");
    {
        // Overlapping bursts take turns, so they can't race to create the window.
        MutexLock gate(gateMutex, 20000);
        for (std::wstring& commandLine : commandLines) {
            STARTUPINFOW startup = {};
            startup.cb = sizeof startup;
            PROCESS_INFORMATION process = {};
            if (!CreateProcessW(wt.c_str(), &commandLine[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                                &startup, &process)) {
                launchError = GetLastError();
                break;
            }
            // A wt.exe that hands its tabs to an existing window exits quickly. One
            // that has to create the window keeps running, so wait until it's ready.
            WaitForInputIdle(process.hProcess, 5000);
            WaitForSingleObject(process.hProcess, 1500);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
    }  // gate released before any message box, so other bursts aren't held up
    if (gateMutex) CloseHandle(gateMutex);

    if (launchError)
        return fail(L"Could not start Windows Terminal (error " + std::to_wstring(launchError) + L").");
    return 0;
}

int openConsole(const std::wstring& url) {
    evewt::Target target;
    if (!evewt::parseTelnetUrl(url, target))
        return fail(L"This isn't a telnet link EVE Windows Terminal can open:\n\n" + url.substr(0, 300));
    const std::vector<evewt::Target> batch = joinBurst(target);
    if (batch.empty()) return 0;  // another evewt.exe is opening this burst
    return openConsoles(batch);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const std::wstring arg = (argv && argc >= 2) ? argv[1] : L"";
    if (argv) LocalFree(argv);

    if (arg == L"--setup" || arg == L"--unregister") {
        // ShellExecute of an ms-settings: URI wants COM initialised.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const int code = arg == L"--setup" ? runSetup() : runUnregister();
        if (SUCCEEDED(com)) CoUninitialize();
        return code;
    }
    return openConsole(arg);
}
