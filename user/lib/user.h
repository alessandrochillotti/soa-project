#pragma once

/* ioctl commands */
#define IOCTL_HIGH_PRIORITY(fd)     (ioctl(fd, 1))
#define IOCTL_LOW_PRIORITY(fd)      (ioctl(fd, 2))
#define IOCTL_BLOCK(fd)             (ioctl(fd, 3))
#define IOCTL_UNBLOCK(fd)           (ioctl(fd, 4))
#define IOCTL_TIMEOUT(fd, value)    (ioctl(fd, 5, value))