#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>


typedef enum {
    BREAD,
    CHEESE,
    LETTUCE,
    INGREDIENT_COUNT
} Ingredient;


pthread_mutex_t table_mutex;
sem_t supplier_sem;
sem_t maker_sem[INGREDIENT_COUNT];
int table_ingredients[2];
int table_empty = 1;
int N;


void* supplier(void* arg);
void* sandwich_maker(void* arg);
const char* ingredient_name(Ingredient ing);
Ingredient get_missing_ingredient(int ing1, int ing2);
void make_sandwich(Ingredient maker_type);

int main() {
    printf("Enter the number of times supplier places ingredients: ");
    scanf("%d", &N);
    
    if (N <= 0) {
        printf("Number of placements must be positive\n");
        return 1;
    }
    
    pthread_mutex_init(&table_mutex, NULL);
    sem_init(&supplier_sem, 0, 1); // Supplier starts first
    
    for (int i = 0; i < INGREDIENT_COUNT; i++) {
        sem_init(&maker_sem[i], 0, 0); // All makers start blocked
    }
    
  
    srand(time(NULL));
    

    pthread_t supplier_thread;
    pthread_t maker_threads[INGREDIENT_COUNT];
    
    pthread_create(&supplier_thread, NULL, supplier, NULL);
    
    int maker_ids[INGREDIENT_COUNT] = {BREAD, CHEESE, LETTUCE};
    const char* maker_names[INGREDIENT_COUNT] = {"A", "B", "C"};
    
    for (int i = 0; i < INGREDIENT_COUNT; i++) {
        pthread_create(&maker_threads[i], NULL, sandwich_maker, &maker_ids[i]);
    }
    
    pthread_join(supplier_thread, NULL);
    
    for (int i = 0; i < INGREDIENT_COUNT; i++) {
        pthread_cancel(maker_threads[i]);
    }

    for (int i = 0; i < INGREDIENT_COUNT; i++) {
        pthread_join(maker_threads[i], NULL);
    }
    
    pthread_mutex_destroy(&table_mutex);
    sem_destroy(&supplier_sem);
    for (int i = 0; i < INGREDIENT_COUNT; i++) {
        sem_destroy(&maker_sem[i]);
    }
    
    return 0;
}

void* supplier(void* arg) {
    for (int i = 0; i < N; i++) {
        sem_wait(&supplier_sem);
        
        pthread_mutex_lock(&table_mutex);
        
        int ing1, ing2;
        do {
            ing1 = rand() % INGREDIENT_COUNT;
            ing2 = rand() % INGREDIENT_COUNT;
        } while (ing1 == ing2);
        
        table_ingredients[0] = ing1;
        table_ingredients[1] = ing2;
        table_empty = 0;
        
        printf("Supplier places: %s and %s\n", 
               ingredient_name(ing1), ingredient_name(ing2));
        
        Ingredient missing_ing = get_missing_ingredient(ing1, ing2);
        
        pthread_mutex_unlock(&table_mutex);
        
        sem_post(&maker_sem[missing_ing]);
    }
    return NULL;
}

void* sandwich_maker(void* arg) {
    Ingredient my_ingredient = *(Ingredient*)arg;
    
    while (1) {
        sem_wait(&maker_sem[my_ingredient]);
        
        pthread_mutex_lock(&table_mutex);
        
        printf("Maker %c picks up %s and %s\n", 
               'A' + my_ingredient,
               ingredient_name(table_ingredients[0]),
               ingredient_name(table_ingredients[1]));
        
        table_empty = 1;
        
        pthread_mutex_unlock(&table_mutex);
        
        make_sandwich(my_ingredient);
        
        // Signal supplier that table is empty
        sem_post(&supplier_sem);
    }
    
    return NULL;
}

void make_sandwich(Ingredient maker_type) {
    printf("Maker %c is making the sandwich...\n", 'A' + maker_type);
    usleep(500000); // 0.5 seconds
    printf("Maker %c finished making the sandwich and eats it\n", 'A' + maker_type);
    printf("Maker %c signals Supplier\n\n", 'A' + maker_type);
}

const char* ingredient_name(Ingredient ing) {
    switch (ing) {
        case BREAD: return "Bread";
        case CHEESE: return "Cheese";
        case LETTUCE: return "Lettuce";
        default: return "Unknown";
    }
}

Ingredient get_missing_ingredient(int ing1, int ing2) {
    if (ing1 != BREAD && ing2 != BREAD) return BREAD;
    if (ing1 != CHEESE && ing2 != CHEESE) return CHEESE;
    if (ing1 != LETTUCE && ing2 != LETTUCE) return LETTUCE;
    return BREAD;
}
