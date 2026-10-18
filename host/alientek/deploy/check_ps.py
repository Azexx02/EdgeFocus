import os
import paramiko

client = paramiko.SSHClient()
client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
client.connect('192.168.10.6', username='hu', password=os.environ.get("EDGEFOCUS_BUILD_PASS", ""))
stdin, stdout, stderr = client.exec_command('ps aux | grep -iE "make|cmake|focus"')
print("Running processes:")
print(stdout.read().decode())
client.close()
