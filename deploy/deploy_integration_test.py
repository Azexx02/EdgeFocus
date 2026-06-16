"""Build and deploy integration test to board."""
import os
import zipfile
import paramiko
import time

HOST = "192.168.10.6"
USER = "hu"
PASS = "REDACTED_BUILD_PASSWORD"
BOARD = "192.168.10.210"
REMOTE_DIR = "/home/hu/edgefocus_test"
BOARD_DIR = "/root/tests"

base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
focus_app = os.path.join(base_dir, "focus_app")
zip_path = os.path.join(base_dir, "deploy", "test_integration_src.zip")

SOURCES = [
    "tests/test_integration.cpp",
    "network/BleGattServer.cpp",
    "policy/ReminderPolicy.cpp", "policy/ModeManager.cpp",
    "behavior/SlidingWindow.cpp", "behavior/EventDetector.cpp",
    "behavior/RiskScorer.cpp", "behavior/FocusStateMachine.cpp",
    "behavior/BehaviorEngine.cpp",
    "device/DeviceManager.cpp", "device/ScreenController.cpp",
    "device/BleBandController.cpp", "device/GpioHelper.cpp",
    "storage/SQLiteStore.cpp", "storage/ReportGenerator.cpp",
    "vision/FrameQuality.cpp", "vision/ModelRegistry.cpp",
    "vision/PoseModelAdapter.cpp", "vision/MockVisionSource.cpp",
    "vision/InferenceScheduler.cpp", "vision/VisionEngine.cpp",
    "app/AppContext.cpp", "app/SystemStateStore.cpp", "app/Logger.cpp",
]

# Collect all headers
headers = []
for root, dirs, files in os.walk(focus_app):
    for f in files:
        if f.endswith(".h") or f.endswith(".hpp"):
            rel = os.path.relpath(os.path.join(root, f), focus_app).replace("\\", "/")
            headers.append(rel)

# Generate CMakeLists.txt
cmake_sources = "\n".join(f"    {s}" for s in SOURCES)
cmake = f"""cmake_minimum_required(VERSION 3.14)
project(focus_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(FetchContent)
FetchContent_Declare(sqlite3 URL https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sqlite3)
if(NOT sqlite3_POPULATED)
  FetchContent_Populate(sqlite3)
endif()
add_library(sqlite3_lib STATIC ${{sqlite3_SOURCE_DIR}}/sqlite3.c)
target_include_directories(sqlite3_lib PUBLIC ${{sqlite3_SOURCE_DIR}})
add_executable(test_integration
{cmake_sources}
)
target_include_directories(test_integration PRIVATE ${{CMAKE_CURRENT_SOURCE_DIR}})
find_package(Threads REQUIRED)
target_link_libraries(test_integration PRIVATE Threads::Threads sqlite3_lib)
"""

# Create zip
with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("focus_app/CMakeLists.txt", cmake)
    for s in SOURCES:
        fp = os.path.join(focus_app, s)
        if os.path.exists(fp):
            z.write(fp, "focus_app/" + s)
        else:
            print(f"  WARN: missing {s}")
    for h in headers:
        fp = os.path.join(focus_app, h)
        if os.path.exists(fp):
            z.write(fp, "focus_app/" + h)

print(f"[1] Created {zip_path} ({os.path.getsize(zip_path)} bytes)")

# Build on SDK
print(f"[2] Connecting to {HOST}...")
client = paramiko.SSHClient()
client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
client.connect(HOST, username=USER, password=PASS)

sftp = client.open_sftp()
sftp.put(zip_path, "/home/hu/test_integration_src.zip")
sftp.close()

print("[3] Cross-compiling test_integration...")
commands = """
rm -rf /home/hu/edgefocus_test
mkdir -p /home/hu/edgefocus_test
unzip -o -q /home/hu/test_integration_src.zip -d /home/hu/edgefocus_test
cd /home/hu/edgefocus_test/focus_app
mkdir -p build_arm && cd build_arm
source /opt/st/stm32mp2/5.0.3-snapshot/environment-setup-cortexa35-ostl-linux
cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1
make -j$(nproc) 2>&1
echo "BUILD_RESULT=$?"
"""

stdin, stdout, stderr = client.exec_command(f"bash -c '{commands}'")
output = stdout.read().decode()
for line in output.strip().split("\n")[-5:]:
    print(f"  {line}")

if "BUILD_RESULT=0" not in output:
    print("[FAIL] Build failed")
    err = stderr.read().decode()
    for line in err.strip().split("\n")[-15:]:
        print(f"  [ERR] {line}")
    client.close()
    exit(1)

# Download
stdin, stdout, stderr = client.exec_command(
    "find /home/hu/edgefocus_test/focus_app/build_arm -name test_integration -type f | head -1"
)
remote_bin = stdout.read().decode().strip()
local_bin = os.path.join(os.path.dirname(__file__), "test_integration_arm")
sftp = client.open_sftp()
sftp.get(remote_bin, local_bin)
sftp.close()
client.close()
print(f"[4] Downloaded: {local_bin}")

# Deploy to board
print(f"[5] Deploying to board {BOARD}...")
ssh = paramiko.SSHClient()
ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
ssh.connect(BOARD, username="root", password="", timeout=10)
ssh.exec_command(f"mkdir -p {BOARD_DIR}")
time.sleep(0.3)

sftp = ssh.open_sftp()
sftp.put(local_bin, f"{BOARD_DIR}/test_integration")
sftp.close()
ssh.exec_command(f"chmod +x {BOARD_DIR}/test_integration")
time.sleep(0.5)

print("[6] Running test_integration on board...")
stdin, stdout, stderr = ssh.exec_command(f"cd {BOARD_DIR} && ./test_integration 2>&1", timeout=30)
result = stdout.read().decode()
exit_code = stdout.channel.recv_exit_status()
ssh.close()

print(result)
if exit_code == 0:
    print("[PASS] Integration test passed on board")
else:
    print(f"[FAIL] Integration test failed (exit={exit_code})")
exit(0 if exit_code == 0 else 1)
