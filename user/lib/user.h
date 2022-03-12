#pragma once

/* ioctl commands */
#define turn_to_high_priority(fd)       (ioctl(fd, 1))
#define turn_to_low_priority(fd)        (ioctl(fd, 2))
#define set_blocking_operations(fd)     (ioctl(fd, 3))
#define set_unblocking_operations(fd)   (ioctl(fd, 4))
#define set_timeout(fd, value)          (ioctl(fd, 5, value))