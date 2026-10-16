#!/usr/bin/env python3
"""Run a test command with one owned Redis Unix socket, then stop that server.

Linux CI uses the same Redis image as its TCP service. Native POSIX validation can
pass --redis-server instead. No default port, system service or existing data is used.
"""

import argparse
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
import uuid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--redis-server", type=Path)
    source.add_argument("--docker-image")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a test command after -- is required")
    if args.docker_image and not sys.platform.startswith("linux"):
        parser.error("Docker Unix socket sharing requires a native Linux host")

    def interrupted(signum, _frame):
        raise SystemExit(128 + signum)

    old_term = signal.signal(signal.SIGTERM, interrupted)
    try:
        # Docker must mount the runner's shared temp root, not a container-local
        # /tmp. Native runs keep the short /tmp fallback used on macOS as well.
        temp_root = os.environ.get("RUNNER_TEMP", "/tmp") if args.docker_image else "/tmp"
        if not Path(temp_root).is_absolute():
            parser.error("Docker RUNNER_TEMP must be an absolute directory")
        with tempfile.TemporaryDirectory(prefix="qbr-", dir=temp_root) as directory:
            path = Path(directory) / "redis.sock"
            # sockaddr_un includes the terminating NUL: Linux has 108 bytes,
            # macOS 104. Use the smaller bound on other POSIX hosts.
            capacity = 108 if sys.platform.startswith("linux") else 104
            path_bytes = len(os.fsencode(path))
            if path_bytes >= capacity:
                raise RuntimeError(
                    f"Redis Unix socket path is too long: {path_bytes} bytes "
                    f"(maximum {capacity - 1}): {path}")
            server = None
            child = None
            container = None
            child_result = None
            server_log = (Path(directory) / "redis.log").open("w+")
            try:
                options = ["--port", "0", "--unixsocketperm", "700", "--save", "",
                           "--appendonly", "no", "--protected-mode", "yes"]
                if args.docker_image:
                    # Know our private identity even if docker run is interrupted
                    # after starting Redis but before it returns the container ID.
                    container = "qbr-" + uuid.uuid4().hex
                    output = subprocess.check_output(
                        ["docker", "run", "--rm", "-d", "--name", container,
                         "--network", "none",
                         "--user", f"{os.getuid()}:{os.getgid()}",
                         "--mount", f"type=bind,src={directory},dst=/data",
                         "--entrypoint", "redis-server", args.docker_image, *options,
                         "--unixsocket", "/data/redis.sock", "--dir", "/data"],
                        text=True, timeout=90).strip()
                    if not re.fullmatch(r"[0-9a-f]{64}", output):
                        raise RuntimeError("docker did not return an exact container identity")
                    container = output
                else:
                    server = subprocess.Popen(
                        [str(args.redis_server.resolve()), *options, "--unixsocket", str(path),
                         "--dir", directory], stdout=server_log, stderr=subprocess.STDOUT)

                deadline = time.monotonic() + 20
                while True:
                    try:
                        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
                            probe.settimeout(1)
                            probe.connect(str(path))
                            probe.sendall(b"*1\r\n$4\r\nPING\r\n")
                            reply = b""
                            while not reply.endswith(b"\r\n") and len(reply) < 64:
                                part = probe.recv(64 - len(reply))
                                if not part:
                                    break
                                reply += part
                            if reply != b"+PONG\r\n":
                                raise RuntimeError(f"private Redis PING returned {reply!r}")
                        break
                    except OSError:
                        if server is not None and server.poll() is not None:
                            raise RuntimeError("private redis-server exited before readiness")
                        if time.monotonic() >= deadline:
                            raise RuntimeError("private Redis Unix socket did not become ready")
                        time.sleep(0.1)
                print("Private Redis Unix socket: PING/PONG verified", flush=True)
                env = dict(os.environ, REDIS_UNIX_SOCKET=str(path))
                child = subprocess.Popen(command, env=env, start_new_session=True)
                child_result = child.wait()
                return child_result
            finally:
                primary_error = sys.exc_info()[1]
                cleanup_errors = []
                # Cleanup is bounded; another termination signal must not skip
                # the remaining owned resources while the first is handled.
                signal.signal(signal.SIGTERM, signal.SIG_IGN)

                def cleanup(action):
                    try:
                        action()
                    except Exception as error:
                        cleanup_errors.append(error)

                def stop_process(process, group=False):
                    if process is None or process.poll() is not None:
                        return
                    for signum in (signal.SIGTERM, signal.SIGKILL):
                        try:
                            if group:
                                os.killpg(process.pid, signum)
                            else:
                                process.send_signal(signum)
                        except ProcessLookupError:
                            pass  # exited between poll() and the signal
                        try:
                            process.wait(timeout=5)
                            return
                        except subprocess.TimeoutExpired:
                            if signum == signal.SIGKILL:
                                raise

                def remove_container():
                    if container is None:
                        return
                    removed = subprocess.run(
                        ["docker", "rm", "-f", container], capture_output=True,
                        text=True, timeout=20)
                    # A failed launch or --rm may already have removed our own
                    # container. Other Docker failures remain cleanup errors.
                    absent = f"Error response from daemon: No such container: {container}"
                    if removed.returncode == 1 and removed.stderr.strip() == absent:
                        return
                    removed.check_returncode()

                cleanup(lambda: stop_process(child, group=True))
                cleanup(lambda: stop_process(server))
                cleanup(remove_container)

                def report_server_log():
                    if primary_error is not None or cleanup_errors:
                        server_log.seek(0)
                        print(server_log.read(), file=sys.stderr)

                cleanup(report_server_log)
                cleanup(server_log.close)
                for error in cleanup_errors:
                    print(f"Private Redis cleanup failed: {error}", file=sys.stderr)
                    if getattr(error, "stderr", None):
                        print(error.stderr, file=sys.stderr)
                # Keep a failing child/primary exception as the verdict, with
                # secondary failures logged. Cleanup failure alone must fail.
                if cleanup_errors and primary_error is None and not child_result:
                    raise cleanup_errors[0]
    finally:
        signal.signal(signal.SIGTERM, old_term)


if __name__ == "__main__":
    sys.exit(main())
