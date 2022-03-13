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
#include <linux/version.h>
#include "lib/defines.h"
#include "lib/dynamic-buffer.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alessandro Chillotti");

static int Major;
object_t files[MINOR_NUMBER];

bool enabled[MINOR_NUMBER] = {[0 ... (MINOR_NUMBER-1)] = true}; 
long byte_in_buffer[FLOWS * MINOR_NUMBER]; // first 128 -> low_priority, second 128 -> high_priority
long thread_in_wait[FLOWS * MINOR_NUMBER];

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

        // check if multi-flow device is enabled for thi minor
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
        }

        return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{
        int minor;

        minor = get_minor(file);

        kfree(file->private_data);
        file->private_data = NULL;

        printk("%s: device file with minor %d closed\n", MODNAME, minor);    

        return 0;
}

void deferred_write(unsigned long data)
{
        packed_work_t *work = container_of((void*)data,packed_work_t,the_work);
        object_t *object = files + work->minor;

        write_dynamic_buffer(object->buffer[LOW_PRIORITY], work->staging_area, work->size);

        wake_up(&(object->buffer[LOW_PRIORITY]->waitqueue));

        kfree(container_of((void*)data,packed_work_t,the_work));

        printk(KERN_INFO "%s-%d: deferred write completed", MODNAME, work->minor);

        module_put(THIS_MODULE);
}

static ssize_t dev_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
        int ret;
        long *current_byte_in_buffer;
        long *current_thread_in_wait;
        object_t *object;
        session_t *session;
        dynamic_buffer_t *buffer;
        char *temp_buffer;

        object = files + get_minor(filp);
        session = (session_t *)filp->private_data;
        buffer = object->buffer[session->priority];

        printk(KERN_INFO "%s-%d: write called by minor\n", MODNAME, get_minor(filp));

        mutex_lock(&(buffer->operation_synchronizer));

        current_byte_in_buffer = byte_in_buffer + (session->priority * MINOR_NUMBER) + get_minor(filp);
        current_thread_in_wait = thread_in_wait + (session->priority * MINOR_NUMBER) + get_minor(filp);

        if(*current_byte_in_buffer == MAX_BYTE_IN_BUFFER && session->blocking) {
                mutex_unlock(&(buffer->operation_synchronizer));

                __sync_fetch_and_add(thread_in_wait, 1);

                ret = wait_event_interruptible_timeout(buffer->waitqueue, *current_byte_in_buffer < MAX_BYTE_IN_BUFFER, session->timeout*CONFIG_HZ);

                __sync_fetch_and_sub(thread_in_wait, 1);
                
                // check if timeout elapsed or condition evaluated
                if (ret == 0)
                        return 0;
                else if (ret == 1)
                        mutex_lock(&(buffer->operation_synchronizer));
        } else if (*current_byte_in_buffer == MAX_BYTE_IN_BUFFER) {
                mutex_unlock(&(buffer->operation_synchronizer));
                return 0;
        }

        if(len > MAX_BYTE_IN_BUFFER - *current_byte_in_buffer) 
                len = MAX_BYTE_IN_BUFFER - *current_byte_in_buffer;

        if (session->priority == HIGH_PRIORITY) {
                temp_buffer = kmalloc(len, GFP_KERNEL);
                if (temp_buffer == NULL) 
                        return -ENOMEM;

                ret = copy_from_user(temp_buffer, buff, len);

                write_dynamic_buffer(buffer, temp_buffer, len);

                *current_byte_in_buffer += len;

                printk(KERN_INFO "%s-%d: %ld byte are written\n", MODNAME, get_minor(filp), (len-ret));

                wake_up(&(buffer->waitqueue));
        } else {
                packed_work_t *the_task;

                if(!try_module_get(THIS_MODULE)) return -ENODEV;

                the_task =  kmalloc(sizeof(packed_work_t),GFP_KERNEL);
                if (the_task == NULL) {
                        printk(KERN_ERR "%s-%d: work queue buffer allocation failure\n",MODNAME,get_minor(filp));
                        return -ENOMEM;
                }

                the_task->staging_area = kmalloc(len, GFP_KERNEL);
                if (the_task->staging_area == NULL) {
                        printk(KERN_ERR "%s-%d: staging area allocation failure\n",MODNAME,get_minor(filp));
                        return -ENOMEM;
                }

                // fill struct
                ret = copy_from_user(the_task->staging_area, buff, len);
                the_task->minor = get_minor(filp);
                the_task->size = len;

                __INIT_WORK(&(the_task->the_work),(void*)deferred_write,(unsigned long)(&(the_task->the_work)));
                ret = 0;

                *current_byte_in_buffer += len;

                queue_work(object->workqueue, &(the_task->the_work));
        }

        mutex_unlock(&(object->buffer[session->priority]->operation_synchronizer));

        return len-ret;
}

static ssize_t dev_read(struct file *filp, char *buff, size_t len, loff_t *off)
{
        int ret;
        long *current_byte_in_buffer;
        long *current_thread_in_wait;
        object_t *object;
        session_t *session;
        dynamic_buffer_t *buffer;

        object = files + get_minor(filp);
        session = (session_t *)filp->private_data;
        buffer = object->buffer[session->priority];
        
        printk(KERN_INFO "%s-%d: read called\n",MODNAME,get_minor(filp));

        if (len == 0)
                return 0;

        mutex_lock(&(buffer->operation_synchronizer));

        current_byte_in_buffer = byte_in_buffer + (session->priority * MINOR_NUMBER) + get_minor(filp);
        current_thread_in_wait = thread_in_wait + (session->priority * MINOR_NUMBER) + get_minor(filp);

        if(*current_byte_in_buffer == 0 && session->blocking) {
                mutex_unlock(&(buffer->operation_synchronizer));

                __sync_fetch_and_add(thread_in_wait, 1);

                ret = wait_event_interruptible_timeout(buffer->waitqueue, *current_byte_in_buffer != 0, session->timeout*CONFIG_HZ);

                __sync_fetch_and_sub(thread_in_wait, 1);

                // check if timeout elapsed or condition evaluated
                if (ret == 0) 
                        return 0;
                else if (ret == 1) 
                        mutex_lock(&(buffer->operation_synchronizer));
        } else if (*current_byte_in_buffer == 0) {
                mutex_unlock(&(buffer->operation_synchronizer));
                return 0;
        }
 
        if(len > *current_byte_in_buffer)
                len = *current_byte_in_buffer;

        ret = read_dynamic_buffer(buffer, buff, len);

        *current_byte_in_buffer -= len - ret;

        wake_up(&(buffer->waitqueue));

        mutex_unlock(&(buffer->operation_synchronizer));

        printk(KERN_INFO "%s-%d: %ld byte are read\n", MODNAME, get_minor(filp), (len-ret));

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
                if (session->timeout == 0)
                        session->timeout = MIN_SECONDS;
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

void cleanup_module(void)
{
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
