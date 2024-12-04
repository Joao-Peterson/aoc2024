#ifndef _TEST_HEADER_
#define _TEST_HEADER_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>

// global test count
size_t test_counter = 1;

//failed test count
size_t fail_counter = 0;

// void make_bar(double value, double max, char *array, size_t size, char chr){
//     size_t lim = (size_t)((value*size)/max);
    
//     for(size_t i = 0; i < size; i++){
//         if(i <= lim)
//             array[i] = chr;
//         else 
//             array[i] = ' ';
//     }
// }

void test_call(bool condition, char *file, int line, const char *text, ...){
    va_list args;
    va_start(args, text);

    static char buffer[500];
    vsnprintf(buffer, 500 - 1, text, args);
    
    if(condition){
        printf("[TEST: %3lu] [PASSED] [%s:%d]: %s\n", (unsigned long)test_counter, file, line, buffer);
    }
    else{
        printf("[TEST: %3lu] [FAILED] [%s:%d]: %s\n", (unsigned long)test_counter, file, line, buffer);
        fail_counter++;
    }

    va_end(args);
    test_counter++;
}

void test_summary(void){
    printf("[SUMMARY]: %lu succeeded, %lu failed\n", (unsigned long)(test_counter - 1 - fail_counter), (unsigned long)fail_counter);
}

// takes a condition to test, if true passes if false fails
#define test(condition, text, ...) test_call((condition), __FILE__, __LINE__, text, ##__VA_ARGS__)

#endif