#ifndef DEFINES_H
#define DEFINES_H

/* MODULE INFORMATIONS DEFINITION */

#define MODNAME "CHAR DEV"
#define DEVICE_NAME "multi-flow device"

/* CONSTANTS DEFINITION */

/* general information */
#define MAX_BYTE_IN_BUFFER 32*4096      // 32 pagine
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
        char *content;              // content of segment
        int byte_read;              // byte number read
        int size;
        struct data_segment* next;
} data_segment_t;

/* definition of dynamic buffer */
typedef struct dynamic_buffer {
        data_segment_t* head;     // head pointer to read
        data_segment_t* tail;     // tail pointer to attach new written block
        struct mutex operation_synchronizer;
        wait_queue_head_t waitqueue;
} dynamic_buffer_t;

/* struct to abstract IO object */
typedef struct object {
        struct workqueue_struct *workqueue;     // the workqueue is in object_t because is only for low priority
        dynamic_buffer_t *buffer[FLOWS];
} object_t;

/* session struct */
typedef struct session {
        int priority;
        bool blocking;
        unsigned long timeout;
} session_t;

/* delayed work */
typedef struct packed_work{
        data_segment_t *staging_area;
        int minor;
        struct work_struct the_work;
} packed_work_t;

/* FUNCTIONS PROTOTYPE */

/* dynamic buffer functions prototype */
int             init_dynamic_buffer(dynamic_buffer_t *);
int             init_data_segment(data_segment_t *, char *, int);
unsigned long   write_dynamic_buffer(dynamic_buffer_t *, data_segment_t *);
void            read_dynamic_buffer(dynamic_buffer_t *, char *, int);
void            free_segment_buffer(data_segment_t *);
void            free_dynamic_buffer(dynamic_buffer_t *);

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

/* redefinition of wait_event_interruptible_timeout for awake only one thread */
#define __p_wait_event_interruptible_timeout(wq, condition, ret, mutex)	        \
do {									        \
	DEFINE_WAIT(__wait);						        \
									        \
	for (;;) {							        \
		prepare_to_wait(&wq, &__wait, TASK_INTERRUPTIBLE);	        \
                if (mutex_trylock(mutex)) {                                     \
		        if (condition)                                          \
                                break;                                          \
                        else                                                    \
                                mutex_unlock(mutex);                            \
                }                  					        \
									        \
		if (!signal_pending(current)) {				        \
			ret = schedule_timeout(ret);			        \
			if (!ret)					        \
				break;					        \
			continue;					        \
		}							        \
		ret = -ERESTARTSYS;					        \
		break;							        \
	}								        \
	finish_wait(&wq, &__wait);					        \
} while (0)

#define p_wait_event_interruptible_timeout(wq, condition, timeout, mutex)       \
({									        \
	long __ret = timeout;						        \
	if (!(condition))						        \
		__p_wait_event_interruptible_timeout(wq, condition, __ret, mutex);\
	__ret;								        \
})

#endif
