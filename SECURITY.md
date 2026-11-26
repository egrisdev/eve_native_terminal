# Security policy

## Supported versions

Only the [latest release](https://github.com/egrisdev/eve_native_terminal/releases/latest) gets security fixes. Please check that a problem still happens there before reporting it.

## Reporting a vulnerability

Please do **not** open a public issue for a security problem.

Report it privately instead: on this repository, open the **Security** tab and choose **Report a vulnerability** ([direct link](https://github.com/egrisdev/eve_native_terminal/security/advisories/new)). Only the maintainers can see the report.

It helps to include:

- the EVE Windows Terminal version (shown in Settings > Apps) and your Windows version,
- the link or the steps that trigger the problem, and what an attacker could gain.

Leave out real passwords, host names and addresses.

## What to expect

EVE Windows Terminal is a free project maintained in spare time, so there is no guaranteed response time. Reports are read, and confirmed problems are fixed in a new release and credited in its notes unless you ask not to be named.

## Scope

In scope: anything that lets a `telnet://` link or a console session run a command on the PC, open something other than a console tab, or change settings outside the app's own registration.

Out of scope: a program already running as the signed-in Windows user (see [Burst coordination](#burst-coordination)), the fact that telnet is not encrypted, and vulnerabilities in EVE-NG, Windows Terminal or the devices you connect to.

## Security model

### `telnet://` links can come from any web page

Once TELNET is set to EVE Windows Terminal, Windows hands `evewt.exe` every `telnet://` link the browser opens, from any site, not only EVE-NG. Browsers ask before they open an external app, unless you told them to always allow it for that site. So `evewt.exe` treats every link as untrusted:

- It accepts only `telnet://host[:port]`. The host must be either a DNS name or IPv4 address made of letters, digits, `.` and `-` that starts with a letter or digit, or an IPv6 address in brackets. The port must be 1–65535 and defaults to 23. Any path, query or fragment after them is ignored.
- Everything else is refused with an error box, and no tab opens. That includes user info (`user@`), percent-encoding, spaces, quotes, `;`, and a host starting with `-`, `.` or another character that could make it read as a command-line option or a Windows Terminal command. The refused forms are listed in `testUrls` in [`tests/core_test.cpp`](tests/core_test.cpp).
- Those checks are what make it safe to put the host and port into the Windows Terminal command line. The install path there is quoted. `evewt-console.exe` checks its host and port arguments again in the same way.
- Neither program asks for elevation (their manifests say `asInvoker`), and neither runs a shell. `evewt.exe` writes only to the current user's registry, and only for `--setup` and `--unregister`.

What a page can still do, as with any telnet handler, is make a tab connect to any host and port it names. The tab sends telnet option negotiation and, after 300 ms, one Enter (CR NUL). When several links arrive together, `evewt.exe` first connects to each briefly to read the node's name. That connection sends only the option negotiation.

### Burst coordination

The `evewt.exe` processes started by one Console to All Nodes click share a named mutex and a shared-memory queue (`Local\EVEwt.BurstLock`, `Local\EVEwt.Burst`, and `Local\EVEwt.LaunchGate` for opening tabs). These live in the session-local namespace, so only programs in the same Windows session can reach them. The process that opens the tabs doesn't check the queued hosts again. A program that can write to the queue already runs as you, though, so it could start any command itself.

### Telnet is not encrypted

Console traffic crosses the network in clear text, as with any telnet client, and that includes passwords typed into a device. EVE-NG's native consoles are telnet, so keep them on a lab network you trust.

### The installer

The MSI isn't code-signed. Take it only from this repository's [releases](https://github.com/egrisdev/eve_native_terminal/releases), where a GitHub Actions workflow builds it from the tagged source, or [build it yourself](README.md#building-from-source). It installs for all users:

- the two exes, in `C:\Program Files\EVE-WT`
- registration under `HKLM\SOFTWARE\Classes\EVEwt.telnet`, `HKLM\SOFTWARE\EVEwt` and `HKLM\SOFTWARE\RegisteredApplications`

It runs `evewt.exe --setup` as the installing user after the install, and `evewt.exe --unregister` before an uninstall.
