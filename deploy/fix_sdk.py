import paramiko

client = paramiko.SSHClient()
client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
client.connect('192.168.10.6', username='hu', password='REDACTED_BUILD_PASSWORD')

cmd = 'echo "REDACTED_BUILD_PASSWORD" | sudo -S sed -i "s/message(FATAL_ERROR/message(WARNING/g" /opt/st/stm32mp2/5.0.3-snapshot/sysroots/cortexa35-ostl-linux/usr/lib/cmake/opencv4/OpenCVModules.cmake'

stdin, stdout, stderr = client.exec_command(cmd)
print("ERR:", stderr.read().decode())
print("OUT:", stdout.read().decode())

client.close()
