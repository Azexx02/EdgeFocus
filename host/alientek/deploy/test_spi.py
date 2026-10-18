import os
print('opened')
fd = os.open('/dev/spidev0.0', os.O_RDWR)
print('written')
os.write(fd, b'\x00')
print('done')
