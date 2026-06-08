#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"error_log_output.h"

void error_log_write(char *error_statement){
  FILE *error_f;
  error_f=fopen("error_log.txt","a");

  if(error_f==NULL){
    perror("cant open error_log.txt");
    exit(1);
  }
  char buff[strlen(error_statement  )+1];

  snprintf(buff,strlen(error_statement  )+1,"%s\n",error_statement);

  ssize_t size=fputs(buff,error_f);
  if(size<0)
    perror("can not write on error_log.txt");

  fclose(error_f);
}