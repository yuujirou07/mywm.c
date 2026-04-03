#include<stdio.h>
#include<stdlib.h>
#include<string.h>
int main(int argv,char* argc[]){
    if(argv==1){
        FILE *fp=fopen("mywm.c","r");
        FILE *txt=fopen("mywm.txt","w");
        char buf[256];
        while(fgets(buf,sizeof(buf),fp)!=NULL){
            fputs(buf,txt);
        }
        fclose(fp);
        fclose(txt);
    }
    else{
        int argv_counter=1;
        while(argv_counter<=argv){
            char buf[256];
            strcpy(buf,argc[argv_counter]);
             char *dotc_ptr=strstr(buf,".c");
            if(dotc_ptr!=NULL){
                strcpy(dotc_ptr,"\0");
                strcat(buf,".txt");
            }
            FILE *fp=fopen(argc[argv_counter],"r");
            if(fp==NULL){
                printf("not found path");
                return 1;
            }
            FILE *txt=fopen(buf,"w");
            while(fgets(buf,sizeof(buf),fp)!=NULL){
                fputs(buf,txt);
            }
            fclose(txt);
            fclose(fp);
            argv_counter++;

        }
    }
    return 0;
}