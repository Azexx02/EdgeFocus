import paramiko
import sys

def start_bg():
    ssh = paramiko.SSHClient()
    ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    ssh.connect('192.168.10.210', username='root', password='', timeout=5)
    
    # Check if focus_app is already running and kill it
    ssh.exec_command('pkill -9 focus_app')
    ssh.exec_command('systemctl stop lcd-spi-touch-test.service')
    ssh.exec_command('systemctl stop lcd-spi-touch-test.timer')
    ssh.exec_command('systemctl disable lcd-spi-touch-test.timer')
    
    print("Starting focus_app in background...")
    # Run permanently using nohup
    ssh.exec_command('cd /root && nohup ./focus_app > /root/focus_app.log 2>&1 &')
    
    ssh.close()
    print("App is now running continuously on the board.")

if __name__ == "__main__":
    start_bg()
