/*  
 *  multi-flow device driver with 128 minor numbers
 */

#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>	
#include <linux/pid.h>		/* For pid types */
#include <linux/tty.h>		/* For the tty declarations */
#include <linux/version.h>	/* For LINUX_VERSION_CODE */


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alessandro Chillotti");

#define MODNAME "CHAR DEV"
#define DEVICE_NAME "multi-flow device"

#define MINOR_NUMBER 128
#define FLOWS 2

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
typedef struct buffer {
    char* stream;               // circular buffer
    int begin;                  // first byte to read
    int valid_bytes;                 // valid_bytes + begin = point to write
} buffer_t;

typedef struct object {
    struct mutex operation_synchronizer;;
    unsigned char priority;
    
    char* stream[FLOWS];        // circular buffer
    int begin[FLOWS];           // first byte to read
    int valid_bytes[FLOWS];     // valid_bytes + begin = point to write

    bool blocking;
    unsigned long timeout;
} object_t;

#define OBJECT_MAX_SIZE (4096)

object_t files[MINOR_NUMBER];

/* the actual driver */

static int dev_open(struct inode *inode, struct file *file) {

    int minor;

    minor = get_minor(file);

    if (minor >= MINOR_NUMBER){
	    return -ENODEV;
    }

    printk("%s: device file successfully opened for object with minor %d\n", MODNAME, minor);
    
    return 0;
}


static int dev_release(struct inode *inode, struct file *file) {

    int minor;

    minor = get_minor(file);

    printk("%s: device file with minor %d closed\n", MODNAME, minor);
    
    return 0;
}



static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off) {

    int minor;
    int ret;
    int seek_buffer;
    object_t *object;

    minor = get_minor(filp);
    object = files + minor;

    printk("%s: somebody called a write on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

    mutex_lock(&(object->operation_synchronizer));

    if(*off >= OBJECT_MAX_SIZE) { // offset too large
 	    mutex_unlock(&(object->operation_synchronizer));
	    return -ENOSPC; // no space left on device
    }

    if(*off > object->valid_bytes[object->priority]) { // offset beyond the current stream size
 	    mutex_unlock(&(object->operation_synchronizer));
	    return -ENOSR; // out of stream resources
    }

    if((OBJECT_MAX_SIZE - *off) < len) {
        mutex_unlock(&(object->operation_synchronizer));
        if (object->blocking) {
            // TODO: blocking case
            printk("block");
        } else {
            // len = OBJECT_MAX_SIZE - *off;
            return -ENOSPC;
        }
    }

    if (object->priority == HIGH_PRIORITY) {
        printk("HIGH PRIORITY");

        seek_buffer = (object->begin[HIGH_PRIORITY] + object->valid_bytes[HIGH_PRIORITY])%OBJECT_MAX_SIZE;
        ret = copy_from_user(&(object->stream[HIGH_PRIORITY][seek_buffer]), buff, len);

        object->valid_bytes[HIGH_PRIORITY] = object->valid_bytes[HIGH_PRIORITY] + (len - ret);
    } else {
        // TODO: write in LOW_PRIORITY case
    }

    *off += (len - ret);
    object->valid_bytes[object->priority] = *off;
    mutex_unlock(&(object->operation_synchronizer));

    printk("%ld byte are written", (len-ret));

    return len-ret;
}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {

    int minor;
    int ret;
    object_t *object;

    minor = get_minor(filp);
    object = files + minor;

    printk("%s: somebody called a read on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

    mutex_lock(&(object->operation_synchronizer));

    if(*off > object->valid_bytes[object->priority]) { 
 	    mutex_unlock(&(object->operation_synchronizer));
	    return 0;
    }

    if((object->valid_bytes[object->priority] - *off) < len) {
        if (object->blocking && object->timeout != 0) {
            // TODO: blocking case
            printk("block");
        } else {
            len = object->valid_bytes[object->priority] - *off;
        }
    } 

    ret = copy_to_user(buff, &(object->stream[object->priority][object->begin[object->priority]]), len);

    object->begin[object->priority] = (object->begin[object->priority] + (len - ret))%OBJECT_MAX_SIZE;
    object->valid_bytes[object->priority] = object->valid_bytes[object->priority] - (len - ret);

    mutex_unlock(&(object->operation_synchronizer));

    printk("%ld byte are read", (len-ret));

    return len - ret;
}

static ssize_t dev_ioctl(struct file *filp, unsigned int command, unsigned long param) {
    return 0;
}

static struct file_operations fops = {
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

        files[i].priority = HIGH_PRIORITY;

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