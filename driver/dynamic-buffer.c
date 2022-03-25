#include <stddef.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/list.h>
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
        struct list_head *head;

        head = &(buffer->head);

        mutex_init(&(buffer->operation_synchronizer));

        init_waitqueue_head(&(buffer->waitqueue));

        INIT_LIST_HEAD(head);
        
        head = &(buffer->head_process);
        INIT_LIST_HEAD(head);

        return 0;
}

int init_data_segment(data_segment_t *element, char *content, int len)
{
        element->content = content;
        element->size = len;
        element->byte_read = 0;

        return 0;
}

void write_dynamic_buffer(dynamic_buffer_t *buffer, data_segment_t *segment_to_write)
{
        list_add_tail(&(segment_to_write->list),&(buffer->head));
}

void read_dynamic_buffer(dynamic_buffer_t *buffer, char *read_content, int len)
{
        struct list_head *cur;
        struct list_head *old;
        struct list_head *head;
        data_segment_t *cur_seg;
        int byte_read;

        head = &(buffer->head);
        cur = (head)->next;
        cur_seg = list_entry(cur, struct data_segment, list);
        byte_read = 0;

        while (len - byte_read >= cur_seg->size - cur_seg->byte_read) {
                memcpy(read_content + byte_read, cur_seg->content + cur_seg->byte_read, cur_seg->size - cur_seg->byte_read);
                byte_read += cur_seg->size - cur_seg->byte_read;

                old = cur;
                cur = cur->next;
                free_segment_buffer(cur_seg);
                list_del(old);
                
                if (cur == head)
                        break;
                cur_seg = list_entry(cur, struct data_segment, list);
        }
        
        // check if i must read in this condition: byte_to_read < cur->size
        if (cur != head) {
                cur_seg = list_entry(cur, struct data_segment, list);

                memcpy(read_content + byte_read, cur_seg->content + cur_seg->byte_read, len - byte_read);

                cur_seg->byte_read += len - byte_read;
                byte_read += len - byte_read;
        }
}

void free_segment_buffer(data_segment_t *segment)
{
        kfree(segment->content);
        kfree(segment);

        return;
}

void free_dynamic_buffer(dynamic_buffer_t *buffer)
{ 
        struct list_head *cur;
        struct list_head *head;
        data_segment_t *cur_seg;

        head = &(buffer->head);

        list_for_each(cur, head) {
                cur_seg = list_entry(cur, data_segment_t, list);

                free_segment_buffer(cur_seg);
                list_del(cur);
        }

        mutex_destroy(&(buffer->operation_synchronizer));

        kfree(buffer);

        return;
}