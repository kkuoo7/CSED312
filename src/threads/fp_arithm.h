// #ifndef THREADS_FP_ARITHM_H
// #define THREADS_FP_ARITHM_H


/*
informations taken from PinOS official guide
x and y are fixed-point numbers, n is an integer, fixed-point
numbers are in signed p.q format where p + q = 31, and f is 1 << q:
17.14 formet fixed point number:

MSB(31) - sign bit
30-14 - integer part
13-0 - fractional part

*/

#define FP_1 (1 << 14) //1.00000000000000 = 1

int fp_to_int(int x)
{
    return x / FP_1;
}

int fp_to_int_rounding(int x)
{
    return x >= 0 ? (x + FP_1 / 2) / FP_1 : (x - FP_1 / 2) / FP_1;
}

int int_to_fp(int n)
{
    return n * FP_1;
}

int fp_add(int x, int y){
    return x + y;
}

int fp_sub(int x, int y){
    return x - y;
}

int fp_add_int(int x, int n){
    return x + int_to_fp(n);
}

int fp_sub_int(int x, int n){
    return x - int_to_fp(n);
}

int fp_mul(int x, int y){
    return ((int64_t)x) * y / FP_1;
}

int fp_mul_int(int x, int n){
    return x * n;
}

int fp_div(int x, int y){
    return ((int64_t)x) * FP_1 / y;
}

int fp_div_int(int x, int n){
    return x / n;
}

//#endif /* threads/fp_arithm.h */