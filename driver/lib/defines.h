#ifndef DEFINES_H
#define DEFINES_H

/* MODULE INFORMATIONS DEFINITION */

#define MODNAME "CHAR DEV"
#define DEVICE_NAME "multi-flow device"

/* CONSTANTS DEFINITION */

/* general information */
#define MAX_BYTE_IN_BUFFER 32*4096                      // maximum number of byte in buffer
#define MINOR_NUMBER 128                                // maximum number of minor manageable
#define FLOWS 2                                         // number of different priority

#define LOW_PRIORITY 0                                  // index assigned to low priority
#define HIGH_PRIORITY 1                                 // index assigned to high priority

/* ioctl indexes */
#define TO_HIGH_PRIORITY        3                       
#define TO_LOW_PRIORITY         4
#define BLOCK                   5
#define UNBLOCK                 6
#define TIMEOUT                 7

/* upper and lower bound for seconds */
#define MIN_SECONDS             1                       // minimum amount of seconds
#define MAX_SECONDS             17179869                // maximum amount of seconds

/* STRUCTURES DEFINITION */

/*
 * data_segment_t - data segment
 * @list:       list_head element to link to list
 * @content:    byte content of data segment
 * @byte_read:  number of byte read up to instant t
 * @size:       size of data segment content
 */
typedef struct data_segment {
        struct list_head list;
        char *content;
        int byte_read;
        int size;
} data_segment_t;

/*
 * dynamic_buffer_t - buffer
 * @head:       head of linked list
 * @op_mutex:   mutex to synchronize operation in buffer
 * @waitqueue:  waitqueue
 */
typedef struct dynamic_buffer {
        struct list_head head;
        struct mutex op_mutex;
        wait_queue_head_t waitqueue;
} dynamic_buffer_t;

/*
 * object_t - I/O object
 * @workqueue:  pointer to workqueue for low priority flow
 * @buffer:     two buffer, low and high priority
 */
typedef struct object {
        struct workqueue_struct *workqueue;
        dynamic_buffer_t *buffer[FLOWS];
} object_t;

/*
 * session_t - I/O session
 * @priority:   priority of session
 * @flags:      flags used for allocation (blocking or not)
 * @timeout:    timeout for blocking operations
 */
typedef struct session {
        short priority;
        gfp_t flags;
        unsigned long timeout;
} session_t;

/*
 * packed_work_t - delayed work
 * @staging_area:       byte to write
 * @minor:              minor of device
 * @the_work:           work struct
 */
typedef struct packed_work{
        data_segment_t *staging_area;
        int minor;
        struct work_struct the_work;
} packed_work_t;

/* dynamic buffer functions prototypes */
void    init_dynamic_buffer(dynamic_buffer_t *);
void    init_data_segment(data_segment_t *, char *, int);
void    write_dynamic_buffer(dynamic_buffer_t *, data_segment_t *);
void    read_dynamic_buffer(dynamic_buffer_t *, char *, int);
void    free_data_segment(data_segment_t *);
void    free_dynamic_buffer(dynamic_buffer_t *);

/* MACRO DEFINITION */
#define get_seconds(sec)        (sec > MAX_SECONDS ? sec = MAX_SECONDS : (sec == 0 ? sec = MIN_SECONDS : sec))

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)      MAJOR(session->f_inode->i_rdev)
#define get_minor(session)      MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)      MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)      MINOR(session->f_dentry->d_inode->i_rdev)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0)
#define personal_wait   wait_event_interruptible_exclusive_timeout      
#else
#define personal_wait   wait_event_interruptible_timeout
#endif

/* definition of macro to manage byte in buffer and indexes*/
#define get_byte_in_buffer_index(priority, minor)                               \
        ((priority * MINOR_NUMBER) + minor)

#define byte_to_read(priority,minor)                                            \
        byte_in_buffer[get_byte_in_buffer_index(priority, minor)]

#define busy_space(priority,minor)                                              \
        (priority == LOW_PRIORITY ?                                             \
        (                                                                       \
                byte_in_buffer[get_byte_in_buffer_index(priority, minor)] +     \
                        booked_byte[minor]) :                                   \
                byte_in_buffer[get_byte_in_buffer_index(priority, minor)]       \
        )                                                             

#define free_space(priority,minor)                                              \
        MAX_BYTE_IN_BUFFER - busy_space(priority,minor)         

#define is_there_space(priority,minor)                                          \
        (free_space(priority,minor) == 0 ? 0 : 1)                            

#define is_empty(priority,minor)                                                \
        (byte_to_read(priority,minor) == 0 ? 1 : 0)    

#define is_blocking(flags)                                                      \
        (flags == GFP_ATOMIC ? 0 : 1)

#define add_byte_in_buffer(priority,minor,len)                                  \
        byte_in_buffer[get_byte_in_buffer_index(priority,minor)] += len

#define add_booked_byte(minor,len)                                              \
        booked_byte[minor] += len

#define sub_byte_in_buffer(priority,minor,len)                                  \
        byte_in_buffer[get_byte_in_buffer_index(priority,minor)] -= len

#define sub_booked_byte(minor,len)                                              \
        booked_byte[minor] -= len
        
#define atomic_inc_thread_in_wait(priority,minor)                               \
        __sync_fetch_and_add(                                                   \
                thread_in_wait + get_byte_in_buffer_index(priority,minor),      \
                1)

#define atomic_dec_thread_in_wait(priority,minor)                               \
        __sync_fetch_and_sub(                                                   \
                thread_in_wait + get_byte_in_buffer_index(priority,minor),      \
                1)

/*
 * lock_and_awake
 *
 * This macro return a value of condition evaluated in the macro
 * wait_event_interruptible_exclusive_timeout.
 * 
 * @condition:  condition to evaluate
 * @mutex:      pointer to mutex to try lock
 */
#define lock_and_awake(condition, mutex)                                        \
({                                                                              \
        int __ret = 0;                                                          \
        if (mutex_trylock(mutex)) {                                             \
                if (condition)                                                  \
                        __ret = 1;                                              \
                else                                                            \
                        mutex_unlock(mutex);                                    \
        }                                                                       \
        __ret;                                                                  \
})

/*
 * __wait_event_interruptible_exclusive_timeout
 *
 * This macro allow to put in waitqueue a task in exclusive mode and
 * set a timeout.
 * 
 * @wq_head:    head of waitqueue
 * @condition:  awakeness condition
 * @timeout:    awakeness timeout
 */
#define __wait_event_interruptible_exclusive_timeout(wq_head,condition,timeout) \
	___wait_event(wq_head, ___wait_cond_timeout(condition),			\
		      TASK_INTERRUPTIBLE, 1, timeout,				\
		      __ret = schedule_timeout(__ret))


/*
 * wait_event_interruptible_exclusive_timeout
 *
 * This macro is a change of wait_event_interruptible_timeout because
 * call __wait_event_interruptible_exclusive_timeout instead of the
 * macro __wait_event_interruptible_timeout.
 * 
 * @wq_head:    head of waitqueue
 * @condition:  awakeness condition
 * @timeout:    awakeness timeout
 */
#define wait_event_interruptible_exclusive_timeout(wq_head,condition,timeout)   \
({										\
	long __ret = timeout;							\
	might_sleep();								\
	if (!___wait_cond_timeout(condition))					\
		__ret = __wait_event_interruptible_exclusive_timeout(wq_head,	\
						condition, timeout);		\
	__ret;									\
})

#endif
