import os
import zipfile
import paramiko
import time

HOST = "192.168.10.6"
USER = "hu"
PASS = "REDACTED_BUILD_PASSWORD"
REMOTE_DIR = "/home/hu/edgefocus_build"
ZIP_NAME = "focus_app_src.zip"
REMOTE_ZIP = f"/home/hu/{ZIP_NAME}"
DEMO_HTTP = os.environ.get("EDGEFOCUS_DEPLOY_DEMO_HTTP", "OFF").upper() in ("1", "ON", "TRUE", "YES")

def create_zip():
    print("[1] Creating source zip...")
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    focus_app_dir = os.path.join(base_dir, "focus_app")
    zip_path = os.path.join(base_dir, "deploy", ZIP_NAME)
    
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zipf:
        for root, dirs, files in os.walk(focus_app_dir):
            if "build" in root or "__pycache__" in root:
                continue
            for file in files:
                if file.endswith(".zip") or file.endswith(".exe") or file.endswith(".obj"):
                    continue
                file_path = os.path.join(root, file)
                # Ensure the arcname starts with focus_app/
                arcname = "focus_app/" + os.path.relpath(file_path, focus_app_dir).replace('\\', '/')
                zipf.write(file_path, arcname)
    print(f"    Created {zip_path}")
    return zip_path

def remote_build(zip_path):
    print(f"[2] Connecting to {HOST}...")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    client.connect(HOST, username=USER, password=PASS)
    
    print("[3] Uploading source zip...")
    sftp = client.open_sftp()
    sftp.put(zip_path, REMOTE_ZIP)
    
    print("[4] Compiling on remote host (this may take a minute)...")
    commands = f"""
    rm -rf {REMOTE_DIR}
    mkdir -p {REMOTE_DIR}
    unzip -o -q {REMOTE_ZIP} -d {REMOTE_DIR}
    cd {REMOTE_DIR}/focus_app
    mkdir -p build_arm
    cd build_arm
    source /opt/st/stm32mp2/5.0.3-snapshot/environment-setup-cortexa35-ostl-linux
    cmake .. -DEDGEFOCUS_WITH_STAI_MPU=ON -DEDGEFOCUS_WITH_OPENCV=ON -DEDGEFOCUS_WITH_DEMO_HTTP={'ON' if DEMO_HTTP else 'OFF'} -DCMAKE_BUILD_TYPE=Release
    make -j$(nproc)
    """
    
    stdin, stdout, stderr = client.exec_command(f"bash -c '{commands}'")
    
    # Print output as it comes
    while True:
        line = stdout.readline()
        if not line:
            break
        print("   ", line.strip())
        
    err = stderr.read().decode()
    if err:
        print("   [ERRORS]:")
        print(err)
        
    status = stdout.channel.recv_exit_status()
    if status != 0:
        print("Compilation failed!")
        client.close()
        exit(1)
        
    print("[5] Downloading compiled binary...")
    
    stdin, stdout, stderr = client.exec_command(f"find {REMOTE_DIR} -name focus_app -type f | grep build_arm")
    remote_bin = stdout.read().decode().strip()
    
    if not remote_bin:
        print("Error: Could not find compiled focus_app binary.")
        client.close()
        exit(1)
        
    local_bin = os.path.join(os.path.dirname(__file__), "focus_app_arm")
    sftp.get(remote_bin, local_bin)
    print(f"    Downloaded to {local_bin}")
    
    sftp.close()
    client.close()
    print("[+] Remote build successful!")

if __name__ == "__main__":
    zip_path = create_zip()
    remote_build(zip_path)
