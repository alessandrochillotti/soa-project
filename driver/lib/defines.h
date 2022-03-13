#pragma once

#include "dynamic-buffer.h"

#define MAX_BYTE_IN_BUFFER 32*4096      // 32 pagine
#define MINOR_NUMBER 128
#define FLOWS 2

#define LOW_PRIORITY 0
#define HIGH_PRIORITY 1

/* module defines */
#define MODNAME "CHAR DEV"
#define DEVICE_NAME "multi-flow device"

/* indexes fot ioctl */
#define TO_HIGH_PRIORITY        3
#define TO_LOW_PRIORITY         4
#define BLOCK                   5
#define UNBLOCK                 6
#define TIMEOUT                 7

#define MIN_SECONDS             1
#define MAX_SECONDS             16777215        // 2^(32) = 4294967296 -> 16777215 * 250 < 4294967296 (no overflow)

/* macro */
#define get_seconds(sec)                (sec&MAX_SECONDS)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)      MAJOR(session->f_inode->i_rdev)
#define get_minor(session)      MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)      MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)      MINOR(session->f_dentry->d_inode->i_rdev)
#endif

#define print(minor,msg)        \
        printk("%s - %d: %s\n", \
        MODNAME, minor, msg);

/* struct to abstract IO object */
typedef struct object {
        struct workqueue_struct *workqueue;     // the workqueue is in object_t because is only for low priority
        dynamic_buffer_t *buffer[FLOWS];
} object_t;

/* session struct */
typedef struct session {
        int priority;
        bool blocking;
        unsigned long timeout;
} session_t;

/* delayed work */
typedef struct packed_work{
        char* staging_area;		        // data to write
        int size;
        int minor;
        struct work_struct the_work;
} packed_work_t;

/* function prototypes */
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
static ssize_t dev_ioctl(struct file *, unsigned int, unsigned long);
void deferred_write(unsigned long);
