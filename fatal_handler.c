#include <stdio.h>
#include <stdlib.h>
extern int fatal_handler(void *udata, const char *msg){
    printf("%s", msg);
    exit(1);
    return 1;
}