#!/usr/bin/env python3
"""drive.py - one-shot CLI client for imgui_harness.h control sockets.

Usage:
  drive.py <sock> wait_ready [timeout_s]     ping until app answers (default 10)
  drive.py <sock> <cmd ...>                  send one command, print reply line
                                             (exits non-zero on ERR/timeout)

Exit code 0 => reply starts with OK. Stdlib only.
"""
import socket
import sys
import time


def send_recv(path, cmd, timeout=15.0):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(path)
    s.sendall((cmd + "\n").encode())
    buf = b""
    while b"\n" not in buf:
        try:
            chunk = s.recv(4096)
        except socket.timeout:
            s.close()
            return "ERR driver-timeout"
        if not chunk:
            break
        buf += chunk
    s.close()
    return buf.decode(errors="replace").strip()


def main():
    if len(sys.argv) < 3:
        print((__doc__ or "").strip())
        return 2
    path, args = sys.argv[1], sys.argv[2:]
    if args[0] == "wait_ready":
        deadline = time.time() + (float(args[1]) if len(args) > 1 else 10.0)
        while time.time() < deadline:
            try:
                r = send_recv(path, "ping", timeout=1.0)
            except (FileNotFoundError, ConnectionRefusedError):
                r = ""
            if r.startswith("OK"):
                print(r)
                return 0
            time.sleep(0.15)
        print("ERR not ready")
        return 1
    reply = send_recv(path, " ".join(args))
    print(reply)
    return 0 if reply.startswith("OK") else 1


if __name__ == "__main__":
    sys.exit(main())
