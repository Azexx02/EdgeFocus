import paramiko
import stat
import time

def deploy():
    print("Connecting to 192.168.10.210...")
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect('192.168.10.210', username='root', password='', timeout=10)
    
    print("Stopping systemd focus_app service...")
    ssh.exec_command("systemctl stop focus_app")
    ssh.exec_command("systemctl disable focus_app")
    ssh.exec_command("pkill -9 focus_app")
    time.sleep(2)
    ssh.exec_command("rm -f /root/focus_app")
    time.sleep(1)
    
    sftp = ssh.open_sftp()
    print("Uploading fixed binary to /root/focus_app...")
    sftp.put('c:/Users/Aze/Desktop/edgefocus(1)/开发板部分代码/deploy/focus_app_arm_bin', '/root/focus_app')
    sftp.chmod('/root/focus_app', stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IXOTH)
    
    print("Running focus_app...")
    ssh.exec_command("cd /root && nohup ./focus_app > focus_app.log 2>&1 &")
    
    sftp.close()
    ssh.close()
    print("Done!")

if __name__ == "__main__":
    deploy()
