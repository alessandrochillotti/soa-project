#ifndef CIRCULARBUFFER_H_
#define CIRCULARBUFFER_H_

/* definition of circular buffer */
typedef struct circular_buffer {
    char* stream;               // char stream
    int begin;                  // first byte to read
    int valid_bytes;            // valid_bytes + begin = point to write
} circular_buffer_t;

/* functions prototypes */
unsigned long circular_copy_to_user (char __user *, const char *, unsigned long, int);
unsigned long circular_copy_from_user (char **, const char __user *, unsigned long, int);

/* functions */
unsigned long circular_copy_from_user (char **to_buffer, const char __user * from_buffer, unsigned long n, int seek_buffer) {
    
    unsigned long ret;

    if (seek_buffer + n >= OBJECT_MAX_SIZE) { // end of buffer reached
        ret = copy_from_user(to_buffer[seek_buffer], from_buffer, OBJECT_MAX_SIZE - seek_buffer);
        ret = copy_from_user(to_buffer[0], from_buffer + (OBJECT_MAX_SIZE - seek_buffer), (n - OBJECT_MAX_SIZE - seek_buffer));
    } else {
        ret = copy_from_user(to_buffer[seek_buffer], from_buffer, n);
    }

    return ret;
}

unsigned long circular_copy_to_user (char __user *to_buffer, const char *from_buffer, unsigned long n, int begin) {

    unsigned long ret;

    if (begin + n >= OBJECT_MAX_SIZE) { // end of buffer reached
        ret = copy_to_user(to_buffer, from_buffer + begin, OBJECT_MAX_SIZE - begin);
        ret = copy_to_user(to_buffer + (OBJECT_MAX_SIZE - begin), from_buffer, (n - OBJECT_MAX_SIZE - begin));
    } else {
        ret = copy_to_user(to_buffer, from_buffer + begin, n);
    }

    return ret;
}

#endif