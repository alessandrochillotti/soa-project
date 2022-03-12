Skip to content
Search or jump to…
Pull requests
Issues
Marketplace
Explore
 
@alessandrochillotti 
alessandrochillotti
/
soa-project
Private
Code
Issues
2
Pull requests
Actions
Projects
Security
Insights
Settings
soa-project/user/user.c
@alessandrochillotti
alessandrochillotti #16
Latest commit af1e686 17 hours ago
 History
 1 contributor
109 lines (93 sloc)  3.17 KB
  
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "lib/defines.h"
#include "lib/user.h"

int main(int argc, char** argv)
{
        int ret;
        int fd;
        int major;
        int minor;
        char *path;
        unsigned long timeout;

        int num;
        char op = '0';

        char options[8] = {'1', '2', '3', '4', '5', '6', '7', '8'};

        char command[100];
        char tmp[MAX_TMP_SIZE];

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

        while (true) {
                printf("\033[2J\033[H");

                if (op == '6') {
                        printf("Scritti primi %d byte di %s\n\n", ret, tmp);
                } else if (op == '7') {
                        printf("Lettura: %s (%d byte)\n\n", tmp, ret);
                }
                memset(tmp, 0, MAX_TMP_SIZE);

                printf("1. Settare high priority\n");
                printf("2. Settare low priority\n");
                printf("3. Operazioni bloccanti\n");
                printf("4. Operazioni non bloccanti\n");
                printf("5. Settare timeout\n");
                printf("6. Scrivere\n");
                printf("7. Leggere\n");
                printf("8. Esci\n");

                op = multiChoice("Cosa vuoi fare", options, 8);

                switch(op) {
                case '1':
                        turn_to_high_priority(fd);
                        break;
                case '2':
                        turn_to_low_priority(fd);
                        break;
                case '3':
                        set_blocking_operations(fd);
                        break;
                case '4':
                        set_unblocking_operations(fd);
                        break;
                case '5':
                        printf("Inserisci il valoro del timeout: ");
                        do {
                        scanf("%ld", &timeout);
                        } while (timeout < 1);
                        set_timeout(fd, timeout);
                        break;
                case '6':
                        printf("Inserisci testo da scrivere: ");
                        fgets(tmp, MAX_TMP_SIZE, stdin);
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