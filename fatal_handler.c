#include <stdio.h>
#include <stdlib.h>;
int fatal_handler(void *udata, const char *msg){
    printf("%s", msg);
    exit(1);
    return 1;
}