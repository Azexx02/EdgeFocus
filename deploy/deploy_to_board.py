import paramiko
import os
import zipfile
import stat
import time

def create_zip(zip_path, folder_path):
    print(f"Creating {zip_path} from {folder_path}...")
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zipf:
        for root, dirs, files in os.walk(folder_path):
            if 'build' in root or '.git' in root or '__pycache__' in root:
                continue
            for file in files:
                if file.endswith('.zip') or file.endswith('.exe') or file.endswith('.lib') or file.endswith('.obj') or file.endswith('.pdb'):
                    continue
                file_path = os.path.join(root, file)
                arcname = os.path.relpath(file_path, folder_path)
                zipf.write(file_path, arcname)

def run_ssh_cmd(ssh, cmd):
    print(f"Running: {cmd}")
    stdin, stdout, stderr = ssh.exec_command(cmd)
    exit_status = stdout.channel.recv_exit_status()
    out = stdout.read().decode()
    err = stderr.read().decode()
    if out: print("STDOUT:", out)
    if err: print("STDERR:", err)
    return exit_status, out

def deploy():
    # 1. Zip local folder
    folder_to_zip = r"C:\Users\Aze\Desktop\edgefocus(1)\开发板部分代码\focus_app"
    zip_file = "focus_app_src.zip"
    create_zip(zip_file, folder_to_zip)

    # 2. Connect to .6
    print("Connecting to 192.168.10.6...")
    ssh6 = paramiko.SSHClient()
    ssh6.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh6.connect('192.168.10.6', username='hu', password='REDACTED_BUILD_PASSWORD', timeout=10)
    
    sftp6 = ssh6.open_sftp()
    print("Uploading zip to .6...")
    sftp6.put(zip_file, '/home/hu/focus_app_src.zip')
    
    print("Extracting and compiling on .6...")
    cmd = """
    cd /home/hu
    rm -rf focus_app_cross
    mkdir focus_app_cross
    cd focus_app_cross
    unzip -q ../focus_app_src.zip
    source /opt/st/stm32mp2/5.0.3-snapshot/environment-setup-cortexa35-ostl-linux
    cmake -B build_arm -DCMAKE_BUILD_TYPE=Release
    cmake --build build_arm -j4
    """
    status, _ = run_ssh_cmd(ssh6, cmd)
    if status != 0:
        print("Build failed on .6!")
        return

    print("Downloading compiled binary to Windows...")
    sftp6.get('/home/hu/focus_app_cross/build_arm/focus_app', 'focus_app_arm_bin')
    sftp6.close()
    ssh6.close()

    # 3. Connect to .210
    print("Connecting to 192.168.10.210...")
    ssh210 = paramiko.SSHClient()
    ssh210.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh210.connect('192.168.10.210', username='root', password='', timeout=10)
    
    print("Killing existing focus_app on .210...")
    run_ssh_cmd(ssh210, "pkill -9 focus_app")
    
    sftp210 = ssh210.open_sftp()
    print("Uploading binary to .210...")
    sftp210.put('focus_app_arm_bin', '/root/focus_app')
    sftp210.chmod('/root/focus_app', stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
    
    # 4. Disable lcd service and run app
    print("Disabling lcd-spi-touch-test.service on .210...")
    run_ssh_cmd(ssh210, "systemctl stop lcd-spi-touch-test.service")
    run_ssh_cmd(ssh210, "systemctl disable lcd-spi-touch-test.service")
    run_ssh_cmd(ssh210, "systemctl stop lcd-spi-touch-test.timer")
    run_ssh_cmd(ssh210, "systemctl disable lcd-spi-touch-test.timer")
    
    # Upload scripts/send_ble_signal.sh to /root/scripts/
    run_ssh_cmd(ssh210, "mkdir -p /root/scripts")
    sftp210.put(os.path.join(folder_to_zip, 'scripts', 'send_ble_signal.sh'), '/root/scripts/send_ble_signal.sh')
    sftp210.chmod('/root/scripts/send_ble_signal.sh', 0o755)
    sftp210.close()
    
    print("Running focus_app continuously on .210...")
    run_ssh_cmd(ssh210, "cd /root && nohup ./focus_app > /root/focus_app.log 2>&1 &")
    
    ssh210.close()
    print("\nDeployment and test complete. App is running in the background.")

if __name__ == "__main__":
    deploy()
