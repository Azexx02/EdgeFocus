"""
Board module test runner.
Deploy to board via: scp tests/Debug/test_xxx root@192.168.10.210:/root/tests/
Run on board: ssh root@192.168.10.210 "cd /root/tests && ./test_xxx"

This script automates the process for a single test.
Usage: python deploy/run_board_test.py <test_name> [--build]
  --build: also cross-compile before deploying (requires ARM SDK)
"""
import paramiko
import sys
import time
import os

BOARD = os.environ.get("EDGEFOCUS_BOARD_IP", "192.168.10.210")
BOARD_USER = "root"
BOARD_PASS = ""
BOARD_TEST_DIR = "/root/tests"

TESTS = {
    "test_storage": {
        "desc": "SQLiteStore + ReportGenerator",
        "timeout": 15,
    },
    "test_behavior_fsm": {
        "desc": "FocusStateMachine + EventDetector + RiskScorer",
        "timeout": 10,
    },
    "test_reminder_policy": {
        "desc": "ReminderPolicy + ModeManager",
        "timeout": 10,
    },
    "test_device_manager": {
        "desc": "DeviceManager + ScreenController + BleBand",
        "timeout": 15,
    },
    "test_vision_result": {
        "desc": "VisionResult + MockVisionSource + InferenceScheduler",
        "timeout": 15,
    },
    "test_ble_gatt": {
        "desc": "BleGattServer API commands",
        "timeout": 10,
    },
}


def deploy_binary(test_name, binary_path):
    """Upload binary to board."""
    print(f"  Uploading {test_name} to {BOARD}...")
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(BOARD, username=BOARD_USER, password=BOARD_PASS, timeout=10)

    ssh.exec_command(f"mkdir -p {BOARD_TEST_DIR}")
    time.sleep(0.3)

    sftp = ssh.open_sftp()
    remote_path = f"{BOARD_TEST_DIR}/{test_name}"
    sftp.put(binary_path, remote_path)
    sftp.close()

    ssh.exec_command(f"chmod +x {remote_path}")
    ssh.close()
    print(f"  Deployed to {remote_path}")


def run_on_board(test_name, timeout=15):
    """Run test on board and capture output."""
    print(f"  Running {test_name} on board (timeout={timeout}s)...")
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(BOARD, username=BOARD_USER, password=BOARD_PASS, timeout=10)

    remote_path = f"{BOARD_TEST_DIR}/{test_name}"
    stdin, stdout, stderr = ssh.exec_command(
        f"cd {BOARD_TEST_DIR} && ./{test_name} 2>&1",
        timeout=timeout
    )

    output = stdout.read().decode()
    err = stderr.read().decode()
    exit_code = stdout.channel.recv_exit_status()

    ssh.close()

    print(f"\n  --- {test_name} output ---")
    if output:
        print(output)
    if err:
        print(f"  [STDERR]: {err}")

    if exit_code == 0:
        print(f"  --- {test_name}: PASS ---\n")
    else:
        print(f"  --- {test_name}: FAIL (exit={exit_code}) ---\n")

    return exit_code == 0


def main():
    if len(sys.argv) < 2:
        print("Usage: python deploy/run_board_test.py <test_name> [--build]")
        print(f"Available tests:")
        for name, info in TESTS.items():
            print(f"  {name}: {info['desc']}")
        print("\n  Use 'all' to run all tests sequentially")
        sys.exit(1)

    test_name = sys.argv[1]
    do_build = "--build" in sys.argv

    if do_build:
        print("[BUILD] Cross-compiling...")
        os.system(f"python deploy/build_test.py {test_name}")

    if test_name == "all":
        results = {}
        for t in TESTS:
            print(f"\n{'='*50}")
            print(f"  Module: {t} ({TESTS[t]['desc']})")
            print(f"{'='*50}")
            binary = f"deploy/{t}_arm"
            if not os.path.exists(binary):
                print(f"  [SKIP] Binary not found: {binary}")
                print(f"  Run: python deploy/build_test.py {t}")
                results[t] = "SKIP"
                continue
            deploy_binary(t, binary)
            ok = run_on_board(t, TESTS[t]["timeout"])
            results[t] = "PASS" if ok else "FAIL"

        print(f"\n{'='*50}")
        print("  MODULE TEST RESULTS")
        print(f"{'='*50}")
        for t, r in results.items():
            icon = "PASS" if r == "PASS" else "FAIL" if r == "FAIL" else "SKIP"
            print(f"  [{icon}] {t}: {TESTS[t]['desc']}")
        print(f"{'='*50}")
    else:
        if test_name not in TESTS:
            print(f"Unknown test: {test_name}")
            print(f"Available: {', '.join(TESTS.keys())}")
            sys.exit(1)

        binary = f"deploy/{test_name}_arm"
        if not os.path.exists(binary):
            print(f"  Binary not found: {binary}")
            print(f"  Run first: python deploy/build_test.py {test_name}")
            sys.exit(1)

        deploy_binary(test_name, binary)
        ok = run_on_board(test_name, TESTS[test_name]["timeout"])
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
