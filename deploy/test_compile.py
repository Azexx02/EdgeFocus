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
    print('Compilation successful!')
if __name__ == '__main__':
    deploy()
