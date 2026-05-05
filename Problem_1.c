#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct {
    int n;
    int* fibonacci_seq;
} fib_data_t;

typedef struct {
    int* fibonacci_seq;
    int fib_size;
    int search_index;
    int result;
} search_data_t;


void* compute_fibonacci(void* arg);
void* search_fibonacci(void* arg);

int main() {
    int n, search_count;
    pthread_t fib_thread, search_thread;

    printf("Enter the term of fibonacci sequence:\n");
    scanf("%d", &n);
    
    if (n < 0 || n > 40) {
        printf("Error: n must be between 0 and 40\n");
        return 1;
    }
    
    fib_data_t fib_data;
    fib_data.n = n;
    fib_data.fibonacci_seq = NULL;
    
    if (pthread_create(&fib_thread, NULL, compute_fibonacci, &fib_data) != 0) {
        printf("Error creating Fibonacci thread\n");
        return 1;
    }
    
    if (pthread_join(fib_thread, NULL) != 0) {
        printf("Error joining Fibonacci thread\n");
        return 1;
    }
    
    for (int i = 0; i <= n; i++) {
        printf("a[%d] = %d\n", i, fib_data.fibonacci_seq[i]);
    }
    
    printf("How many numbers you are willing to search?:\n");
    scanf("%d", &search_count);
    
    if (search_count <= 0) {
        printf("Error: search count must be greater than 0\n");
        free(fib_data.fibonacci_seq);
        return 1;
    }

    for (int i = 0; i < search_count; i++) {
        int search_index;
        printf("Enter search %d:\n", i + 1);
        scanf("%d", &search_index);

        search_data_t search_data;
        search_data.fibonacci_seq = fib_data.fibonacci_seq;
        search_data.fib_size = n;
        search_data.search_index = search_index;
        
        if (pthread_create(&search_thread, NULL, search_fibonacci, &search_data) != 0) {
            printf("Error creating search thread\n");
            continue;
        }
        
        if (pthread_join(search_thread, NULL) != 0) {
            printf("Error joining search thread\n");
            continue;
        }
        
        printf("result of search #%d = %d\n", i + 1, search_data.result);
    }
    
    free(fib_data.fibonacci_seq);
    
    return 0;
}

void* compute_fibonacci(void* arg) {
    fib_data_t* data = (fib_data_t*)arg;
    int n = data->n;
    

    data->fibonacci_seq = (int*)malloc((n + 1) * sizeof(int));
    if (data->fibonacci_seq == NULL) {
        printf("Memory allocation failed\n");
        return NULL;
    }
    
    if (n >= 0) {
        data->fibonacci_seq[0] = 0;
    }
    if (n >= 1) {
        data->fibonacci_seq[1] = 1;
    }
    
    for (int i = 2; i <= n; i++) {
        data->fibonacci_seq[i] = data->fibonacci_seq[i-1] + data->fibonacci_seq[i-2];
    }
    
    return NULL;
}

void* search_fibonacci(void* arg) {
    search_data_t* data = (search_data_t*)arg;
    
    // Check if search index is valid
    if (data->search_index < 0 || data->search_index > data->fib_size) {
        data->result = -1;
    } else {
        data->result = data->fibonacci_seq[data->search_index];
    }
    
    return NULL;
}
