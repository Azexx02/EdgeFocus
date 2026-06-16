"""
Per-module board test deployment script.
Usage: python deploy/build_test.py <test_name>
Example: python deploy/build_test.py test_storage
         python deploy/build_test.py test_all

Builds individual test binaries on ARM SDK server, deploys to board, runs and reports.
Each test is compiled with its own minimal CMakeLists.txt — no other targets.
"""
import os
import sys
import zipfile
import paramiko
import time
import tempfile

HOST = "192.168.10.6"
USER = "hu"
PASS = "REDACTED_BUILD_PASSWORD"
BOARD = "192.168.10.210"
BOARD_USER = "root"
BOARD_PASS = ""
REMOTE_DIR = "/home/hu/edgefocus_test"
BOARD_DIR = "/root/tests"

# Per-test: sources needed (relative to focus_app/)
TEST_DEPS = {
    "test_storage": {
        "sources": [
            "storage/SQLiteStore.cpp",
            "storage/ReportGenerator.cpp",
            "app/AppContext.cpp",
            "app/Logger.cpp",
            "tests/test_storage.cpp",
        ],
        "headers": [
            "storage/StorageEvent.h", "storage/schema.h",
            "storage/SQLiteStore.h", "storage/ReportGenerator.h",
            "app/Module.h", "app/BoundedQueue.h",
            "app/AppContext.h", "app/SystemStateStore.h", "app/Logger.h",
            "behavior/FocusState.h", "policy/InterventionCommand.h",
            "policy/ModeManager.h",
        ],
        "extra_libs": "sqlite3_lib",
        "timeout": 15,
    },
    "test_behavior_fsm": {
        "sources": [
            "behavior/SlidingWindow.cpp",
            "behavior/EventDetector.cpp",
            "behavior/RiskScorer.cpp",
            "behavior/FocusStateMachine.cpp",
            "tests/test_behavior_fsm.cpp",
        ],
        "headers": [
            "behavior/FocusState.h", "behavior/EventDetector.h",
            "behavior/SlidingWindow.h", "behavior/RiskScorer.h",
            "behavior/FocusStateMachine.h",
            "vision/VisionResult.h", "vision/MockVisionSource.h",
            "config/BehaviorConfig.h",
        ],
        "extra_libs": "",
        "timeout": 10,
    },
    "test_reminder_policy": {
        "sources": [
            "policy/ReminderPolicy.cpp",
            "policy/ModeManager.cpp",
            "device/DeviceManager.cpp",
            "device/ScreenController.cpp",
            "device/BleBandController.cpp",
            "device/GpioHelper.cpp",
            "storage/SQLiteStore.cpp",
            "app/AppContext.cpp",
            "app/SystemStateStore.cpp",
            "app/Logger.cpp",
            "tests/test_reminder_policy.cpp",
        ],
        "headers": [
            "policy/ReminderPolicy.h", "policy/ModeManager.h",
            "policy/InterventionCommand.h",
            "device/DeviceManager.h", "device/ScreenController.h",
            "device/BleBandController.h", "device/GpioHelper.h",
            "storage/StorageEvent.h", "storage/schema.h",
            "storage/SQLiteStore.h", "storage/ReportGenerator.h",
            "app/Module.h", "app/BoundedQueue.h",
            "app/AppContext.h", "app/SystemStateStore.h", "app/Logger.h",
            "behavior/FocusState.h",
        ],
        "extra_libs": "sqlite3_lib",
        "timeout": 10,
    },
    "test_device_manager": {
        "sources": [
            "device/DeviceManager.cpp",
            "device/ScreenController.cpp",
            "device/BleBandController.cpp",
            "device/GpioHelper.cpp",
            "app/AppContext.cpp",
            "app/SystemStateStore.cpp",
            "app/Logger.cpp",
            "tests/test_device_manager.cpp",
        ],
        "headers": [
            "device/DeviceManager.h", "device/ScreenController.h",
            "device/BleBandController.h", "device/GpioHelper.h",
            "policy/InterventionCommand.h",
            "app/Module.h", "app/BoundedQueue.h",
            "app/AppContext.h", "app/SystemStateStore.h", "app/Logger.h",
            "behavior/FocusState.h", "policy/ModeManager.h",
            "storage/ReportGenerator.h",
        ],
        "extra_libs": "",
        "timeout": 15,
    },
    "test_vision_result": {
        "sources": [
            "vision/FrameQuality.cpp",
            "vision/ModelRegistry.cpp",
            "vision/PoseModelAdapter.cpp",
            "vision/MockVisionSource.cpp",
            "vision/InferenceScheduler.cpp",
            "vision/VisionEngine.cpp",
            "app/AppContext.cpp",
            "app/SystemStateStore.cpp",
            "app/Logger.cpp",
            "tests/test_vision_result.cpp",
        ],
        "headers": [
            "vision/VisionResult.h", "vision/ModelAdapter.h",
            "vision/ModelRegistry.h", "vision/FrameQuality.h",
            "vision/PoseModelAdapter.h", "vision/MockVisionSource.h",
            "vision/InferenceScheduler.h", "vision/VisionEngine.h",
            "vision/stai_mpu_wrapper.hpp", "vision/pose_keypoint_pp.hpp",
            "camera/CameraFrame.h", "camera/CameraPipeline.h",
            "app/Module.h", "app/BoundedQueue.h",
            "app/AppContext.h", "app/SystemStateStore.h", "app/Logger.h",
            "behavior/FocusState.h", "policy/InterventionCommand.h",
            "policy/ModeManager.h", "storage/ReportGenerator.h",
            "config/BehaviorConfig.h",
        ],
        "extra_libs": "",
        "timeout": 15,
    },
    "test_ble_gatt": {
        "sources": [
            "network/BleGattServer.cpp",
            "policy/ReminderPolicy.cpp",
            "policy/ModeManager.cpp",
            "device/DeviceManager.cpp",
            "device/ScreenController.cpp",
            "device/BleBandController.cpp",
            "device/GpioHelper.cpp",
            "storage/SQLiteStore.cpp",
            "storage/ReportGenerator.cpp",
            "app/AppContext.cpp",
            "app/SystemStateStore.cpp",
            "app/Logger.cpp",
            "tests/test_ble_gatt.cpp",
        ],
        "headers": [
            "network/BleGattServer.h",
            "policy/ReminderPolicy.h", "policy/ModeManager.h",
            "policy/InterventionCommand.h",
            "device/DeviceManager.h", "device/ScreenController.h",
            "device/BleBandController.h", "device/GpioHelper.h",
            "storage/StorageEvent.h", "storage/schema.h",
            "storage/SQLiteStore.h", "storage/ReportGenerator.h",
            "app/Module.h", "app/BoundedQueue.h",
            "app/AppContext.h", "app/SystemStateStore.h", "app/Logger.h",
            "behavior/FocusState.h", "behavior/EventDetector.h",
            "behavior/SlidingWindow.h",
            "vision/VisionResult.h", "vision/MockVisionSource.h",
            "camera/CameraFrame.h",
            "config/BehaviorConfig.h",
        ],
        "extra_libs": "sqlite3_lib",
        "timeout": 10,
    },
}

TESTS = list(TEST_DEPS.keys())


def generate_cmake(test_name, deps):
    """Generate a minimal CMakeLists.txt for a single test."""
    sources_list = "\n".join(f"    {s}" for s in deps["sources"])
    extra_libs = deps.get("extra_libs", "")

    cmake = f"""cmake_minimum_required(VERSION 3.14)
project(focus_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

include(FetchContent)
FetchContent_Declare(sqlite3 URL https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sqlite3)
if(NOT sqlite3_POPULATED)
  FetchContent_Populate(sqlite3)
endif()

add_library(sqlite3_lib STATIC ${{sqlite3_SOURCE_DIR}}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${{sqlite3_SOURCE_DIR}})

add_executable({test_name}
{sources_list}
)
target_include_directories({test_name} PRIVATE ${{CMAKE_CURRENT_SOURCE_DIR}})

find_package(Threads REQUIRED)
target_link_libraries({test_name} PRIVATE Threads::Threads)
"""

    if extra_libs:
        cmake += f"target_link_libraries({test_name} PRIVATE {extra_libs})\n"

    return cmake


def create_test_zip(test_name):
    """Create a minimal source zip for a single test."""
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    focus_app_dir = os.path.join(base_dir, "focus_app")
    zip_path = os.path.join(base_dir, "deploy", f"{test_name}_src.zip")

    if test_name not in TEST_DEPS:
        print(f"Unknown test: {test_name}")
        return None

    deps = TEST_DEPS[test_name]
    cmake_content = generate_cmake(test_name, deps)

    # Collect all files
    all_files = set(deps["sources"] + deps["headers"])

    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zipf:
        # Write generated CMakeLists.txt
        zipf.writestr("focus_app/CMakeLists.txt", cmake_content)

        # Write source and header files
        for rel_path in all_files:
            full_path = os.path.join(focus_app_dir, rel_path)
            if os.path.exists(full_path):
                arcname = "focus_app/" + rel_path.replace('\\', '/')
                zipf.write(full_path, arcname)
            else:
                print(f"  [WARN] Missing: {rel_path}")

    print(f"  Created {zip_path} ({os.path.getsize(zip_path)} bytes)")
    return zip_path


def remote_build_test(zip_path, test_name):
    """Cross-compile the test on ARM SDK server."""
    print(f"[2] Connecting to {HOST}...")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS)

    print("[3] Uploading source...")
    sftp = client.open_sftp()
    remote_zip = f"/home/hu/{os.path.basename(zip_path)}"
    sftp.put(zip_path, remote_zip)

    print(f"[4] Cross-compiling {test_name}...")
    commands = f"""
    rm -rf {REMOTE_DIR}
    mkdir -p {REMOTE_DIR}
    unzip -o -q {remote_zip} -d {REMOTE_DIR}
    cd {REMOTE_DIR}/focus_app
    mkdir -p build_arm
    cd build_arm
    source /opt/st/stm32mp2/5.0.3-snapshot/environment-setup-cortexa35-ostl-linux
    cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1
    make -j$(nproc) 2>&1
    echo "BUILD_RESULT=$?"
    """

    stdin, stdout, stderr = client.exec_command(f"bash -c '{commands}'")
    output = stdout.read().decode()
    err = stderr.read().decode()

    for line in output.strip().split('\n'):
        print(f"   {line}")

    if "BUILD_RESULT=0" not in output:
        print(f"  [FAIL] Build failed for {test_name}")
        if err:
            for line in err.strip().split('\n')[-20:]:
                print(f"  [ERR] {line}")
        client.close()
        return None

    # Find the compiled binary
    stdin, stdout, stderr = client.exec_command(
        f"find {REMOTE_DIR}/focus_app/build_arm -name '{test_name}' -type f | head -1"
    )
    remote_bin = stdout.read().decode().strip()

    if not remote_bin:
        print(f"  [FAIL] Binary not found for {test_name}")
        client.close()
        return None

    # Download
    local_bin = os.path.join(os.path.dirname(__file__), f"{test_name}_arm")
    sftp.get(remote_bin, local_bin)
    print(f"  Downloaded: {local_bin}")

    sftp.close()
    client.close()
    return local_bin


def deploy_and_run(local_bin, test_name):
    """Deploy binary to board and run it."""
    print(f"[5] Deploying to board {BOARD}...")
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(BOARD, username=BOARD_USER, password=BOARD_PASS, timeout=10)

    ssh.exec_command(f"mkdir -p {BOARD_DIR}")
    time.sleep(0.3)

    sftp = ssh.open_sftp()
    remote_path = f"{BOARD_DIR}/{test_name}"
    sftp.put(local_bin, remote_path)
    sftp.close()

    ssh.exec_command(f"chmod +x {remote_path}")
    time.sleep(0.5)

    timeout = TEST_DEPS[test_name]["timeout"]
    print(f"[6] Running {test_name} on board (timeout={timeout}s)...")
    stdin, stdout, stderr = ssh.exec_command(f"cd {BOARD_DIR} && ./{test_name} 2>&1", timeout=timeout)

    output = stdout.read().decode()
    err_output = stderr.read().decode()
    exit_code = stdout.channel.recv_exit_status()

    ssh.close()

    if output:
        print(output)

    if exit_code == 0:
        print(f"  [PASS] {test_name}")
    else:
        print(f"  [FAIL] {test_name} (exit={exit_code})")
        if err_output:
            print(f"  [STDERR]: {err_output[-500:]}")

    return exit_code == 0


def main():
    if len(sys.argv) < 2:
        print("Usage: python deploy/build_test.py <test_name>")
        print(f"Available: {', '.join(TESTS)}")
        print("  Or 'test_all' for all tests")
        sys.exit(1)

    test_name = sys.argv[1]

    if test_name == "test_all":
        results = {}
        for t in TESTS:
            print(f"\n{'='*60}")
            print(f"  {t}")
            print(f"{'='*60}")
            zip_path = create_test_zip(t)
            if not zip_path:
                results[t] = "SKIP"
                continue
            local_bin = remote_build_test(zip_path, t)
            if not local_bin:
                results[t] = "BUILD_FAIL"
                continue
            ok = deploy_and_run(local_bin, t)
            results[t] = "PASS" if ok else "FAIL"

        print(f"\n{'='*60}")
        print("  RESULTS")
        print(f"{'='*60}")
        for t, r in results.items():
            print(f"  [{r}] {t}")
        print(f"{'='*60}")
    else:
        if test_name not in TEST_DEPS:
            print(f"Unknown test: {test_name}")
            print(f"Available: {', '.join(TESTS)}")
            sys.exit(1)

        zip_path = create_test_zip(test_name)
        if not zip_path:
            sys.exit(1)
        local_bin = remote_build_test(zip_path, test_name)
        if not local_bin:
            sys.exit(1)
        ok = deploy_and_run(local_bin, test_name)
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
