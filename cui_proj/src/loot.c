#include<stdio.h>
#include<math.h>

float Q_rsqrt( float number);
float my_sqrt(float n,int count);
int main(){
    float loot_n = my_sqrt(2, 20);
    float loot_n2 = Q_rsqrt(2);
    float loot_n3 = sqrt(2);
    printf("%f\n",loot_n);
    printf("%f\n",loot_n2 * 2);
    printf("%f\n",loot_n3);
    return 0;


}
float Q_rsqrt( float number ) 
{ 
    long i; 
    float x2, y; 
    const float threehalfs = 1.5F; 
 
    x2 = number * 0.5F; 
    y = number; 
    i = * ( long * ) &y;    // evil floating point bit level hacking 
    i = 0x5f3759df - ( i >> 1 );              // what the fuck? 
    y = * ( float* ) &i; 
    y = y * (threehalfs - ( x2 * y * y ) );   // 1st iteration 
//  y = y * (threehalfs - ( x2 * y * y ) );   // 2nd iteration, 
                                           // this can be removed 
 
    return y; 
}

float my_sqrt(float n,int count){
    float st,ed;
    st=0;
    ed=n;
    float a=0;
    while(count-- > 0){
        a = (float)(ed+st)/2;
        if(a*a > n){
            ed=a;
        }
        else{
            st=a;
        }
    }
    return a;
}