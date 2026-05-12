#include<stdio.h>
#include <string.h>
#include <stdlib.h>
int main(){
    char *a=malloc(sizeof(char)*10);
    memcpy(a,"123456789",sizeof("123456789"));
    memmove(a, &a[4],strlen(&a[4]));
    printf("%s\n",a);

}