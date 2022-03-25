/*  
 *  multi-flow device driver with 128 minor numbers
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

static int Major;
object_t devices[MINOR_NUMBER];

bool enabled[MINOR_NUMBER] = {[0 ... (MINOR_NUMBER-1)] = true}; 
long byte_in_buffer[FLOWS * MINOR_NUMBER]; // first 128 -> low_priority, second 128 -> high_priority
long thread_in_wait[FLOWS * MINOR_NUMBER];
long booked_byte[MINOR_NUMBER+1] = {[0 ... (MINOR_NUMBER)] = 0}; // booked_byte[MINOR_NUMBER] is always 0

module_param_array(enabled, bool, NULL, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
module_param_array(byte_in_buffer, long, NULL, S_IRUSR | S_IRGRP);
module_param_array(thread_in_wait, long, NULL, S_IRUSR | S_IRGRP);

/* the actual driver */
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
        if (session == NULL)
                return -ENOMEM;

        session->priority = HIGH_PRIORITY;
        session->flags = GFP_KERNEL;
        session->timeout = MAX_SECONDS;

        file->private_data = session;

        printk(KERN_INFO "%s-%d: device file successfully opened for object\n", MODNAME, minor);
        
        return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{
        kfree(file->private_data);
        file->private_data = NULL;

        printk("%s: device file with minor %d closed\n", MODNAME, get_minor(file));    

        return 0;
}

void deferred_write(unsigned long data)
{
        packed_work_t *work = container_of((void*)data,packed_work_t,the_work);
        object_t *object = devices + work->minor;
        dynamic_buffer_t *buffer = object->buffer[LOW_PRIORITY];

        mutex_lock(&(buffer->operation_synchronizer));

        write_dynamic_buffer(object->buffer[LOW_PRIORITY], work->staging_area);

        printk(KERN_INFO "%s-%d: deferred write of '%s' completed", MODNAME, work->minor, work->staging_area->content);

        sub_booked_byte(work->minor,work->staging_area->size);
        add_byte_in_buffer(LOW_PRIORITY,work->minor,work->staging_area->size);

        mutex_unlock(&(buffer->operation_synchronizer));

        kfree(container_of((void*)data,packed_work_t,the_work));

        wake_up_interruptible(&(object->buffer[LOW_PRIORITY]->waitqueue));

        module_put(THIS_MODULE);
}

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

        minor = get_minor(filp);
        object = devices + minor;
        session = (session_t *)filp->private_data;
        buffer = object->buffer[session->priority];

        printk(KERN_INFO "%s-%d: write called\n", MODNAME, minor);

        // copy data to write in a temporary buffer
        temp_buffer = kmalloc(len, session->flags);
        if (temp_buffer == NULL)
                return -ENOMEM;
        byte_not_copied = copy_from_user(temp_buffer, buff, len);

        segment_to_write = kmalloc(sizeof(data_segment_t), session->flags);
        if (segment_to_write == NULL) {
                kfree(temp_buffer);
                return -ENOMEM;
        }

        mutex_try_or_lock(&(buffer->operation_synchronizer),session->flags);

        // check if thread must block
        if(!is_there_space(session->priority,minor) && is_blocking(session->flags)) {
                mutex_unlock(&(buffer->operation_synchronizer));

                atomic_inc_thread_in_wait(session->priority, minor);

                ret = wait_event_interruptible_exclusive_timeout(
                        buffer->waitqueue, 
                        lock_and_awake(
                                is_there_space(session->priority,minor),
                                &(buffer->operation_synchronizer)
                                ),
                        session->timeout*CONFIG_HZ
                );

                atomic_dec_thread_in_wait(session->priority, minor);

                // check if timeout elapsed
                if (ret == 0)
                        return -EAGAIN;
                if (ret == -ERESTARTSYS)
                        return -EINTR;
        } else if (!is_there_space(session->priority,minor)) {
                mutex_unlock(&(buffer->operation_synchronizer));

                kfree(temp_buffer);
                kfree(segment_to_write);
                return 0;
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
                packed_work_t *the_task;

                if(!try_module_get(THIS_MODULE)) 
                        return -ENODEV;

                the_task =  kmalloc(sizeof(packed_work_t),session->flags);
                if (the_task == NULL) {
                        free_segment_buffer(segment_to_write);
                        mutex_unlock(&(buffer->operation_synchronizer));

                        printk(KERN_ERR "%s-%d: work queue buffer allocation failure\n",MODNAME,minor);
                        return -ENOMEM;
                }

                the_task->staging_area = segment_to_write;
                the_task->minor = minor;

                __INIT_WORK(&(the_task->the_work),(void*)deferred_write,(unsigned long)(&(the_task->the_work)));

                add_booked_byte(minor,len);

                queue_work(object->workqueue, &(the_task->the_work));

                printk(KERN_INFO "%s-%d: '%s' queued", MODNAME, minor, segment_to_write->content);
        }

        mutex_unlock(&(object->buffer[session->priority]->operation_synchronizer));

        return len;
}

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

        mutex_try_or_lock(&(buffer->operation_synchronizer),session->flags);

        if(byte_to_read(session->priority,minor) == 0 && is_blocking(session->flags)) {
                mutex_unlock(&(buffer->operation_synchronizer));

                atomic_inc_thread_in_wait(session->priority, minor);

                ret = wait_event_interruptible_exclusive_timeout(
                        buffer->waitqueue, 
                        lock_and_awake(
                                byte_to_read(session->priority,minor) > 0,
                                &(buffer->operation_synchronizer)
                                ),
                        session->timeout*CONFIG_HZ
                );

                atomic_dec_thread_in_wait(session->priority, minor);

                // check if timeout elapsed
                if (ret == 0) 
                        return -EAGAIN;
                if (ret == -ERESTARTSYS)
                        return -EINTR;
        } else if (byte_to_read(session->priority,minor) == 0) {
                mutex_unlock(&(buffer->operation_synchronizer));
                return 0;
        }
 
        if(len > byte_to_read(session->priority,minor))
                len = byte_to_read(session->priority,minor);

        temp_buffer = kmalloc(len, session->flags);
        if (temp_buffer == NULL)
                return -ENOMEM;

        read_dynamic_buffer(buffer, temp_buffer, len);

        sub_byte_in_buffer(session->priority,minor,len);

        wake_up_interruptible(&(buffer->waitqueue));

        mutex_unlock(&(buffer->operation_synchronizer));

        ret = copy_to_user(buff,temp_buffer,len);

        printk(KERN_INFO "%s-%d: %ld byte are read\n",MODNAME,minor,len-ret);

        return len - ret;
}

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
                return 0;
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

                if (devices[i].buffer[LOW_PRIORITY] == NULL || devices[i].buffer[HIGH_PRIORITY] == NULL) break;

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
