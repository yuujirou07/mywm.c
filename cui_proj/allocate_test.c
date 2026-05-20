#include <stddef.h>
#include<stdio.h>
#include <stdlib.h>
#include <string.h>
#include<math.h>
#include<stdbool.h>
#include <unistd.h>
#include<time.h>
int *addr(int *ad1,int *ad2,int counter);
int *subt(int *ad1,int *ad2,int counter);
int *division(int *ad1,int *ad2,int counter);
int *multiplic(int *ad1,int *ad2,int counter);

// int型の数値を多倍長整数の配列（一番下の桁基準）に変換する補助関数
int *create_num(int num, int counter) {
    int *arr = calloc(counter, sizeof(int));
    for (int i = counter - 1; i >= 0 && num > 0; i--) {
        arr[i] = num % 10;
        num /= 10;
    }
    return arr;
}

// arctan(1/x) をテイラー級数展開で計算する関数
int *arctan_1_over_x(int x, int counter) {
    int *one = calloc(counter, sizeof(int));
    one[0] = 1; // 1の位を1にして 1.0000... を表現

    int *arr_x = create_num(x, counter);
    
    int *arr_x2 = create_num(x * x, counter);

    // T = 1 / x
    int *T = division(one, arr_x, counter);
    int *S = calloc(counter, sizeof(int));

    // テイラー展開: S = T/1 - T/(3*x^2) + T/(5*x^4) ...
    for (int n = 0; ; n++) { // Tが0になるまで無限にループさせる
        // Tがすべて0になったらこれ以上計算しても無駄なのでループを終了（ここで安全に抜けます）
        bool is_zero = true;
        for (int i = 0; i < counter; i++) {
            if (T[i] != 0) {
                is_zero = false;
                break;
            }
        }
        if (is_zero) break;

        int *arr_div = create_num(2 * n + 1, counter);
        int *term = division(T, arr_div, counter);
        free(arr_div);

        int *next_S;
        if (n % 2 == 0) {
            next_S = addr(S, term, counter);
        } else {
            next_S = subt(S, term, counter);
        }
        free(S);
        S = next_S;
        free(term);

        // 次の項のために T = T / x^2 を計算
        int *next_T = division(T, arr_x2, counter);
        free(T);
        T = next_T;
    }

    free(one);
    free(arr_x);
    free(arr_x2);
    free(T);

    return S;
}

int main(){
    printf("誕生日の月日を入力してください (例5月25日 0525)\n");
    char buff[16];
    fgets(buff,sizeof(char)*16,stdin);
    buff[strcspn(buff, "\n")] = '\0'; // 改行文字を取り除く
    int len=strlen(buff);
    int birth[len];
    for(int i=0;i<len;i++){
        birth[i]=buff[i]-'0';
    }
    printf("計算中です\n");
    int addr_counter = 2500; // 要素数。1要素あたり4桁なので 2500要素 * 4 = 10000桁

    // マチンの公式: pi = 4 * (4 * arctan(1/5) - arctan(1/239))
    int *arctan_1_5 = arctan_1_over_x(5, addr_counter);
    int *arctan_1_239 = arctan_1_over_x(239, addr_counter);

    int *four = create_num(4, addr_counter);

    // 4 * arctan(1/5) を計算
    int *term1 = multiplic(arctan_1_5, four, addr_counter);

    // (4 * arctan(1/5) - arctan(1/239)) を計算
    int *term2 = subt(term1, arctan_1_239, addr_counter);

    // 最後に4倍する: pi = 4 * (4 * arctan(1/5) - arctan(1/239))
    int *pi = multiplic(term2, four, addr_counter);
    clock_t st,ed;
    st=clock();
    int i=0;
    int b_i=0;
    int total_digits = 0; // 出力した小数の桁数をカウント
    bool found = false;

    while(1){
        ed=clock();
        if(ed-st>=50){
            st=ed;
            
            char str_val[16];
            if(i == 0){
                printf("%d.", pi[i]);
                sprintf(str_val, "%d", pi[i]);
            } else {
                printf("%04d", pi[i]); // 4桁でゼロ埋めして出力
                sprintf(str_val, "%04d", pi[i]);
            }
            fflush(stdout); // 画面にリアルタイムで表示を反映させる
            
            for(int c = 0; c < strlen(str_val); c++){
                if(i != 0) total_digits++; // 少数部のみ桁数をカウント
                int digit = str_val[c] - '0';
                if(digit >= 0 && digit <= 9){
                    if(birth[b_i] == digit){
                        b_i++;
                        if(b_i >= len){
                            found = true;
                            break;
                        }
                    } else {
                        b_i = 0;
                        if(birth[b_i] == digit) b_i++;
                    }
                }
            }
            
            i++;
            if(found || i >= addr_counter){
                if(found){
                    printf("\nあなたの誕生日は小数第%d桁目にあります\n", total_digits - len + 1);
                } else {
                    printf("\nあなたの誕生日は設定した桁数にはありませんでした\n");
                }
                break;
            }
        }
        
    }
    printf("\n");

    // メモリの解放
    free(arctan_1_5);
    free(arctan_1_239);
    free(four);
    free(term1);
    free(term2);
    free(pi);
 
    return 0;
}
int *addr(int *ad1,int *ad2,int counter){
    int carry = 0; // 桁上がりを保持する変数
    int result[counter];

    memset(result,0,sizeof(int)*counter);
    
    int counter_save=counter;
    for(int i=counter-1;i>=0;i--){
        int addered=ad1[i]+ad2[i] + carry;
        if(addered >= BASE){
            carry = 1;
            //桁上がりを抜く
            addered -= BASE;
        } else {
            carry = 0;
        }
        result[i]=addered;
    }
    int *return_arry=calloc(counter_save,sizeof(int));
    memcpy(return_arry, result, sizeof(int) * counter_save);

    return return_arry;
}
//ひかれる数ad1　ab2引く数
int *subt(int *ad1,int *ad2,int counter){
    int result[counter];
    int bollow=0;
    memset(result,0,sizeof(int)*counter);

    int counter_save=counter;
    for(int i=counter-1;i>=0;i--){
        int addered=ad1[i]-ad2[i]-bollow;
        if(addered<0){
            bollow=1;
            addered+=BASE;
        }
        else {
            bollow=0;
        }
        result[i]=addered;
    }
    
    int *return_arry=calloc(counter_save,sizeof(int));
    memcpy(return_arry, result, sizeof(int) * counter_save);

    return return_arry;
}
//ad1割られる数
int *division(int *ad1,int *ad2,int counter){
    int *return_arry = calloc(counter, sizeof(int));
    if (return_arry == NULL) return NULL;

    // ゼロ除算（0で割る）を防ぐためのチェック
    bool is_zero = true;
    for (int i = 0; i < counter; i++) {
        if (ad2[i] != 0) {
            is_zero = false;
            break;
        }
    }
    if (is_zero) {
        free(return_arry);
        return NULL;
    }

    // 筆算中の「余り（引かれたあとの数）」を保持する配列
    int temp_rem[counter];
    memset(temp_rem, 0, sizeof(int) * counter);

    // 上の桁から順番に計算していく（筆算と同じ手順）
    for (int i = 0; i < counter; i++) {
        // 1要素左にシフト（BASE倍する）
        for (int j = 0; j < counter - 1; j++) {
            temp_rem[j] = temp_rem[j + 1];
        }
        // 次の桁を下ろしてくる
        temp_rem[counter - 1] = ad1[i];
        
        // temp_rem が ad2 以上である限り引き続ける
        while (1) {
            // 大小比較（上の桁から比較）
            int cmp = 0;
            for (int k = 0; k < counter; k++) {
                if (temp_rem[k] > ad2[k]) {
                    cmp = 1; break;
                } else if (temp_rem[k] < ad2[k]) {
                    cmp = -1; break;
                }
            }
            
            // 引く数より小さくなったら（これ以上引けなくなったら）ループを抜ける
            if (cmp < 0) break;
            
            // 引き算（temp_rem - ad2）
            int borrow = 0;
            for (int k = counter - 1; k >= 0; k--) {
                int sub = temp_rem[k] - ad2[k] - borrow;
                if (sub < 0) {
                    sub += BASE;
                    borrow = 1;
                } else {
                    borrow = 0;
                }
                temp_rem[k] = sub;
            }
            
            // 割り算の答え（商）の該当する桁に1を足す（引き算できた回数を記録）
            return_arry[i]++;
        }
    }

    return return_arry;
}
int *multiplic(int *ad1,int *ad2,int counter){
    int *return_arry = calloc(counter, sizeof(int));
    if (return_arry == NULL) return NULL;
    
    // 各桁の掛け算を一旦足し合わせるための一時配列
    // 掛け算の結果がintの最大値を超える可能性があるため、long long型にする
    long long temp_result[counter];
    memset(temp_result, 0, sizeof(long long) * counter);

    // 1. 各桁ごとに掛け算をして、該当する桁の位置に足し込む
    for(int i=counter-1;i>=0;i--){
        for(int j=counter-1;j>=0;j--){
            // 筆算の時の「どの位置に数字を書くか」を計算
            int pos = i + j - (counter - 1);
            if (pos >= 0) {
                temp_result[pos] += (long long)ad1[i] * ad2[j];
            }
        }
    }

    // 2. 下の桁から順番に繰り上がり（キャリー）を処理する
    long long carry = 0;
    for(int i=counter-1;i>=0;i--){
        long long sum = temp_result[i] + carry;
        return_arry[i] = sum % BASE;
        carry = sum / BASE;
    }

    return return_arry;
}
