#include <stddef.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/version.h>

#include "lib/defines.h"

/* functions */
int init_dynamic_buffer(dynamic_buffer_t *buffer)
{
        buffer->head = NULL;
        buffer->tail = NULL;

        mutex_init(&(buffer->operation_synchronizer));

        init_waitqueue_head(&(buffer->waitqueue));

        return 0;
}

int init_buffer_element(buffer_element_t *element, char *content, int len)
{
        element->content = content;
        element->size = len;
        element->byte_read = 0;
        element->next = NULL;

        return 0;
}

unsigned long write_dynamic_buffer(dynamic_buffer_t *buffer, char *content, int len)
{
        buffer_element_t *element_to_write = kmalloc(sizeof(buffer_element_t), GFP_KERNEL);
        if (element_to_write == NULL) return -ENOMEM;

        init_buffer_element(element_to_write, content, len);

        if (buffer->head == NULL) { // zero elements
                buffer->head = element_to_write;
        } else if (buffer->tail == NULL) { // one element
                buffer->tail = element_to_write;
                buffer->head->next = buffer->tail;
        } else { // general case
                buffer->tail->next = element_to_write;
                buffer->tail = element_to_write;
        }

        return len;
}

unsigned long read_dynamic_buffer(dynamic_buffer_t *buffer, char __user *read_content, int len)
{
        buffer_element_t *cur;
        buffer_element_t *old;

        int byte_read;
        int ret;

        byte_read = 0;
        cur = buffer->head;

        // read whole blocks
        while (cur != NULL && (len - byte_read > cur->size - cur->byte_read)) {
                ret = copy_to_user(read_content + byte_read, cur->content + cur->byte_read, cur->size - cur->byte_read);

                byte_read += cur->size - cur->byte_read - ret;

                if (ret == 0) {
                        old = cur;
                        cur = cur->next;

                        buffer->head = cur;
                        if (buffer->head == buffer->tail) buffer->tail = NULL;
                        free_element_buffer(old);
                } else {
                        cur->byte_read += cur->size - cur->byte_read - ret;

                        return len - byte_read;
                }
        }

        // check if i must read in this condition: byte_to_read < cur->size
        if (cur != NULL) {
                ret = copy_to_user(read_content + byte_read, cur->content + cur->byte_read, len - byte_read);

                cur->byte_read += len - byte_read - ret;
                byte_read += len - byte_read - ret;
        }

        return len - byte_read;
}

void free_element_buffer(buffer_element_t *element)
{
        kfree(element->content);
        kfree(element);

        return;
}

void free_dynamic_buffer(dynamic_buffer_t *buffer)
{
        buffer_element_t* cur;
        buffer_element_t* old;

        cur = buffer->head;

        while (cur != NULL) {
                old = cur;
                cur = cur->next;

                free_element_buffer(old);       
        }

        kfree(buffer);

        return;
}