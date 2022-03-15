#pragma once

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

/* definition of dynamic buffer element */
typedef struct buffer_element {
        char *content;              // content of element
        int byte_read;              // byte number read
        int size;
        struct buffer_element* next;
} buffer_element_t;

/* definition of dynamic buffer */
typedef struct dynamic_buffer {
        buffer_element_t* head;     // head pointer to read
        buffer_element_t* tail;     // tail pointer to attach new written block
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
        char* staging_area;		        // data to write
        int size;
        int minor;
        struct work_struct the_work;
} packed_work_t;

/* MACRO DEFINITION */
#define get_seconds(sec)        (sec > MAX_SECONDS ? sec = MAX_SECONDS : (sec == 0 ? sec = MIN_SECONDS : sec))

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)      MAJOR(session->f_inode->i_rdev)
#define get_minor(session)      MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)      MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)      MINOR(session->f_dentry->d_inode->i_rdev)
#endif

/* FUNCTIONS PROTOTYPE */

/* dynamic buffer functions prototype */
int             init_dynamic_buffer(dynamic_buffer_t *);
int             init_buffer_element(buffer_element_t *, char *, int);
unsigned long   write_dynamic_buffer(dynamic_buffer_t *, char *, int);
unsigned long   read_dynamic_buffer(dynamic_buffer_t *, char *, int);
void            free_element_buffer(buffer_element_t *);
void            free_dynamic_buffer(dynamic_buffer_t *);