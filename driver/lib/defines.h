#ifndef DEFINES_H
#define DEFINES_H

/* MODULE INFORMATIONS DEFINITION */

#define MODNAME "CHAR DEV"
#define DEVICE_NAME "multi-flow device"

/* CONSTANTS DEFINITION */

/* general information */
#define MAX_BYTE_IN_BUFFER 10//32*4096      // 32 pagine
#define MINOR_NUMBER 128
#define FLOWS 2

#define LOW_PRIORITY 0
#define HIGH_PRIORITY 1

/* ioctl indexes */
#define TO_HIGH_PRIORITY        3
#define TO_LOW_PRIORITY         4
#define BLOCK                   5
#define UNBLOCK                 6
#define TIMEOUT                 7

/* upper and lower bound for seconds */
#define MIN_SECONDS             1
#define MAX_SECONDS             17179869        // 2^(32) = 4294967296 -> 4294967296/250 > 17179869.184 (no overflow)

/* STRUCTURES DEFINITION */

/* definition of dynamic buffer segment */
typedef struct data_segment {
        struct list_head list;
        char *content;                  // content of segment
        int byte_read;                  // byte number read
        int size;
} data_segment_t;

/* definition of dynamic buffer */
typedef struct dynamic_buffer {
        struct list_head head;     // head pointer to read
        struct mutex operation_synchronizer;
        wait_queue_head_t waitqueue;
        struct list_head head_process;
} dynamic_buffer_t;

/* struct to abstract IO object */
typedef struct object {
        struct workqueue_struct *workqueue;     // the workqueue is in object_t because is only for low priority
        dynamic_buffer_t *buffer[FLOWS];
} object_t;

/* session struct */
typedef struct session {
        int priority;
        // bool blocking;
        gfp_t flags;
        unsigned long timeout;
} session_t;

/* delayed work */
typedef struct packed_work{
        data_segment_t *staging_area;
        int minor;
        struct work_struct the_work;
} packed_work_t;

/* element for struct task_struct list */
typedef struct task_struct_element {
        struct task_struct *p;
        struct list_head list;
} task_struct_element_t;

/* FUNCTIONS PROTOTYPE */

/* dynamic buffer functions prototype */
int     init_dynamic_buffer(dynamic_buffer_t *);
int     init_data_segment(data_segment_t *, char *, int);
void    write_dynamic_buffer(dynamic_buffer_t *, data_segment_t *);
void    read_dynamic_buffer(dynamic_buffer_t *, char *, int);
void    free_segment_buffer(data_segment_t *);
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

/* definition of macro to manage byte in buffer and indexes*/
#define get_booked_byte_index(priority,minor)                                   \
        (priority == HIGH_PRIORITY ? MINOR_NUMBER : minor)

#define get_byte_in_buffer_index(priority, minor)                               \
        ((priority * MINOR_NUMBER) + minor)

#define byte_to_read(priority,minor)                                            \
        byte_in_buffer[get_byte_in_buffer_index(priority, minor)]

#define busy_space(priority,minor)                                              \
        (byte_in_buffer[get_byte_in_buffer_index(priority, minor)] +            \
        booked_byte[get_booked_byte_index(priority,minor)])                                                                                            \

#define free_space(priority,minor)                                              \
        MAX_BYTE_IN_BUFFER - busy_space(priority,minor)         

#define is_there_space(priority,minor)                                          \
        (free_space(priority,minor) == 0 ? 0 : 1)                            

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

#define mutex_try_or_lock(mutex,flags)                                          \
        if (is_blocking(flags))                                                 \
                mutex_lock(mutex);                                              \
        else {                                                                  \
                if (!mutex_trylock(mutex))                                      \
                        return -EAGAIN;                                         \
        }

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

#define __wait_event_interruptible_exclusive_timeout(wq_head,condition,timeout) \
	___wait_event(wq_head, ___wait_cond_timeout(condition),			\
		      TASK_INTERRUPTIBLE, 1, timeout,				\
		      __ret = schedule_timeout(__ret))


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
