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
    char* stream;
    int next;
} buffer_t;

typedef struct object {
    struct mutex state;
    unsigned char high_priority;
    buffer_t flow[FLOWS];
} object_t;

object_t objects[MINOR_NUMBER];

/* the actual driver */

static int dev_open(struct inode *inode, struct file *file) {

    int minor;

    minor = get_minor(file);

    // this device file is single instance
    if (!mutex_trylock(&(objects[minor].state))) {
	    return -EBUSY;
    }

    printk("%s: device file successfully opened by thread %d\n",MODNAME,current->pid);
    
    //device opened by a default nop
    return 0;
}


static int dev_release(struct inode *inode, struct file *file) {

    int minor;

    minor = get_minor(file);
    mutex_unlock(&(objects[minor].state));

    printk("%s: device file closed by thread %d\n",MODNAME,current->pid);

    //device closed by default nop
    return 0;
}



static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off) {

    printk("%s: somebody called a write on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

    // return len;
    return 1;
}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off) {

    printk("%s: somebody called a write on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

    return 0;
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

    // allocation of structures
    for (i = 0; i < MINOR_NUMBER; i++) {
        mutex_init(&(objects[i].state));

        objects[i].high_priority = 0;
        objects[i].flow[0].stream = (char*)__get_free_page(GFP_KERNEL);
        objects[i].flow[0].next = 0;
        objects[i].flow[1].stream = (char*)__get_free_page(GFP_KERNEL);
        objects[i].flow[1].next = 0;

        if ((objects[i].flow[0].stream == NULL) || (objects[i].flow[1].stream == NULL)) break; 
    }

    if (i != MINOR_NUMBER) {
        for (; i > -1; i--) {
		    free_page((unsigned long) objects[i].flow[0].stream);
            free_page((unsigned long) objects[i].flow[1].stream);
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
        free_page((unsigned long) objects[i].flow[0].stream);
        free_page((unsigned long) objects[i].flow[1].stream);
    }

    unregister_chrdev(Major, DEVICE_NAME);

    printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n",MODNAME, Major);

	return;
}