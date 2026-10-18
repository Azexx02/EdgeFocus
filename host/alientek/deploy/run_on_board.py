import paramiko
import time
import sys
import os

BOARD_IP = os.environ.get("EDGEFOCUS_BOARD_IP", "192.168.10.210")

def check_board():
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect(BOARD_IP, username='root', password='', timeout=5)
    
    # Check if focus_app is already running and kill it
    ssh.exec_command('pkill -9 focus_app')
    time.sleep(1)
    
    print("Starting focus_app...")
    stdin, stdout, stderr = ssh.exec_command('cd /root && ./focus_app')
    
    timeout = time.time() + 20  # run for 20 seconds
    try:
        while time.time() < timeout:
            if stdout.channel.recv_ready():
                print(stdout.channel.recv(1024).decode(), end='', flush=True)
            if stderr.channel.recv_ready():
                print("ERR:", stderr.channel.recv(1024).decode(), end='', flush=True)
            if stdout.channel.exit_status_ready():
                print("Process exited with:", stdout.channel.recv_exit_status())
                break
            time.sleep(0.1)
    except Exception as e:
        print("Exception:", e)
    finally:
        ssh.exec_command('pkill -9 focus_app')
        ssh.close()
        print("\nDone")

if __name__ == "__main__":
    check_board()
