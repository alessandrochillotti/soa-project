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
#include <linux/workqueue.h>
#include <linux/pid.h>		/* For pid types */
#include <linux/tty.h>		/* For the tty declarations */
#include <linux/version.h>	/* For LINUX_VERSION_CODE */

#include "../lib/parameters.h"
#include "../lib/my-ioctl.h"
#include "../lib/circular-buffer.h"
#include "../lib/dynamic-buffer.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alessandro Chillotti");

#define MODNAME "CHAR DEV"
#define DEVICE_NAME "multi-flow device"

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_write(struct file *, const char *, size_t, loff_t *);
static ssize_t dev_ioctl(struct file *, unsigned int, unsigned long);
void deferred_write(unsigned long);

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
    struct workqueue_struct *workqueue;
    dynamic_buffer_t *buffer[FLOWS];
} object_t;

/* session struct */
typedef struct session {
    unsigned char priority;
    bool blocking;
    unsigned long timeout;
} session_t;

/* delayed work */
typedef struct packed_work{
    char* staging_area;             // data to write
    int size;
    int minor;
    struct work_struct the_work;
} packed_work_t;

object_t files[MINOR_NUMBER];

/* the actual driver */

static int dev_open(struct inode *inode, struct file *file) {

    int minor;
    session_t *session;

    minor = get_minor(file);

    if (minor >= MINOR_NUMBER){
	    return -ENODEV;
    }

    session = kmalloc(sizeof(session_t), GFP_KERNEL);
    if (session == NULL) return -ENOMEM;

    session->priority = LOW_PRIORITY;
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

void deferred_write(unsigned long data) {

    packed_work_t *work = container_of((void*)data,packed_work_t,the_work);
    object_t *object = files + work->minor;

    write_dynamic_buffer(object->buffer[LOW_PRIORITY], work->staging_area, work->size);

    kfree(container_of((void*)data,packed_work_t,the_work));
}

static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off) {

    int minor;
    int ret;
    object_t *object;
    session_t *session;
    char *temp_buffer;

    minor = get_minor(filp);
    object = files + minor;
    session = (session_t *)filp->private_data;

    printk("%s: somebody called a write on dev with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

    mutex_lock(&(object->buffer[session->priority]->operation_synchronizer));

    if (session->priority == HIGH_PRIORITY) {

        temp_buffer = kmalloc(len, GFP_KERNEL);
        if (temp_buffer == NULL) return -ENOMEM;

        ret = copy_from_user(temp_buffer, buff, len);

        write_dynamic_buffer(object->buffer[HIGH_PRIORITY], temp_buffer, len);
    } else {
        packed_work_t *the_task;

        the_task =  kmalloc(sizeof(packed_work_t),GFP_KERNEL);
        if (the_task == NULL) {
            printk("%s: work queue buffer allocation failure\n",MODNAME);
            return -1;
        }

        the_task->staging_area = kmalloc(len, GFP_KERNEL);
        if (the_task->staging_area == NULL) {
            printk("%s: staging area allocation failure\n",MODNAME);
            return -1;
        }

        // Fill struct
        ret = copy_from_user(the_task->staging_area, buff, len);
        the_task->minor = minor;
        the_task->size = len;

        __INIT_WORK(&(the_task->the_work),(void*)deferred_write,(unsigned long)(&(the_task->the_work)));
        ret = 0;

        queue_work(object->workqueue, &(the_task->the_work));
    }

    mutex_unlock(&(object->buffer[session->priority]->operation_synchronizer));

    printk("%ld byte are written", (len-ret));

    return len-ret;
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

    mutex_lock(&(object->buffer[session->priority]->operation_synchronizer));

    // printk("byte_in_buffer = %d", object->buffer[session->priority]->byte_in_buffer);

    if(object->buffer[session->priority]->byte_in_buffer == 0) {
        if (session->blocking && session->timeout != 0) {
            // TODO: blocking case with condition byte_in_buffer != 0
            printk("blocking case");
            mutex_unlock(&(object->buffer[session->priority]->operation_synchronizer));
        } else {
            mutex_unlock(&(object->buffer[session->priority]->operation_synchronizer));
        }
        return 0;
    }

    if(len > object->buffer[session->priority]->byte_in_buffer) len = object->buffer[session->priority]->byte_in_buffer;

    ret = read_dynamic_buffer(object->buffer[session->priority], buff, len);

    mutex_unlock(&(object->buffer[session->priority]->operation_synchronizer));

    printk("%ld byte are read (parola = %s)", (len-ret), buff);

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
        case TIMEOUT:           session->blocking = true;
                                session->timeout = param;
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
        files[i].workqueue = create_singlethread_workqueue("work-queue-" + i);
        files[i].buffer[LOW_PRIORITY] = kmalloc(sizeof(dynamic_buffer_t), GFP_KERNEL);
        files[i].buffer[HIGH_PRIORITY] = kmalloc(sizeof(dynamic_buffer_t), GFP_KERNEL);

        if (files[i].buffer[LOW_PRIORITY] == NULL || files[i].buffer[HIGH_PRIORITY] == NULL) break;

        if (init_dynamic_buffer(files[i].buffer[LOW_PRIORITY]) == ENOMEM || init_dynamic_buffer(files[i].buffer[HIGH_PRIORITY]) == ENOMEM) break; 
    }

    if (i < MINOR_NUMBER) {
        for (; i > -1; i--) {
		    free_dynamic_buffer(files[i].buffer[LOW_PRIORITY]);
            free_dynamic_buffer(files[i].buffer[HIGH_PRIORITY]);
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
        destroy_workqueue(files[i].workqueue);

        free_dynamic_buffer(files[i].buffer[LOW_PRIORITY]);
        free_dynamic_buffer(files[i].buffer[HIGH_PRIORITY]);
    }

    unregister_chrdev(Major, DEVICE_NAME);

    printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n",MODNAME, Major);

	return;
}