#pragma once

/* Indexes fot ioctl */
#define TO_HIGH_PRIORITY    1
#define TO_LOW_PRIORITY     2
#define BLOCK               3
#define UNBLOCK             4
#define TIMEOUT             5

/* User commands */
#define IOCTL_HIGH_PRIORITY(fd)     (ioctl(fd, TO_HIGH_PRIORITY))
#define IOCTL_LOW_PRIORITY(fd)      (ioctl(fd, TO_LOW_PRIORITY))
#define IOCTL_BLOCK(fd)             (ioctl(fd, BLOCK))
#define IOCTL_UNBLOCK(fd)           (ioctl(fd, UNBLOCK))
#define IOCTL_TIMEOUT(fd, value)    (ioctl(fd, TIMEOUT, value))
