#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_COMMAND 100

char command[MAX_COMMAND];

int main(int argc, char** argv){

     int ret;
     char *path;
     int major;
     int minor;

     if (argc < 3) {
	     printf("usage: path major minor");
	     return -1;
     }

     path = argv[1];
     major = strtol(argv[2], NULL, 10);
     minor = strtol(argv[3], NULL, 10);

     printf("creating device file with minor %d for device %s with major %d\n", minor, path, major);

     sprintf(command, "mknod %s c %d %i\n", path, major, minor);
     system(command);

     printf("trying to open %s\n",argv[1]);

     ret = open(argv[1],O_RDWR);
     printf("open returned %d\n",ret);
      
     ret = write(ret,"ab",2);
     printf("write returned %d\n",ret);
	
     pause();
}
