#pragma once

/* ioctl commands definition */
#define turn_to_high_priority(fd)       ioctl(fd, 3)
#define turn_to_low_priority(fd)        ioctl(fd, 4)
#define set_blocking_operations(fd)     ioctl(fd, 5)
#define set_unblocking_operations(fd)   ioctl(fd, 6)
#define set_timeout(fd, value)          ioctl(fd, 7, value)