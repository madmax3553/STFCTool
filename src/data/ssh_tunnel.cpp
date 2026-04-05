#include "data/ssh_tunnel.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <thread>
#include <vector>
#include <sstream>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

namespace stfc {

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

SshTunnel::SshTunnel(const SshTunnelConfig& config)
    : config_(config) {}

SshTunnel::~SshTunnel() {
    close();
}

// ---------------------------------------------------------------------------
// Port check — is something already listening on the local port?
// ---------------------------------------------------------------------------

bool SshTunnel::is_port_listening() const {
    int sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config_.local_port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // Set a short connect timeout via non-blocking + select
    struct timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    int result = ::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    ::close(sock);

    return result == 0;
}

// ---------------------------------------------------------------------------
// Wait for port to become available after ssh spawn
// ---------------------------------------------------------------------------

bool SshTunnel::wait_for_port(int timeout_seconds) const {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (is_port_listening()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return false;
}

// ---------------------------------------------------------------------------
// Open the tunnel
// ---------------------------------------------------------------------------

std::string SshTunnel::open() {
    // If port is already listening, assume an existing tunnel and reuse it
    if (is_port_listening()) {
        reused_ = true;
        opened_.store(true);
        return "";  // Success — already connected
    }

    // Build the ssh command arguments
    // ssh -L local_port:remote_host:remote_port user@host -N -f -o ...
    std::vector<std::string> args;
    args.push_back("ssh");

    // Port forwarding
    std::string forward = std::to_string(config_.local_port) + ":" +
                          config_.remote_host + ":" +
                          std::to_string(config_.remote_port);
    args.push_back("-L");
    args.push_back(forward);

    // SSH target
    args.push_back(config_.ssh_user + "@" + config_.ssh_host);

    // SSH port (if non-default)
    if (config_.ssh_port != 22) {
        args.push_back("-p");
        args.push_back(std::to_string(config_.ssh_port));
    }

    // No remote command — just forwarding
    args.push_back("-N");

    // Don't read stdin
    args.push_back("-n");

    // Connection timeout
    args.push_back("-o");
    args.push_back("ConnectTimeout=" + std::to_string(config_.connect_timeout));

    // Host key checking
    if (!config_.strict_host_checking) {
        args.push_back("-o");
        args.push_back("StrictHostKeyChecking=no");
        args.push_back("-o");
        args.push_back("UserKnownHostsFile=/dev/null");
        args.push_back("-o");
        args.push_back("LogLevel=ERROR");
    }

    // SSH key
    if (!config_.ssh_key_path.empty()) {
        args.push_back("-i");
        args.push_back(config_.ssh_key_path);
    }

    // Exit if forwarding fails (don't hang with a broken tunnel)
    args.push_back("-o");
    args.push_back("ExitOnForwardFailure=yes");

    // Keep-alive to detect dead connections
    args.push_back("-o");
    args.push_back("ServerAliveInterval=30");
    args.push_back("-o");
    args.push_back("ServerAliveCountMax=3");

    // Fork the ssh process
    pid_t pid = fork();
    if (pid < 0) {
        return std::string("Failed to fork ssh process: ") + strerror(errno);
    }

    if (pid == 0) {
        // Child process — exec ssh
        // Redirect stdout/stderr to /dev/null to avoid polluting TUI
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }

        // Build argv for execvp
        std::vector<char*> argv;
        for (auto& a : args) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        execvp("ssh", argv.data());

        // If execvp returns, it failed
        _exit(127);
    }

    // Parent process
    ssh_pid_ = pid;

    // Wait for the tunnel to become active
    if (!wait_for_port(config_.connect_timeout + 2)) {
        // Check if ssh process died
        int wstatus = 0;
        pid_t w = waitpid(ssh_pid_, &wstatus, WNOHANG);
        if (w > 0) {
            ssh_pid_ = -1;
            if (WIFEXITED(wstatus)) {
                int code = WEXITSTATUS(wstatus);
                if (code == 255) {
                    return "SSH connection refused — is " + config_.ssh_host + " reachable?";
                }
                return "SSH tunnel exited with code " + std::to_string(code);
            }
            return "SSH tunnel process terminated unexpectedly";
        }

        // ssh is still running but port isn't responding — kill it
        close();
        return "SSH tunnel opened but Ollama is not responding on " +
               config_.remote_host + ":" + std::to_string(config_.remote_port) +
               " — is Ollama running on the remote machine? (ollama serve)";
    }

    opened_.store(true);
    reused_ = false;
    return "";  // Success
}

// ---------------------------------------------------------------------------
// Close the tunnel
// ---------------------------------------------------------------------------

void SshTunnel::close() {
    if (ssh_pid_ > 0 && !reused_) {
        // Send SIGTERM first for clean shutdown
        kill(ssh_pid_, SIGTERM);

        // Wait briefly for clean exit
        int wstatus = 0;
        int tries = 0;
        while (tries < 10) {
            pid_t w = waitpid(ssh_pid_, &wstatus, WNOHANG);
            if (w > 0 || w == -1) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            tries++;
        }

        // Force kill if still alive
        if (tries >= 10) {
            kill(ssh_pid_, SIGKILL);
            waitpid(ssh_pid_, &wstatus, 0);
        }

        ssh_pid_ = -1;
    }
    opened_.store(false);
    reused_ = false;
}

// ---------------------------------------------------------------------------
// Status queries
// ---------------------------------------------------------------------------

bool SshTunnel::is_open() const {
    if (!opened_.load()) return false;
    return is_port_listening();
}

std::string SshTunnel::local_endpoint() const {
    return "http://127.0.0.1:" + std::to_string(config_.local_port);
}

std::string SshTunnel::status() const {
    if (!opened_.load()) {
        return "Tunnel closed";
    }
    if (reused_) {
        return "Reusing existing tunnel on port " + std::to_string(config_.local_port);
    }
    if (is_port_listening()) {
        return "Tunnel active -> " + config_.ssh_user + "@" + config_.ssh_host +
               ":" + std::to_string(config_.remote_port);
    }
    return "Tunnel process running but port not responding";
}

} // namespace stfc
