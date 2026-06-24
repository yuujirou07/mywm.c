#ifndef MY_DIR_H
#define MY_DIR_H
#include <sys/syscall.h> 
#include <dirent.h>  

int getdir(struct linux_dirent64 *buf,char *path_name,int count);




#endif