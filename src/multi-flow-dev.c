/*  
 *  multi-flow device driver with 128 minor numbers
 */

#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/pid.h>		/* For pid types */
#include <linux/tty.h>		/* For the tty declarations */
#include <linux/version.h>	/* For LINUX_VERSION_CODE */

#include "../lib/parameters.h"
#include "../lib/my-ioctl.h"
#include "../lib/circular-buffer.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alessandro Chillotti");

#define MODNAME "CHAR DEV"
#define DEVICE_NAME "multi-flow device"

#define HIGH_PRIORITY 1
#define LOW_PRIORITY 0

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
static ssize_t dev_ioctl(struct file *, unsigned int, unsigned long);

static int Major;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)	MAJOR(session->f_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)	MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)	MINOR(session->f_dentry->d_inode->i_rdev)
#endif

/* struct to abstract IO object */
typedef struct object {
    struct mutex operation_synchronizer;

    char* stream[FLOWS];        // circular buffer
    int begin[FLOWS];           // first byte to read
    int valid_bytes[FLOWS];     // valid_bytes + begin = point to write
} object_t;

typedef struct session {
    int priority;
    bool blocking;
    unsigned long timeout;
} session_t;

object_t files[MINOR_NUMBER];

/* the actual driver */

static int dev_open(struct inode *inode, struct file *file) {

    int minor;
    session_t *session;

    minor = get_minor(file);

    if (minor >= MINOR_NUMBER){
	    return -ENODEV;
    }

    session = kmalloc(sizeof(session), GFP_KERNEL);
    if (session == NULL) return -ENOMEM;

    session->priority = HIGH_PRIORITY;
    session->blocking = false;
    session->timeout = 0;

    file->private_data = session;

    printk("%s: device file successfully opened for object with minor %d\n", MODNAME, minor);
    
    return 0;
}


static int dev_release(struct inode *inode, struct file *file) {

    int minor;

    minor = get_minor(file);

    kfree(file->private_data);
    file->private_data = NULL;

    printk("%s: device file with minor %d closed\n", MODNAME, minor);    
    
    return 0;
}

unsigned long my_copy_from_user (char **to_buffer, const char __user * from_buffer, unsigned long n, int seek_buffer) {
    
    unsigned long ret;

    if (seek_buffer + n >= OBJECT_MAX_SIZE) { // end of buffer reached
        ret = copy_from_user(to_buffer[seek_buffer], from_buffer, OBJECT_MAX_SIZE - seek_buffer);
        ret = copy_from_user(to_buffer[0], from_buffer + (OBJECT_MAX_SIZE - seek_buffer), (n - OBJECT_MAX_SIZE - seek_buffer));
    } else {
        ret = copy_from_user(to_buffer[seek_buffer], from_buffer, n);
    }

    return ret;
}

static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off) {

    int minor;
    int ret;
    int seek_buffer;
    object_t *object;
    session_t *session;

    minor = get_minor(filp);
    object = files + minor;
    session = (session_t *)filp->private_data;

    printk("%s: somebody called a write on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

    mutex_lock(&(object->operation_synchronizer));

    if (*off >= OBJECT_MAX_SIZE) { // offset too large
 	    mutex_unlock(&(object->operation_synchronizer));
	    return -ENOSPC; // no space left on device
    }

    if (*off > object->valid_bytes[session->priority]) { // offset beyond the current stream size
 	    mutex_unlock(&(object->operation_synchronizer));
	    return -ENOSR; // out of stream resources
    }

    if ((OBJECT_MAX_SIZE - *off) < len) {
        mutex_unlock(&(object->operation_synchronizer));
        if (session->blocking) {
            // TODO: blocking case
            printk("block");
            return 0;
        } else {
            // len = OBJECT_MAX_SIZE - *off;
            return -ENOSPC;
        }
    }

    if (session->priority == HIGH_PRIORITY) {
        printk("HIGH PRIORITY");

        seek_buffer = (object->begin[HIGH_PRIORITY] + object->valid_bytes[HIGH_PRIORITY])%OBJECT_MAX_SIZE;

        ret = my_copy_from_user(&(object->stream[HIGH_PRIORITY]), buff, len, seek_buffer);
        // ret = copy_from_user(&(object->stream[HIGH_PRIORITY][seek_buffer]), buff, len);

        object->valid_bytes[HIGH_PRIORITY] = object->valid_bytes[HIGH_PRIORITY] + (len - ret);
    } else {
        // TODO: write in LOW_PRIORITY case
    }

    *off += (len - ret);
    object->valid_bytes[session->priority] = *off;
    mutex_unlock(&(object->operation_synchronizer));

    printk("%ld byte are written (begin = %d, offset = %d)", (len-ret), object->begin[session->priority], object->valid_bytes[session->priority]);
    return len-ret;
}

unsigned long my_copy_to_user (char __user *to_buffer, const char *from_buffer, unsigned long n, int begin) {

    unsigned long ret;

    if (begin + n >= OBJECT_MAX_SIZE) { // end of buffer reached
        ret = copy_to_user(to_buffer, from_buffer + begin, OBJECT_MAX_SIZE - begin);
        ret = copy_to_user(to_buffer + (OBJECT_MAX_SIZE - begin), from_buffer, (n - OBJECT_MAX_SIZE - begin));
    } else {
        ret = copy_to_user(to_buffer, from_buffer + begin, n);
    }

    return ret;
}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {

    int minor;
    int ret;
    object_t *object;
    session_t *session;

    minor = get_minor(filp);
    object = files + minor;

    session = (session_t *)filp->private_data;

    printk("%s: somebody called a read on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

    mutex_lock(&(object->operation_synchronizer));

    if(*off > object->valid_bytes[session->priority]) { 
 	    mutex_unlock(&(object->operation_synchronizer));
	    return 0;
    }

    if((object->valid_bytes[session->priority] - *off) < len) {
        if (session->blocking && session->timeout != 0) {
            // TODO: blocking case
            printk("block");
            mutex_unlock(&(object->operation_synchronizer));
            return 0;
        } else {
            len = object->valid_bytes[session->priority] - *off;
        }
    }

    ret = my_copy_to_user(buff, object->stream[session->priority], len, object->begin[session->priority]);
    // ret = copy_to_user(buff, &(object->stream[session->priority][object->begin[session->priority]]), len);

    printk("ho letto %s", buff);

    object->begin[session->priority] = (object->begin[session->priority] + (len - ret))%OBJECT_MAX_SIZE;
    object->valid_bytes[session->priority] = object->valid_bytes[session->priority] - (len - ret);

    mutex_unlock(&(object->operation_synchronizer));

    printk("%ld byte are read (begin = %d, offset = %d)", (len-ret), object->begin[session->priority], object->valid_bytes[session->priority]);

    return len - ret;
}

static ssize_t dev_ioctl(struct file *filp, unsigned int command, unsigned long param) {

    session_t *session = (session_t *)filp->private_data;

    switch (command) {
        case TO_HIGH_PRIORITY:  session->priority = HIGH_PRIORITY;  
                            break;
        case TO_LOW_PRIORITY:   session->priority = LOW_PRIORITY;  
                            break;
        case BLOCK:             session->blocking = true;
                            break;
        case UNBLOCK:           session->blocking = false;
                            break;
        case TIMEOUT:           session->timeout = param;
                            break;
        default:                return 0;
    }

    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .write = dev_write,
    .read = dev_read,
    .open =  dev_open,
    .release = dev_release,
    .unlocked_ioctl = dev_ioctl
};

int init_module(void) {

    int i;
    
    Major = __register_chrdev(0, 0, MINOR_NUMBER, DEVICE_NAME, &fops);

    if (Major < 0) {
        printk("%s: registering device failed\n",MODNAME);
        return Major;
    }

    // setup of structures
    for (i = 0; i < MINOR_NUMBER; i++) {
        mutex_init(&(files[i].operation_synchronizer));

        files[i].stream[LOW_PRIORITY] = NULL;
        files[i].stream[LOW_PRIORITY] = (char*)__get_free_page(GFP_KERNEL);
        files[i].begin[LOW_PRIORITY] = 0;
        files[i].valid_bytes[LOW_PRIORITY] = 0;

        files[i].stream[HIGH_PRIORITY] = NULL;
        files[i].stream[HIGH_PRIORITY] = (char*)__get_free_page(GFP_KERNEL);
        files[i].begin[HIGH_PRIORITY] = 0;
        files[i].valid_bytes[HIGH_PRIORITY] = 0;

        if ((files[i].stream[LOW_PRIORITY] == NULL) || (files[i].stream[HIGH_PRIORITY] == NULL)) break; 
    }

    if (i < MINOR_NUMBER) {
        for (; i > -1; i--) {
		    free_page((unsigned long) files[i].stream[LOW_PRIORITY]);
            free_page((unsigned long) files[i].stream[HIGH_PRIORITY]);
	    }

        return -ENOMEM;
    }
        
    printk(KERN_INFO "%s: new device registered, it is assigned major number %d\n",MODNAME, Major);

	return 0;
}

void cleanup_module(void) {

    int i;
    
    // deallocation of structures
    for (i = 0; i < MINOR_NUMBER; i++) {
        free_page((unsigned long) files[i].stream[LOW_PRIORITY]);
        free_page((unsigned long) files[i].stream[HIGH_PRIORITY]);
    }

    unregister_chrdev(Major, DEVICE_NAME);

    printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n",MODNAME, Major);

	return;
}