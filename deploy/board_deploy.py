import os
import tarfile
import paramiko
import time

BOARD_IP = "192.168.10.210"
USER = "root"
PASSWORDS_TO_TRY = ["", "root"]

def create_tar():
    print("[1] Creating deployment package...")
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    tar_path = os.path.join(base_dir, "deploy", "board_package.tar.gz")
    
    focus_app_arm = os.path.join(base_dir, "deploy", "focus_app_arm")
    if not os.path.exists(focus_app_arm):
        print(f"Error: {focus_app_arm} not found. Did you run remote_build.py first?")
        exit(1)
        
    with tarfile.open(tar_path, "w:gz") as tar:
        # Add binary
        tar.add(focus_app_arm, arcname="focus_app")
        # Add service and script
        tar.add(os.path.join(base_dir, "deploy", "focus_app.service"), arcname="focus_app.service")
        tar.add(os.path.join(base_dir, "deploy", "export_logs.sh"), arcname="export_logs.sh")
        # Add models and config
        tar.add(os.path.join(base_dir, "focus_app", "models"), arcname="models")
        tar.add(os.path.join(base_dir, "focus_app", "config"), arcname="config")
        
    print(f"    Created {tar_path}")
    return tar_path

def board_deploy(tar_path):
    print(f"[2] Connecting to {BOARD_IP}...")
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    
    connected = False
    for pwd in PASSWORDS_TO_TRY:
        try:
            client.connect(BOARD_IP, username=USER, password=pwd, timeout=5)
            connected = True
            print(f"    Connected with password '{pwd}'")
            break
        except Exception:
            pass
            
    if not connected:
        print("Error: Could not connect to the board. Is it online?")
        exit(1)
        
    print("[3] Uploading deployment package...")
    sftp = client.open_sftp()
    sftp.put(tar_path, "/tmp/board_package.tar.gz")
    sftp.close()
    
    print("[4] Installing and starting service...")
    commands = """
    systemctl stop focus_app 2>/dev/null || true
    mkdir -p /opt/edgefocus
    tar -xzf /tmp/board_package.tar.gz -C /opt/edgefocus/
    chmod +x /opt/edgefocus/focus_app
    chmod +x /opt/edgefocus/export_logs.sh
    cp /opt/edgefocus/focus_app.service /etc/systemd/system/
    systemctl daemon-reload
    systemctl enable focus_app
    systemctl restart focus_app
    sleep 2
    systemctl --no-pager status focus_app
    """
    
    stdin, stdout, stderr = client.exec_command(f"sh -c '{commands}'")
    
    while True:
        line = stdout.readline()
        if not line:
            break
        print("   ", line.strip())
        
    err = stderr.read().decode()
    if err:
        print("   [WARNINGS/ERRORS]:")
        print("   ", err)
        
    client.close()
    print("[+] Board deployment successful!")

if __name__ == "__main__":
    tar_path = create_tar()
    board_deploy(tar_path)
