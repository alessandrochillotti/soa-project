/*  
 *  multi-flow device driver with 128 minor numbers
 */

#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
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

        if (minor >= MINOR_NUMBER){
                return -ENODEV;
        }

        // check if multi-flow device is enabled for this minor
        if (enabled[minor]) {
                session = kmalloc(sizeof(session_t), GFP_KERNEL);
                if (session == NULL)
                        return -ENOMEM;

                session->priority = HIGH_PRIORITY;
                session->blocking = true;
                session->timeout = MAX_SECONDS;

                file->private_data = session;

                printk(KERN_INFO "%s-%d: device file successfully opened for object\n", MODNAME, minor);
        } else {
                printk(KERN_INFO "%s-%d: device file can't be opened\n", MODNAME, minor);
                return -1;
        }

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

        printk(KERN_INFO "%s-%d: deferred write completed", MODNAME, work->minor);

        sub_booked_byte(work->minor,work->staging_area->size);
        add_byte_in_buffer(LOW_PRIORITY,work->minor,work->staging_area->size);

        mutex_unlock(&(buffer->operation_synchronizer));

        free_segment_buffer(work->staging_area);
        kfree(container_of((void*)data,packed_work_t,the_work));

        wake_up(&(object->buffer[LOW_PRIORITY]->waitqueue));

        module_put(THIS_MODULE);
}

static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
        int ret;
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

        mutex_lock(&(buffer->operation_synchronizer));

        // check if thread must block
        if(free_space(session->priority,minor) == 0 && session->blocking) {
                mutex_unlock(&(buffer->operation_synchronizer));

                atomic_inc_thread_in_wait(session->priority, minor);

                ret = p_wait_event_interruptible_timeout(buffer->waitqueue, free_space(session->priority,minor) > 0, session->timeout*CONFIG_HZ, &(buffer->operation_synchronizer));

                atomic_dec_thread_in_wait(session->priority, minor);

                // check if timeout elapsed
                if (ret == 0)
                        return 0;
        } else if (free_space(session->priority,minor) == 0) {
                mutex_unlock(&(buffer->operation_synchronizer));
                return 0;
        }

        if (len > free_space(session->priority,minor)) 
                len = free_space(session->priority,minor);

        // prepare segment to write
        temp_buffer = kmalloc(len, GFP_KERNEL);
        if (temp_buffer == NULL) {
                mutex_unlock(&(buffer->operation_synchronizer));
                return -ENOMEM;
        } 
        
        ret = copy_from_user(temp_buffer, buff, len);

        segment_to_write = kmalloc(sizeof(data_segment_t), GFP_KERNEL);
        if (segment_to_write == NULL) {
                kfree(temp_buffer);
                mutex_unlock(&(buffer->operation_synchronizer));
                return -ENOMEM;
        }
        init_data_segment(segment_to_write, temp_buffer, len-ret);

        // write data segment
        if (session->priority == HIGH_PRIORITY) {
                write_dynamic_buffer(buffer, segment_to_write);
                add_byte_in_buffer(HIGH_PRIORITY,minor,len-ret);
                wake_up(&(buffer->waitqueue));

                printk(KERN_INFO "%s-%d: %ld byte are written\n", MODNAME, minor, len-ret);
        } else {
                packed_work_t *the_task;

                if(!try_module_get(THIS_MODULE)) 
                        return -ENODEV;

                the_task =  kmalloc(sizeof(packed_work_t),GFP_KERNEL);
                if (the_task == NULL) {
                        free_segment_buffer(segment_to_write);
                        mutex_unlock(&(buffer->operation_synchronizer));

                        printk(KERN_ERR "%s-%d: work queue buffer allocation failure\n",MODNAME,minor);
                        return -ENOMEM;
                }

                the_task->staging_area = segment_to_write;
                the_task->minor = minor;

                __INIT_WORK(&(the_task->the_work),(void*)deferred_write,(unsigned long)(&(the_task->the_work)));

                add_booked_byte(minor,len-ret);

                queue_work(object->workqueue, &(the_task->the_work));
        }

        mutex_unlock(&(object->buffer[session->priority]->operation_synchronizer));

        return len-ret;
}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off)
{
        int ret;
        int minor;
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

        mutex_lock(&(buffer->operation_synchronizer));

        if(byte_to_read(session->priority,minor) == 0 && session->blocking) {
                mutex_unlock(&(buffer->operation_synchronizer));

                atomic_inc_thread_in_wait(session->priority, minor);

                ret = p_wait_event_interruptible_timeout(buffer->waitqueue, byte_to_read(session->priority,minor) > 0, session->timeout*CONFIG_HZ, &(buffer->operation_synchronizer));

                atomic_dec_thread_in_wait(session->priority, minor);

                // check if timeout elapsed
                if (ret == 0)
                        return 0;
        } else if (byte_to_read(session->priority,minor) == 0) {
                mutex_unlock(&(buffer->operation_synchronizer));
                return 0;
        }
 
        if(len > byte_to_read(session->priority,minor))
                len = byte_to_read(session->priority,minor);

        ret = read_dynamic_buffer(buffer, buff, len);

        sub_byte_in_buffer(session->priority,minor,len-ret);

        wake_up(&(buffer->waitqueue));

        mutex_unlock(&(buffer->operation_synchronizer));

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
                session->blocking = true;
                break;
        case UNBLOCK:
                session->blocking = false;
                break;
        case TIMEOUT:
                session->blocking = true;
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
