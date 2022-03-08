#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include "../../lib/my-ioctl.h"
#include "../../lib/parameters.h"

#include "defines.h"

int main(int argc, char** argv){
    int ret;
    int fd;
    int major;
    int minor;
    char *path;

    int num;
    char op = '0';

    char options[8] = {'1', '2', '3', '4', '5', '6', '7', '8'};

    char command[100];
    char tmp[OBJECT_MAX_SIZE];

    if (argc < 4) {
        printf("usage: pathname major minors\n");
        return -1;
    }

    path = argv[1];
    major = strtol(argv[2],NULL,10);
    minor = strtol(argv[3],NULL,10);
    
    sprintf(command,"mknod %s c %d %d\n",path,major,minor);
    system(command);

    // open device
    fd = open(path, O_RDWR);
	if (fd == -1) {
		printf("open error on device %s\n", path);
		return -1;
	}
    
    while(1) {
        printf("\033[2J\033[H");

        if (op == '6') {
            printf("Scrittura: %s (%d byte)\n\n", tmp, ret);
        } else if (op == '7') {
            printf("Lettura: %s (%d byte)\n\n", tmp, ret);
        }
        memset(tmp, 0, OBJECT_MAX_SIZE);

        printf("1. Settare high priority\n");
        printf("2. Settare low priority\n");
        printf("3. Operazioni bloccanti\n");
        printf("4. Operazioni non bloccanti\n");
        printf("5. Settare timeout\n");
        printf("6. Scrivere\n");
        printf("7. Leggere\n");
        printf("8. Esci\n");

        op = multiChoice("Cosa vuoi fare", options, 8);
        
        switch(op){
                case '1':
                    ioctl(fd, TO_HIGH_PRIORITY);
                    break;
                case '2':
                    ioctl(fd, TO_LOW_PRIORITY);
                    break;
                case '3':
                    ioctl(fd, BLOCK);
                    break;
                case '4':
                    ioctl(fd, UNBLOCK);
                    break;
                case '5':
                    ioctl(fd, TIMEOUT, 0);
                    break;
                case '6':
                    printf("Inserisci testo da scrivere: ");
                    fgets(tmp, OBJECT_MAX_SIZE, stdin);
                    ret = write(fd, tmp, strlen(tmp));
                    break;
                case '7':
                    printf("Inserisci quanti byte vuoi leggere: ");
                    scanf("%d", &num);
                    ret = read(fd, tmp, num);
                    break;
                case '8':
                    close(fd);
                    return 0;
                default: 
                    printf("operazione non disponibile");
        }
    }


    return 0;
}
