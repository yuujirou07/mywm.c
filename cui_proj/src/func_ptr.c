#include<stdio.h>

struct a{
    int a;
    void (*f)(char * f);
};

struct oo{
    int h;
    int i;
};
void f(char * f);
int main(){
    struct a gg;

    gg.f = (void*)f;
    char *uu = "roi\n";
    gg.f(uu);

    struct oo i;
    i.h=0;
    i.i=100;
    int *r = &i.h;
    int *y= &i.h+1;
    printf("%d,%d",*r,*y);
}


void f(char * f){

    printf("%s",f);
}