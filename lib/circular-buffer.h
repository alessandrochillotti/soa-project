#pragma once

/* definition of circular buffer */
typedef struct circular_buffer {
    char* stream;               // char stream
    int begin;                  // first byte to read
    int valid_bytes;            // valid_bytes + begin = point to write
} circular_buffer_t;

/* functions prototypes */
int init_circular_buffer(circular_buffer_t *);
void free_circular_buffer(circular_buffer_t *);
unsigned long circular_copy_to_user (char __user *, const circular_buffer_t, unsigned long);
unsigned long circular_copy_from_user (circular_buffer_t *, const char __user *, unsigned long);

/* functions */
int init_circular_buffer(circular_buffer_t *buffer) {
    buffer->begin = 0;
    buffer->valid_bytes = 0;
    buffer->stream = NULL;
    buffer->stream = (char*)__get_free_page(GFP_KERNEL);
    if (buffer->stream == NULL) return -ENOMEM; 

    return 0;
}

void free_circular_buffer(circular_buffer_t *buffer) {
    free_page((unsigned long) buffer->stream[LOW_PRIORITY]);
    free_page((unsigned long) buffer->stream[HIGH_PRIORITY]);
}

int free_bytes(circular_buffer_t *to_buffer) {
    return OBJECT_MAX_SIZE - to_buffer->valid_bytes;
}

unsigned long circular_copy_from_user(circular_buffer_t *to_buffer, const char __user * from_buffer, unsigned long n) {
    
    unsigned long ret;
    int seek_buffer;

    seek_buffer = (to_buffer->begin + to_buffer->valid_bytes)%OBJECT_MAX_SIZE;

    if (seek_buffer + n >= OBJECT_MAX_SIZE) { // end of buffer reached
        ret = copy_from_user(to_buffer->stream + seek_buffer, from_buffer, OBJECT_MAX_SIZE - seek_buffer);
        ret = copy_from_user(to_buffer->stream, from_buffer + (OBJECT_MAX_SIZE - seek_buffer), (n - (OBJECT_MAX_SIZE - seek_buffer)));
    } else {
        ret = copy_from_user(to_buffer->stream + seek_buffer, from_buffer, n);
    }

    return ret;
}

unsigned long circular_copy_to_user(char __user *to_buffer, const circular_buffer_t from_buffer, unsigned long n) {

    unsigned long ret;

    if (from_buffer.begin + n >= OBJECT_MAX_SIZE) { // end of buffer reached
        ret = copy_to_user(to_buffer, from_buffer.stream + from_buffer.begin, OBJECT_MAX_SIZE - from_buffer.begin);
        ret = copy_to_user(to_buffer + (OBJECT_MAX_SIZE - from_buffer.begin), from_buffer.stream, (n - (OBJECT_MAX_SIZE - from_buffer.begin)));
    } else {
        ret = copy_to_user(to_buffer, from_buffer.stream + from_buffer.begin, n);
    }

    return ret;
}