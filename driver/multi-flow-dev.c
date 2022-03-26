/*  
 * @file multi-flow-dev.c
 * @brief multi-flow device driver with 128 minor numbers
 */

#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/workqueue.h>
#include <linux/version.h>
#include "lib/defines.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alessandro Chillotti");

/* module parameters */
bool enabled[MINOR_NUMBER] = {[0 ... (MINOR_NUMBER-1)] = true}; 
long byte_in_buffer[FLOWS * MINOR_NUMBER];
long thread_in_wait[FLOWS * MINOR_NUMBER];
module_param_array(enabled, bool, NULL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
module_param_array(byte_in_buffer, long, NULL, S_IRUSR | S_IRGRP);
module_param_array(thread_in_wait, long, NULL, S_IRUSR | S_IRGRP);

/* global variables */
static int Major;
object_t devices[MINOR_NUMBER];
long booked_byte[MINOR_NUMBER+1] = {[0 ... (MINOR_NUMBER)] = 0};

/* functions prototypes */
static int      dev_open(struct inode *, struct file *);
static int      dev_release(struct inode *, struct file *);
void            deferred_write(struct work_struct *);
static ssize_t  dev_write(struct file *, const char *, size_t, loff_t *);
static ssize_t  dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t  dev_ioctl(struct file *, unsigned int, unsigned long);
int             init_module(void);
void            cleanup_module(void);

/* driver operations setting */
static struct file_operations fops = {
        .owner = THIS_MODULE,
        .write = dev_write,
        .read = dev_read,
        .open =  dev_open,
        .release = dev_release,
        .unlocked_ioctl = dev_ioctl
};

/**
 * dev_open - manage session opening for a certain minor
 * @inode:      I/O metadata of the device file
 * @file:       I/O session to the device file
 * 
 * Returns 0 if the operation is successful, otherwise a negative value.
 */
static int dev_open(struct inode *inode, struct file *file)
{
        int minor;
        session_t *session;

        minor = get_minor(file);

        if (minor >= MINOR_NUMBER) {
                return -ENODEV;
        }

        // check if multi-flow device is enabled for this minor
        if (!enabled[minor])
                return -EINVAL;
        
        session = kmalloc(sizeof(session_t), GFP_KERNEL);
        if (unlikely(!session))
                return -ENOMEM;

        session->priority = HIGH_PRIORITY;
        session->flags = GFP_KERNEL;
        session->timeout = MAX_SECONDS;

        file->private_data = session;

        printk(KERN_INFO "%s-%d: device file successfully opened for object\n", MODNAME, minor);
        
        return 0;
}

/**
 * dev_release - close session opening for a certain minor
 * @inode:      I/O metadata of the device file
 * @file:       I/O session to the device file
 * 
 * Returns 0.
 */
static int dev_release(struct inode *inode, struct file *file)
{
        kfree(file->private_data);
        file->private_data = NULL;

        printk("%s: device file with minor %d closed\n", MODNAME, get_minor(file));    

        return 0;
}

/**
 * deferred_write - deferred write for low priority flow
 * @data:      work pointer to run write
 * 
 */
void deferred_write(struct work_struct *data)
{
        packed_work_t *work = container_of((void*)data,packed_work_t,the_work);
        object_t *object = devices + work->minor;
        dynamic_buffer_t *buffer = object->buffer[LOW_PRIORITY];

        mutex_lock(&(buffer->op_mutex));

        write_dynamic_buffer(object->buffer[LOW_PRIORITY], work->staging_area);

        printk(KERN_INFO "%s-%d: deferred write of '%s' completed", MODNAME, work->minor, work->staging_area->content);

        sub_booked_byte(work->minor,work->staging_area->size);
        add_byte_in_buffer(LOW_PRIORITY,work->minor,work->staging_area->size);

        mutex_unlock(&(buffer->op_mutex));

        kfree(container_of((void*)data,packed_work_t,the_work));

        wake_up_interruptible(&(object->buffer[LOW_PRIORITY]->waitqueue));

        module_put(THIS_MODULE);
}

/**
 * dev_write - write operation of driver
 * @filp:       I/O session to the device file
 * @buff:       buffer that contain data to write
 * @len:        size of content to write
 * 
 * Returns:
 *  written bytes number when the operation is successful
 *  a negative value when error occurs
 */
static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
        int ret;
        int byte_not_copied;
        int minor;
        char *temp_buffer;
        object_t *object;
        session_t *session;
        dynamic_buffer_t *buffer;
        data_segment_t *segment_to_write;
        packed_work_t *the_task;

        minor = get_minor(filp);
        object = devices + minor;
        session = (session_t *)filp->private_data;
        buffer = object->buffer[session->priority];

        printk(KERN_INFO "%s-%d: write called\n", MODNAME, minor);

        // copy data to write in a temporary buffer
        temp_buffer = kmalloc(len, session->flags);
        if (unlikely(!temp_buffer))
                return -ENOMEM;
        byte_not_copied = copy_from_user(temp_buffer, buff, len);

        // prepare memory areas
        segment_to_write = kmalloc(sizeof(data_segment_t), session->flags);
        if (unlikely(!segment_to_write)) {
                kfree(temp_buffer);
                return -ENOMEM;
        }

        if (session->priority == LOW_PRIORITY) {
                the_task =  kmalloc(sizeof(packed_work_t),session->flags);
                if (unlikely(!the_task)) {
                        kfree(temp_buffer);
                        free_data_segment(segment_to_write);
                        return -ENOMEM;
                }
        }

        // check if thread must block
        if(is_blocking(session->flags)) {
                atomic_inc_thread_in_wait(session->priority, minor);

                ret = wait_event_interruptible_exclusive_timeout(
                        buffer->waitqueue, 
                        lock_and_awake(
                                is_there_space(session->priority,minor),
                                &(buffer->op_mutex)
                                ),
                        session->timeout*CONFIG_HZ
                );

                atomic_dec_thread_in_wait(session->priority, minor);

                // check result of wait
                if (ret == 0) 
                        goto free_area;
                if (ret == -ERESTARTSYS) {
                        ret = -EINTR;
                        goto free_area;
                }
        } else {
                if (!mutex_trylock(&(buffer->op_mutex))) {
                        ret = -EBUSY;
                        goto free_area;
                }
                
                if (is_empty(session->priority,minor)) {
                        ret = 0;
                        goto unlock_wake;
                }
        }

        if (len-byte_not_copied > free_space(session->priority,minor)) 
                len = free_space(session->priority,minor);
        else 
                len = len - byte_not_copied;

        init_data_segment(segment_to_write, temp_buffer, len);

        // write data segment
        if (session->priority == HIGH_PRIORITY) {
                write_dynamic_buffer(buffer, segment_to_write);
                add_byte_in_buffer(HIGH_PRIORITY,minor,len);
                wake_up_interruptible(&(buffer->waitqueue));

                printk(KERN_INFO "%s-%d: %ld byte are written\n", MODNAME, minor, len);
        } else {
                if(!try_module_get(THIS_MODULE)) 
                        return -ENODEV;

                the_task->staging_area = segment_to_write;
                the_task->minor = minor;

                __INIT_WORK(&(the_task->the_work),(void*)deferred_write,(unsigned long)(&(the_task->the_work)));

                add_booked_byte(minor,len);

                queue_work(object->workqueue, &(the_task->the_work));

                printk(KERN_INFO "%s-%d: '%s' queued", MODNAME, minor, segment_to_write->content);
        }

        mutex_unlock(&(object->buffer[session->priority]->op_mutex));

        return len;

        // goto label for manage free and unlock
unlock_wake:    mutex_unlock(&(buffer->op_mutex));
                wake_up_interruptible(&(buffer->waitqueue));
free_area:      kfree(temp_buffer);
                kfree(segment_to_write);
                kfree(the_task);
                return ret;
}

/**
 * dev_read - read operation of driver
 * @filp:       I/O session to the device file
 * @buff:       buffer that contain read data
 * @len:        bytes number to be read
 * 
 * Returns:
 *  read bytes number when the operation is successful
 *  a negative value when error occurs
 */
static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off)
{
        int ret;
        int minor;
        char *temp_buffer;
        object_t *object;
        session_t *session;
        dynamic_buffer_t *buffer;

        minor = get_minor(filp);
        object = devices + minor;
        session = (session_t *)filp->private_data;
        buffer = object->buffer[session->priority];
        
        printk(KERN_INFO "%s-%d: read called\n",MODNAME,minor);

        if (len == 0)
                return 0;

        temp_buffer = kmalloc(len, session->flags);
        if (unlikely(!temp_buffer))
                return -ENOMEM;

        if(is_blocking(session->flags)) {
                atomic_inc_thread_in_wait(session->priority, minor);

                ret = wait_event_interruptible_exclusive_timeout(
                        buffer->waitqueue, 
                        lock_and_awake(
                                byte_to_read(session->priority,minor) > 0,
                                &(buffer->op_mutex)
                                ),
                        session->timeout*CONFIG_HZ
                );

                atomic_dec_thread_in_wait(session->priority, minor);

                // check result of wait
                if (ret == 0) {
                        kfree(temp_buffer);
                        return 0;
                }
                if (ret == -ERESTARTSYS) {
                        kfree(temp_buffer);
                        return -EINTR;
                }
        } else {
                if (!mutex_trylock(&(buffer->op_mutex))) {
                        kfree(temp_buffer);
                        return -EBUSY;
                }
                        
                if (is_empty(session->priority,minor)) {
                        mutex_unlock(&(buffer->op_mutex));
                        wake_up_interruptible(&(buffer->waitqueue));
                        kfree(temp_buffer);
                        return 0;
                }
        }
 
        if(len > byte_to_read(session->priority,minor))
                len = byte_to_read(session->priority,minor);

        read_dynamic_buffer(buffer, temp_buffer, len);

        sub_byte_in_buffer(session->priority,minor,len);

        wake_up_interruptible(&(buffer->waitqueue));

        mutex_unlock(&(buffer->op_mutex));

        ret = copy_to_user(buff,temp_buffer,len);

        printk(KERN_INFO "%s-%d: %ld byte are read\n",MODNAME,minor,len-ret);

        return len - ret;
}

/**
 * dev_ioctl - manager of I/O control requests 
 * @filp:       I/O session to the device file
 * @command:    requested ioctl command
 * @param:      optional parameter
 * 
 * Returns 0 if the operation is successful, otherwise a negative value.
 */
static ssize_t dev_ioctl(struct file *filp, unsigned int command, unsigned long param)
{
        session_t *session = (session_t *)filp->private_data;

        switch (command) {
        case TO_HIGH_PRIORITY:
                session->priority = HIGH_PRIORITY;
                break;
        case TO_LOW_PRIORITY:
                session->priority = LOW_PRIORITY;
                break;
        case BLOCK:
                session->flags = GFP_KERNEL;
                break;
        case UNBLOCK:
                session->flags = GFP_ATOMIC;
                break;
        case TIMEOUT:
                session->flags = GFP_ATOMIC;
                session->timeout = get_seconds(param);
                break;
        default:
                return -ENOTTY;
        }

        return 0;
}

/**
 * init_module - module initialization 
 * 
 * Returns 0 if the operation is successful, otherwise a negative value.
 */
int init_module(void)
{
        int i;

        Major = __register_chrdev(0, 0, MINOR_NUMBER, DEVICE_NAME, &fops);

        if (Major < 0) {
                printk(KERN_INFO "%s: registering device failed\n",MODNAME);
                return Major;
        }

        // setup of structures
        for (i = 0; i < MINOR_NUMBER; i++) {
                devices[i].workqueue = create_singlethread_workqueue("work-queue-" + i);

                devices[i].buffer[LOW_PRIORITY] = kmalloc(sizeof(dynamic_buffer_t), GFP_KERNEL);
                devices[i].buffer[HIGH_PRIORITY] = kmalloc(sizeof(dynamic_buffer_t), GFP_KERNEL);

                if (unlikely(!devices[i].buffer[LOW_PRIORITY] || !devices[i].buffer[HIGH_PRIORITY]))
                        break;

                init_dynamic_buffer(devices[i].buffer[LOW_PRIORITY]);
                init_dynamic_buffer(devices[i].buffer[HIGH_PRIORITY]);
        }

        if (i < MINOR_NUMBER) {
                for (; i > -1; i--) {
                        destroy_workqueue(devices[i].workqueue);

                        free_dynamic_buffer(devices[i].buffer[LOW_PRIORITY]);
                        free_dynamic_buffer(devices[i].buffer[HIGH_PRIORITY]);
                }
                return -ENOMEM;
        }

        printk(KERN_INFO "%s: new device registered, it is assigned major number %d\n",MODNAME, Major);

        return 0;
}

/**
 * cleanup_module - module cleanup 
 * 
 */
void cleanup_module(void)
{
        int i;

        // deallocation of structures
        for (i = 0; i < MINOR_NUMBER; i++) {
                destroy_workqueue(devices[i].workqueue);

                free_dynamic_buffer(devices[i].buffer[LOW_PRIORITY]);
                free_dynamic_buffer(devices[i].buffer[HIGH_PRIORITY]);
        }

        unregister_chrdev(Major, DEVICE_NAME);

        printk(KERN_INFO "%s: new device unregistered, it was assigned major number %d\n",MODNAME, Major);

        return;
}
