import pty
import os
import time

pid, fd = pty.fork()
if pid == 0:
    os.execvp("bash", ["bash", "--norc"])
else:
    time.sleep(0.5)
    os.write(fd, b"123456789012345")
    time.sleep(0.5)
    import termios
    import struct
    import fcntl
    winsize = struct.pack("HHHH", 20, 10, 0, 0)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, winsize)
    time.sleep(0.5)
    winsize = struct.pack("HHHH", 20, 20, 0, 0)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, winsize)
    time.sleep(0.5)
    res = os.read(fd, 1024)
    print(repr(res))
