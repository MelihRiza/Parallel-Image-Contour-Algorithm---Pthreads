#include "helpers.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define CONTOUR_CONFIG_COUNT    16
#define FILENAME_MAX_SIZE       50
#define STEP                    8
#define SIGMA                   200
#define RESCALE_X               2048
#define RESCALE_Y               2048

#define CLAMP(v, min, max) if(v < min) { v = min; } else if(v > max) { v = max; }

// Structure that contains all the information necesarry to a thread while running.
typedef struct {
    int P;
    int id;
    int step_x;
    int step_y;
    char* argv2;
    pthread_barrier_t *barrier;
    ppm_image *image; 
    ppm_image **contour_map;
    ppm_image *scaled_image;
    unsigned char **grid;
} Parameters;

// Structure of a thread that contains the 'array' of Parameters, such that it will be
// able to 'share' data with the other threads.
typedef struct {
    Parameters **parameters;
    int id; // id of the thread.
} Threads;


// Updates a particular section of an image with the corresponding contour pixels.
// Used to create the complete contour image.
void update_image(ppm_image *image, ppm_image *contour, int x, int y) {
    for (int i = 0; i < contour->x; i++) {
        for (int j = 0; j < contour->y; j++) {
            int contour_pixel_index = contour->x * i + j;
            int image_pixel_index = (x + i) * image->y + y + j;

            image->data[image_pixel_index].red = contour->data[contour_pixel_index].red;
            image->data[image_pixel_index].green = contour->data[contour_pixel_index].green;
            image->data[image_pixel_index].blue = contour->data[contour_pixel_index].blue;
        }
    }
}

int min(int a, int b) {
    if (a < b) {
        return a;
    }
    return b;
}

void *paralelize_algorithm(void* arg) {
    Threads *thread = malloc(sizeof(Threads));
    thread = (Threads*)arg;

    int P = thread->parameters[thread->id]->P;

    //Initialize contour map - parallel
    int start = thread->id * (double) CONTOUR_CONFIG_COUNT / P;
    int end = min((thread->id + 1) * (double) CONTOUR_CONFIG_COUNT / P, CONTOUR_CONFIG_COUNT);

    for (int i = start; i < end; i++) {
        char filename[FILENAME_MAX_SIZE];
        sprintf(filename, "./contours/%d.ppm", i);
        thread->parameters[thread->id]->contour_map[i] = read_ppm(filename);
    }

    pthread_barrier_wait(thread->parameters[thread->id]->barrier);


    //RESCALE THE IMAGE
    uint8_t sample[3];

    // Check if the image has a lower resolution than the limit. The check is made in only one thread, in this case, thread 0.
    // If the image size is in limits, I can simply assign to 'scaled_image' the original 'image'.
    // Otherwise, if the image has a bigger resoliton than 2048 x 2048 on each of axes or on both, it will need to be redimensioned.
    // I dynamically alloc memory only once, because the rest of the threads, in their parameters will point to the exact memory zone.
    if (thread->id == 0) {  
        if (thread->parameters[thread->id]->image->x <= RESCALE_X && thread->parameters[thread->id]->image->y <= RESCALE_Y) {
            for (int i = 0; i < P; i++) {
                thread->parameters[i]->scaled_image = thread->parameters[i]->image;
            }
        } else {
            thread->parameters[0]->scaled_image->x = RESCALE_X;
            thread->parameters[0]->scaled_image->y = RESCALE_Y;
            
            thread->parameters[0]->scaled_image->data = (ppm_pixel*)malloc(thread->parameters[0]->scaled_image->x *
                                                             thread->parameters[0]->scaled_image->y * sizeof(ppm_pixel));
            for (int i = 1; i < P; i++) {
                thread->parameters[i]->scaled_image->data = thread->parameters[0]->scaled_image->data;
                thread->parameters[i]->scaled_image->x = thread->parameters[0]->scaled_image->x;
                thread->parameters[i]->scaled_image->y = thread->parameters[0]->scaled_image->y;
            }
        }
    }

    // Make sure all the threads reach this point until the parallel execution continues.
    pthread_barrier_wait(thread->parameters[thread->id]->barrier);

    // If the image needs to be redimensioned, the bicubic interpolation algorithm is applied in parralel, 
    // every thread on its chunck of the image.
    if (thread->parameters[thread->id]->image->x > RESCALE_X || thread->parameters[thread->id]->image->y > RESCALE_Y) {
        start = thread->id * (double) thread->parameters[thread->id]->scaled_image->x / P;
        end = min((thread->id + 1) * (double) thread->parameters[thread->id]->scaled_image->x / P, thread->parameters[thread->id]->scaled_image->x);

        // use bicubic interpolation for scaling
        for (int i = start; i < end; i++) {
            for (int j = 0; j < thread->parameters[0]->scaled_image->y; j++) {
                
                float u = (float)i / (float)(thread->parameters[thread->id]->scaled_image->x - 1);
                float v = (float)j / (float)(thread->parameters[thread->id]->scaled_image->y - 1);

                sample_bicubic(thread->parameters[thread->id]->image, u, v, sample);

                thread->parameters[thread->id]->scaled_image->data[i * thread->parameters[thread->id]->scaled_image->y + j].red = sample[0];
                thread->parameters[thread->id]->scaled_image->data[i * thread->parameters[thread->id]->scaled_image->y + j].green = sample[1];
                thread->parameters[thread->id]->scaled_image->data[i * thread->parameters[thread->id]->scaled_image->y + j].blue = sample[2];
            }
        }
    }

    // Again, make sure all the threads finished their prior eventual bicubic interpolation on a chunck before continuing.
    pthread_barrier_wait(thread->parameters[thread->id]->barrier);

    // SAMPLE THE GRID
    int p = thread->parameters[thread->id]->scaled_image->x / thread->parameters[thread->id]->step_x;
    int q = thread->parameters[thread->id]->scaled_image->y / thread->parameters[thread->id]->step_y;

    // Alloc memory for the whole grid.
    if (thread->id == 0) {
        thread->parameters[0]->grid = (unsigned char **)malloc((p + 1) * sizeof(unsigned char*));

        if (!thread->parameters[0]->grid) {
            fprintf(stderr, "Unable to allocate memory\n");
            exit(1);
        }

        // Share the adress of malloc-ed memory to the other threads.
        for (int i = 1; i < P; i++) {
            thread->parameters[i]->grid = thread->parameters[0]->grid;
        }
    }

    // Synchronize all the threads
    pthread_barrier_wait(thread->parameters[thread->id]->barrier);


    start = (thread->id) * (double) p / P;
    end = min((thread->id + 1) * (double) p / P, p);

    // Allocate memory for the grid lines.
    for (int i = start; i <= end; i++) {
        thread->parameters[thread->id]->grid[i] = (unsigned char *)malloc((q + 1) * sizeof(unsigned char));

        if (!thread->parameters[0]->grid[i]) {
            fprintf(stderr, "Unable to allocate memory\n");
            exit(1);
        }
    }

    pthread_barrier_wait(thread->parameters[thread->id]->barrier);

    // Make the grid binary.
    for (int i = start; i < end; i++) {
        for (int j = 0; j < q; j++) {
            ppm_pixel curr_pixel = thread->parameters[thread->id]->scaled_image->data[i * thread->parameters[thread->id]->step_x 
                                                                                * thread->parameters[thread->id]->scaled_image->y 
                                                                                + j * thread->parameters[thread->id]->step_y];

            unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

            if (curr_color > SIGMA) {
                thread->parameters[thread->id]->grid[i][j] = 0;
            } else {
                thread->parameters[thread->id]->grid[i][j] = 1;
            }
        }
    }
    
    if (thread->id == 0) {
        thread->parameters[thread->id]->grid[p][q] = 0;
    }

    pthread_barrier_wait(thread->parameters[thread->id]->barrier);

    // Doing binary grid on the last row.
    for (int i = start; i < end; i++) {
        ppm_pixel curr_pixel = thread->parameters[thread->id]->scaled_image->data[i * thread->parameters[thread->id]->step_x
                                                                             * thread->parameters[thread->id]->scaled_image->y
                                                                              + thread->parameters[thread->id]->scaled_image->x - 1];

        unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

        if (curr_color > SIGMA) {
            thread->parameters[thread->id]->grid[i][q] = 0;
        } else {
            thread->parameters[thread->id]->grid[i][q] = 1;
        }
    }

    pthread_barrier_wait(thread->parameters[thread->id]->barrier);

    start = (thread->id) * (double) q / P;
    end = min((thread->id + 1) * (double) q / P, q);

    // Doing binary grid on the last column.
    for (int j = start; j < end; j++) {
        ppm_pixel curr_pixel = thread->parameters[thread->id]->scaled_image->data[(thread->parameters[thread->id]->scaled_image->x - 1) *
                                                                             thread->parameters[thread->id]->scaled_image->y 
                                                                             + j * thread->parameters[thread->id]->step_y];

        unsigned char curr_color = (curr_pixel.red + curr_pixel.green + curr_pixel.blue) / 3;

        if (curr_color > SIGMA) {
            thread->parameters[thread->id]->grid[p][j] = 0;
        } else {
            thread->parameters[thread->id]->grid[p][j] = 1;
        }
    }

    pthread_barrier_wait(thread->parameters[thread->id]->barrier);

    //MARCH THE SQUARES
    p = thread->parameters[thread->id]->scaled_image->x / thread->parameters[thread->id]->step_x;
    q = thread->parameters[thread->id]->scaled_image->y / thread->parameters[thread->id]->step_y;

    for (int i = start; i < end; i++) {
        for (int j = 0; j < q; j++) {
            unsigned char k = 8 * thread->parameters[thread->id]->grid[i][j] + 4 * thread->parameters[thread->id]->grid[i][j + 1]
                                 + 2 * thread->parameters[thread->id]->grid[i + 1][j + 1] + 1 * thread->parameters[thread->id]->grid[i + 1][j];
            
            update_image(thread->parameters[thread->id]->scaled_image, thread->parameters[thread->id]->contour_map[k],
                                 i * thread->parameters[thread->id]->step_x, j * thread->parameters[thread->id]->step_y);
        }
    }

    pthread_barrier_wait(thread->parameters[thread->id]->barrier);

    // WRITE OUTPUT
    if (thread->id == 0) {
        write_ppm(thread->parameters[0]->scaled_image, thread->parameters[0]->argv2);
    } 

    pthread_barrier_wait(thread->parameters[thread->id]->barrier);


    // FREE MEMORY 
    start = thread->id * (double) CONTOUR_CONFIG_COUNT / P;
    end = min((thread->id + 1) * (double) CONTOUR_CONFIG_COUNT / P, CONTOUR_CONFIG_COUNT);

    // Free used memory. All parameters ([0, 1, 2, 3]) point to the same 
    // data so I will free() only from thread 0.

    for (int i = start; i < end; i++) {
        free(thread->parameters[0]->contour_map[i]->data);
        free(thread->parameters[0]->contour_map[i]);
    }

    pthread_barrier_wait(thread->parameters[thread->id]->barrier);

    if (thread->id == 0) {
        free(thread->parameters[0]->contour_map);

        start = (thread->id) * (double) p / P;
        end = min((thread->id + 1) * (double) p / P, p);

        for (int i = 0; i <= end; i++) {
            free(thread->parameters[0]->grid[i]);
        }
    
        free(thread->parameters[0]->grid);
        free(thread->parameters[0]->image->data);
        free(thread->parameters[0]->image);
    }


    pthread_exit(NULL);
}


int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: ./tema1 <in_file> <out_file> <P>\n");
        return 1;
    }

    ppm_image *image = read_ppm(argv[1]);
    int step_x = STEP;
    int step_y = STEP;

    int P = atoi(argv[3]);

    pthread_t tid[P];
    int r;
    pthread_barrier_t barrier;

    // Initialize 'contour_map' and 'scaled_image' memory, to which all the threads will be able to point through the parameters array.
    ppm_image **contour_map = (ppm_image **)malloc(CONTOUR_CONFIG_COUNT * sizeof(ppm_image *));
    ppm_image *scaled_image = (ppm_image *)malloc(sizeof(ppm_image));

    pthread_barrier_init(&barrier, NULL, P);

    // Initialize the 'array' of parameters
    Parameters **parameters = malloc(sizeof(Parameters*) * P);

    // Assigning values to the array of parameters. Each 'i' index will corespond to i-th thread. 
    for (int i = 0; i < P; i++) {
        parameters[i] = malloc(sizeof(Parameters));
        parameters[i]->P = P;
        parameters[i]->id = i;
        parameters[i]->argv2 = argv[2];
        parameters[i]->step_x = step_x;
        parameters[i]->step_y = step_y;
        parameters[i]->barrier = &barrier;
        parameters[i]->image = image;
        parameters[i]->contour_map = contour_map;
        parameters[i]->scaled_image = scaled_image;
    }

    // Initialize the threads.
    Threads **threads = malloc(sizeof(Threads*) * P);

    // Assign values to each thread. Every entry in thread 'array' will contain the id of the thread
    // and the array of parameters, so each thread will be able to 'see' other threads variables.
    for (int i = 0; i < P; i++) {
        threads[i] = malloc(sizeof(Threads));
        threads[i]->parameters = parameters;
        threads[i]->id = i;
    }

    // Start each thread's execution.
    for (int i = 0; i < P; i++) {
        r = pthread_create(&tid[i], NULL, paralelize_algorithm, threads[i]);

        if (r) {
            printf("Error while creating thread %d!\n", i);
            exit(-1);
        }
    }

    // Wait for all threads to finish.
    for (int i = 0; i < P; i++) {
        r = pthread_join(tid[i], NULL);

        if (r) {
            printf("Error while waiting for thread %d!\n", i);
            exit(-1);
        }
    }

    // Free the structures used.

    for (int i = 0; i < P; i++) {
        free(parameters[i]);
    }
    free(parameters);

    for (int i = 0; i < P; i++) {
        free(threads[i]);
    }
    free(threads);

    free(scaled_image);


    return 0;
}
