#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>

#include "lib/user.h"

#define MINOR_NUMBER 128
#define DATA "ciao\n"
#define SIZE strlen(DATA)

char *to_write[10] = {"a","b","c","d","e","f","g","h","i","l"};

typedef struct info_thread {
        char* path;
        int id;
} info_thread_t;

void *the_thread(void* path)
{
        int fd;
        info_thread_t *info = (info_thread_t *)path;
        char content_read[4096];

        sleep(1);

        // printf("opening device %s\n",device);
        fd = open(info->path,O_RDWR);
        if (fd == -1) {
                printf("open error on device %s\n", info->path);
                return NULL;
        }
        
        printf("device %s successfully opened\n", info->path);
       
#ifdef TEST_1
        turn_to_low_priority(fd);
        if (info->id != 4) {
                read(fd, content_read, 4);
                printf("ho letto %s\n", content_read);
        } else {
                int written_bytes;
                written_bytes = write(fd, DATA, SIZE);
                printf("ho scritto %d byte\n", written_bytes);
        }
#elif defined TEST_2 
        read(fd, content_read, 4);
        printf("ho letto %s\n", content_read);
#elif defined TEST_3
        int written_bytes;
        written_bytes = write(fd, DATA, SIZE);
        printf("ho scritto %d byte\n", written_bytes);
#elif defined TEST_4
        turn_to_low_priority(fd);
        read(fd, content_read, 4);
        printf("ho letto %s\n", content_read);
#elif defined TEST_5
        int written_bytes;
        turn_to_low_priority(fd);
        written_bytes = write(fd, DATA, SIZE);
        printf("ho scritto %d byte\n", written_bytes);
#elif defined TEST_6
        int written_bytes;
        turn_to_low_priority(fd);
        written_bytes = write(fd, to_write[info->id], 1);
        printf("ho prenotato %d byte\n", written_bytes);
#endif

        return NULL;
}

int main(int argc, char** argv)
{
        int ret;
        int major;
        int threads;
        char *device;
        char command[100];
        pthread_t tid;
        info_thread_t thread[MINOR_NUMBER];

        if (argc < 4) {
                printf("usage: pathname major thread\n");
                return -1;
        }

        major = strtol(argv[2],NULL,10);
        threads = strtol(argv[3],NULL,10);

        printf("creating %d thread for device %s with major %d\n", threads, argv[1], major);
        
        device = strdup(argv[1]);

        for (int i = 0; i < threads; i++) {
                thread[i].path = device;
                thread[i].id = i;
                
                pthread_create(&tid,NULL,the_thread,&thread[i]);
        }

        pause();

        return 0;
}
