CalDAV-D: Native Win32 CalDAV & HTTP Web Server
CalDAV-D is a lightweight, high-performance CalDAV and HTTP Web Server designed to run natively as a Windows background daemon or service.

Originally written as a Win32 C daemon, this project serves as a robust, standalone application to host a secure, multi-user calendar and scheduling server directly from any Windows desktop or server environment without heavy runtime dependencies.

🚀 Key Features
Embedded CalDAV Server: Fully functional CalDAV protocol implementation (PROPFIND, REPORT, PUT, DELETE, etc.) for seamless synchronization with standard calendar clients (Apple Calendar, Thunderbird, Windows Outlook via CalDAV plugins).

Integrated HTTP Web UI: Serves a built-in HTML/JS web client offering Day, Week, Month, and People views, plus an advanced Automated Weekly Scheduling engine.

Native TLS/SSL Security: Powered by OpenSSL (libssl/libcrypto). Auto-generates its own CalDAV Local Root CA and localhost leaf certificates for secure TLS 1.2/1.3 connections.

Certificate Management: Export the auto-generated certificate chain as a PKCS#12 (.pfx) bundle to install into the Windows Certificate Store for trusted HTTPS connections.

Multi-User & Permissions: Built-in user management with hashed passwords (SHA-256) and granular permissions (e.g., restricted vs. "can edit all events").

CardDAV / Contacts: Basic contact management and vCard upload/download support.

ICS Support: Import and export standard iCalendar (.ics) files directly from the web UI or local data directory.

🛠 Technical Architecture
Core Engine: Written in pure C for maximum performance, minimal memory footprint, and direct Win32 API integration (Winsock2, Windows Services API).

Windows Service / Daemon: Operates as a native background service or console daemon, ensuring the server remains active independently of user sessions.

Automated Build Support: Compiled using standard Windows toolchains (Visual Studio / MSVC or MinGW/CMake).

⚙️ Default Configuration
When running CalDAV-D for the first time, it loads the following default settings (configurable via config.ini or command-line flags):

CalDAV (HTTPS) Port: 8443

HTTP Web UI Port: 8080

Default Admin User: admin (Password is generated on first run or set via configuration)

PFX Certificate Export Password: caldav

Timeout: 2000 ms

📥 Installation & Build Instructions
Option 1: Download Precompiled Binaries
You can download the latest compiled executable and release assets from the [suspicious link removed].

Option 2: Build from Source (MinGW / Visual Studio)
This project uses CMake. You can build it natively on Windows using Visual Studio or MinGW:

Bash
# Clone the repository
git clone https://github.com/hugodraker/caldavd.git
cd caldavd

# Create a build directory and configure with CMake
mkdir build
cd build
cmake -G "Visual Studio 17 2022" ..

# Build the release binary
cmake --build . --config Release
🔒 Trusting the Self-Signed Certificate
Because CalDAV-D generates its own local SSL certificates on startup, web browsers and CalDAV clients will initially show a security warning. To fix this:

Locate the auto-generated CalDAV_CertBundle.pfx file in your application data directory.

Double-click the file to open the Windows Certificate Import Wizard.

Select Local Machine as the store location.

Place the certificate into the Trusted Root Certification Authorities store.

Password: caldav

🤝 Contributing & License
This work is released into the Public Domain. It is provided "as-is", without warranty of any kind. Contributions, forks, and modifications are highly encouraged!

Keywords: Win32 CalDAV Server, C CalDAV Server, Windows HTTP Server, OpenSSL Windows, Self-hosted Calendar, CalDAV-D, CardDAV Windows, Employee Scheduling Server, Windows Service Daemon.
