/*  
 * @file dynamic-buffer.c
 * @brief support for writing/reading for multi-flow device driver with 128 minor numbers
 */

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

/**
 * init_dynamic_buffer - initialization of buffer
 * @buffer:     pointer to buffer to initialize
 */
void init_dynamic_buffer(dynamic_buffer_t *buffer)
{
        struct list_head *head;

        head = &(buffer->head);

        mutex_init(&(buffer->op_mutex));

        init_waitqueue_head(&(buffer->waitqueue));

        INIT_LIST_HEAD(head);
}

/**
 * init_data_segment - initialization of data segment
 * @element:    pointer to data segment to initialize
 * @content:    content of data segment
 * @len:        size of data segment content
 */
void init_data_segment(data_segment_t *element, char *content, int len)
{
        element->content = content;
        element->size = len;
        element->byte_read = 0;
}

/**
 * write_dynamic_buffer - put in list a new data segment
 * @buffer:     pointer to buffer in edit
 * @segment:    pointer to data segment to add
 */
void write_dynamic_buffer(dynamic_buffer_t *buffer, data_segment_t *segment)
{
        list_add_tail(&(segment->list),&(buffer->head));
}

/**
 * read_dynamic_buffer - read data in buffer
 * @buffer:             pointer to buffer to read
 * @read_content:       buffer that containt read data
 * @len:                bytes number to be read
 */
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
                free_data_segment(cur_seg);
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

/**
 * free_data_segment - free a data segment
 * @segment:    pointer to data segment to free
 */
void free_data_segment(data_segment_t *segment)
{
        kfree(segment->content);
        kfree(segment);

        return;
}

/**
 * free_dynamic_buffer - free a buffer
 * @buffer:     pointer to buffer to free
 */
void free_dynamic_buffer(dynamic_buffer_t *buffer)
{ 
        struct list_head *cur;
        struct list_head *old;
        struct list_head *head;
        data_segment_t *cur_seg;

        head = &(buffer->head);
        old = NULL;

        list_for_each(cur, head) {
                if (old)
                        list_del(old);
                cur_seg = list_entry(cur, data_segment_t, list);

                free_data_segment(cur_seg);
                old = cur;
        }

        mutex_destroy(&(buffer->op_mutex));

        kfree(buffer);

        return;
}