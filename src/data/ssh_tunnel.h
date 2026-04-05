#pragma once

#include <string>
#include <atomic>
#include <sys/types.h>

namespace stfc {

// ---------------------------------------------------------------------------
// SSH Tunnel Manager
//
// Manages an SSH tunnel (ssh -L) as a child process. The tunnel forwards a
// local port to a remote host:port through an SSH connection. Used to reach
// Ollama running on a home server without exposing it to the internet.
//
// Lifecycle:
//   1. SshTunnel tunnel(config);
//   2. tunnel.open();           // spawns ssh -L in background
//   3. ... use localhost:local_port ...
//   4. tunnel.close();          // kills the ssh process
//   5. Destructor also calls close() for safety
//
// The tunnel checks if the port is already in use (e.g. from a manually
// opened tunnel) and reuses it rather than spawning a duplicate.
// ---------------------------------------------------------------------------

struct SshTunnelConfig {
    std::string ssh_user = "groot";              // SSH username
    std::string ssh_host = "192.168.1.225";      // SSH server IP/hostname
    int ssh_port = 22;                           // SSH port
    int local_port = 11434;                      // Local port to forward
    std::string remote_host = "localhost";        // Remote host (from SSH server's perspective)
    int remote_port = 11434;                     // Remote port on remote_host
    int connect_timeout = 10;                    // SSH connection timeout (seconds)
    std::string ssh_key_path = "";               // Path to SSH private key (empty = use default)
    bool strict_host_checking = false;           // StrictHostKeyChecking (false for convenience)
};

class SshTunnel {
public:
    explicit SshTunnel(const SshTunnelConfig& config);
    ~SshTunnel();

    // Open the tunnel. Returns empty string on success, error message on failure.
    std::string open();

    // Close the tunnel (kills the ssh process).
    void close();

    // Check if the tunnel is currently open and the local port is reachable.
    bool is_open() const;

    // Get the local endpoint string (e.g. "http://localhost:11434")
    std::string local_endpoint() const;

    // Get status description for display
    std::string status() const;

private:
    SshTunnelConfig config_;
    pid_t ssh_pid_ = -1;                         // PID of the ssh child process
    std::atomic<bool> opened_{false};             // Whether we spawned the tunnel
    bool reused_ = false;                        // Whether we found an existing tunnel

    // Check if the local port is already listening
    bool is_port_listening() const;

    // Wait for the port to become available after spawning ssh
    bool wait_for_port(int timeout_seconds) const;
};

} // namespace stfc
