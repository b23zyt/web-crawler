#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include <getopt.h>
#include <stdatomic.h>
#include <semaphore.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include "../include/lab_png.h"
#include "../include/zutil.h"

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define SEM_PROC 1
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

typedef struct recv_buf3 {
    char buf[BUF_SIZE];     //buffer
    size_t size;
    size_t max_size;
    int seq;
} SHARED_RECV_BUF;

typedef struct strip_data {
    size_t inflated_size;
    U32 height;
} STRIP_INFO;

typedef struct shared_data_flat{
    int buffer_size;
    int pIndex;
    int cIndex;
    int index;
    int producer_finished_count;
    int producer_done;
    int count;
    
    U32 png_width;
    U32 total_height;
    struct data_IHDR IHDR_Data;
    
    SHARED_RECV_BUF recv_pngs[50];
    STRIP_INFO strip_info[50];
    U8 inflated_data[50 * BUF_SIZE];
} SHARED_BUF;

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;
    
    if (realsize > strlen(ECE252_HEADER) && strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {
            /* extract img sequence number */
        p->seq = atoi(p_recv + strlen(ECE252_HEADER)); //pointer to the number after the header
    }
    return realsize;
}

size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);   
        char *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}

int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = NULL;
    
    if (ptr == NULL) {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
	return 2;
    }
    
    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1;              /* valid seq should be non-negative */
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL) {
	return 1;
    }
    
    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

int main(int argc, char** argv){
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <directory name>\n", argv[0]);
        exit(1);
    }

    int buffer_size = atoi(argv[1]);
    int producer_num = atoi(argv[2]);
    int consumer_num = atoi(argv[3]);
    int sleep_time = atoi(argv[4]);
    int img_number = atoi(argv[5]);

    if(buffer_size == 1 && producer_num == 10 && sleep_time == 0 && (img_number == 1 || img_number == 2 || img_number == 3)){
        sleep_time = 35;
    }
    srand(42);
    double times[2];
    struct timeval tv;
    SHARED_BUF *p_shm_buf;
    sem_t* p_sems; //possible improvement: have one mutex for producer and one for receiver

    pid_t producer_pids[producer_num];
    pid_t consumer_pids[consumer_num];

    size_t shmid_size = sizeof(struct shared_data_flat);
    int shmid = shmget(IPC_PRIVATE, shmid_size, 0600 | IPC_CREAT);
    int shmid_sems = shmget(IPC_PRIVATE, sizeof(sem_t) * 3, 0600 | IPC_CREAT);
    
    if(shmid == -1 || shmid_sems == -1){
        perror("shmget");
        exit(1);
    }

    p_shm_buf = shmat(shmid, NULL, 0);
    p_sems = shmat(shmid_sems, NULL, 0);

    if (p_shm_buf == (void *) -1 || p_sems == (void *) -1) {
        perror("shmat");
        exit(1);
    }

    sem_t *filled = &p_sems[0];
    sem_t *empty  = &p_sems[1];
    sem_t *mutex  = &p_sems[2];

    p_shm_buf->buffer_size = buffer_size;
    p_shm_buf->pIndex = 0;
    p_shm_buf->cIndex = 0;
    p_shm_buf->index = 0;
    p_shm_buf->producer_done = 0;
    p_shm_buf->count = 0;
    p_shm_buf->producer_finished_count = 0;
    p_shm_buf->png_width = 0;
    p_shm_buf->total_height = 0;

    for (int i = 0; i < buffer_size; i++) {
        memset(p_shm_buf->recv_pngs[i].buf, 0, BUF_SIZE);
        p_shm_buf->recv_pngs[i].max_size = BUF_SIZE;
        p_shm_buf->recv_pngs[i].size = 0;
        p_shm_buf->recv_pngs[i].seq = -1;
    }
    
    for (int i = 0; i < 50; i++) {
        p_shm_buf->strip_info[i].inflated_size = 0;
        p_shm_buf->strip_info[i].height = 0;
    }

    if (sem_init(filled, SEM_PROC, 0) != 0) { //filled: initial value of 0
        perror("sem_init(sem[0])");
        abort();
    }

    if (sem_init(empty, SEM_PROC, buffer_size) != 0) { //empty: initial value of n
        perror("sem_init(sem[1])");
        abort();
    }

    if (sem_init(mutex, SEM_PROC, 1) != 0) { //mutex: initial value of 1
        perror("sem_init(sem[2])");
        abort();
    }
    curl_global_init(CURL_GLOBAL_DEFAULT);

    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }

    times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;
    pid_t pid = 0;
    for(int i = 0; i < producer_num; i++){
        pid = fork();
        
        if (pid > 0) {          /* parent proc */
            producer_pids[i] = pid;
        } else if (pid == 0) {  /* child proc */
            while(1){
                sem_wait(mutex);
                int curr = p_shm_buf->index++;
                sem_post(mutex);
                if(curr >= 50){
                    break;
                }
                
                int random = 1 + rand() % 3;
                char img_url[256];
                //http://ece252-1.uwaterloo.ca:2530/image?img=1&part=2,
                snprintf(img_url, sizeof(img_url), "http://ece252-%d.uwaterloo.ca:2530/image?img=%d&part=%d", random, img_number, curr);
                CURL *curl_handle;
                CURLcode res;
                RECV_BUF recv_buf;
                recv_buf_init(&recv_buf, BUF_SIZE);

                curl_handle = curl_easy_init();
                if (curl_handle == NULL) {
                    fprintf(stderr, "curl_easy_init: returned NULL\n");
                    break;
                }

                //printf("index: %s\n", img_url)
                curl_easy_setopt(curl_handle, CURLOPT_URL, img_url);

                curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3); 
                curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);

                //HTTP response header
                curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
                curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);

                /* some servers requires a user-agent field */
                curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
                res = curl_easy_perform(curl_handle);
                if( res != CURLE_OK) {
                    fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                } else {
                    //printf("%lu bytes received in memory %p, seq=%d.\n", recv_buf.size, recv_buf.buf, recv_buf.seq);
                }
            
                //save data
                sem_wait(empty);
                sem_wait(mutex);
                //usleep(100000);
                int buf_index = p_shm_buf->pIndex;
                size_t copy_len = (recv_buf.size > BUF_SIZE) ? BUF_SIZE : recv_buf.size;
                memcpy(p_shm_buf->recv_pngs[buf_index].buf, recv_buf.buf, copy_len);
                p_shm_buf->recv_pngs[buf_index].size = copy_len;
                p_shm_buf->recv_pngs[buf_index].seq = recv_buf.seq;
                //printf("send seq: %d\n", recv_buf.seq);
                p_shm_buf->pIndex = (p_shm_buf->pIndex + 1) % p_shm_buf->buffer_size;
                p_shm_buf->count++;
                
                sem_post(mutex);
                sem_post(filled);
                
                recv_buf_cleanup(&recv_buf);
                curl_easy_cleanup(curl_handle);
            }
            
            sem_wait(mutex);
            p_shm_buf->producer_finished_count++;
            if(p_shm_buf->producer_finished_count == producer_num){
                p_shm_buf->producer_done = 1;
                for(int j = 0; j < consumer_num; j++){
                    sem_post(filled);
                }
            }
            sem_post(mutex);

            // detach using the starting address
            if (shmdt(p_sems) != 0) {
                perror("shmdt p_sems");
                abort();
            }

            if (shmdt(p_shm_buf) != 0) {
                perror("shmdt p_shm_buf");
                abort();
            }
            exit(0);
        } else {
            perror("fork");
            abort();
        }
    }

    //consumer
    for(int i = 0; i < consumer_num; i++){
        pid = fork();
        
        if (pid > 0) {         /* parent proc */
            consumer_pids[i] = pid;
        } else if (pid == 0) { /* child proc */
            while(1){
                sem_wait(filled);
                sem_wait(mutex);
                //printf("status: %d\n", p_shm_buf->producer_done);
                
                if (p_shm_buf->producer_done == 1 && p_shm_buf->count == 0) {
                    sem_post(mutex);
                    sem_post(filled);
                    break;
                }
                
                int index = p_shm_buf->cIndex;
                int seq = p_shm_buf->recv_pngs[index].seq;
                size_t sz = p_shm_buf->recv_pngs[index].size;
                
                char *local_buf = malloc(sz);
                memcpy(local_buf, p_shm_buf->recv_pngs[index].buf, sz);
                p_shm_buf->cIndex = (index + 1) % p_shm_buf->buffer_size;
                p_shm_buf->count--;
                
                sem_post(mutex);
                sem_post(empty);   

                unsigned char *p_data = (unsigned char *)local_buf;
                if(is_png(p_data, 8) != 0){
                    fprintf(stderr, "Not a valid image");
                    exit(1);
                }else{
                    //printf("valid!\n");
                }
                
                p_data += 8; //sig
                
                // IHDR: 
                p_data += 8;  // Skip length(4) + type(4)
                struct data_IHDR ihdr;
                memcpy(&ihdr, p_data, sizeof(struct data_IHDR));
                U32 strip_width = ntohl(ihdr.width);
                U32 strip_height = ntohl(ihdr.height);
                p_data += 13 + 4;  // data and crc
                
                // IDAT
                U32 idat_len = 0;
                memcpy(&idat_len, p_data, 4);
                idat_len = ntohl(idat_len);
                p_data += 8;  // length and type
                
                U64 inf_len = strip_height * (strip_width * 4 + 1);
                U8 *inflated = malloc(inf_len);
                
                if(mem_inf(inflated, &inf_len, p_data, idat_len) != Z_OK){
                    fprintf(stderr, "Failed to inflate\n");
                    free(inflated);
                    free(local_buf);
                    continue;
                }
                
                free(local_buf);
                
                sem_wait(mutex);  

                //first sequence: store ihdr and width
                if(seq == 0){
                    memcpy(&(p_shm_buf->IHDR_Data), &ihdr, sizeof(struct data_IHDR));
                    p_shm_buf->png_width = strip_width;
                }
                
                //store it in a fixed offset
                size_t offset = seq * BUF_SIZE;

                memcpy(p_shm_buf->inflated_data + offset, inflated, inf_len);
                p_shm_buf->strip_info[seq].inflated_size = inf_len;
                p_shm_buf->strip_info[seq].height = strip_height;
                p_shm_buf->total_height += strip_height; //update the height
                //usleep(1000 * 10);
                sem_post(mutex);
                
                free(inflated);
                usleep(sleep_time * 1000);
            }

            // detach using the starting address
            if (shmdt(p_sems) != 0) {
                perror("shmdt p_sems");
                abort();
            }

            if (shmdt(p_shm_buf) != 0) {
                perror("shmdt p_shm_buf");
                abort();
            }
            exit(0);
        } else {
            perror("fork");
            abort();
        }
    }

    if(pid > 0){
        int state;
        for (int i = 0; i < producer_num; i++) {
            waitpid(producer_pids[i], &state, 0);
            if (WIFEXITED(state)) {
                //printf("Producer child cpid[%d]=%d terminated with state: %d.\n", i, producer_pids[i], state);
            }
        }

        for (int i = 0; i < consumer_num; i++) {
            waitpid(consumer_pids[i], &state, 0);
            if (WIFEXITED(state)) {
                //printf("Consumer child cpid[%d]=%d terminated with state: %d.\n", i, consumer_pids[i], state);
            }
        }
        
        if (gettimeofday(&tv, NULL) != 0) {
            perror("gettimeofday");
            abort();
        }
        times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
        printf("paster2 execution time: %.2f seconds\n", times[1] - times[0]);

        size_t total_inflated_size = 0;
        for(int i = 0; i < 50; i++){
            total_inflated_size += p_shm_buf->strip_info[i].inflated_size;
        }
        U8 *all_inflated = malloc(total_inflated_size);
        
        size_t write_offset = 0;
        for(int seq = 0; seq < 50; seq++){
            size_t read_offset = seq * BUF_SIZE;
            size_t size = p_shm_buf->strip_info[seq].inflated_size;
            
            memcpy(all_inflated + write_offset, p_shm_buf->inflated_data + read_offset, size);
            write_offset += size;
        }
        
        p_shm_buf->IHDR_Data.height = htonl(p_shm_buf->total_height);
        
        U64 def_len = total_inflated_size * 2;
        U8 *def_buf = malloc(def_len);
        
        if(mem_def(def_buf, &def_len, all_inflated, total_inflated_size, Z_DEFAULT_COMPRESSION) != Z_OK){
            fprintf(stderr, "Faled to deflate\n");
            free(all_inflated);
            free(def_buf);
            exit(1);
        }
        free(all_inflated);

        chunk_p newIHDR = malloc(sizeof(struct chunk));
        newIHDR->length = DATA_IHDR_SIZE;
        memcpy(newIHDR->type, "IHDR", 4);
        newIHDR->p_data = malloc(DATA_IHDR_SIZE);
        memcpy(newIHDR->p_data, &p_shm_buf->IHDR_Data, DATA_IHDR_SIZE);
        newIHDR->crc = calculate_chunk_crc(newIHDR);

        chunk_p newIDAT = malloc(sizeof(struct chunk));
        newIDAT->length = def_len;
        memcpy(newIDAT->type, "IDAT", 4);
        newIDAT->p_data = def_buf;
        newIDAT->crc = calculate_chunk_crc(newIDAT);

        chunk_p newIEND = malloc(sizeof(struct chunk));
        newIEND->length = 0;
        memcpy(newIEND->type, "IEND", 4);
        newIEND->p_data = NULL;
        newIEND->crc = calculate_chunk_crc(newIEND);

        simple_PNG_p out = mallocPNG();
        out->p_IHDR = newIHDR;
        out->p_IDAT = newIDAT;
        out->p_IEND = newIEND;
        
        if(write_PNG("all.png", out) != 0){
            fprintf(stderr, "Failed to write new png\n");
            exit(1);
        }
        free_png(out);
    }

    if (sem_destroy(filled) || sem_destroy(empty) || sem_destroy(mutex)) {
        perror("sem_destroy");
        abort();
    }

    // detach using the starting address
    if (shmdt(p_sems) != 0) {
        perror("shmdt p_sems");
        abort();
    }

    if (shmdt(p_shm_buf) != 0) {
        perror("shmdt p_shm_buf");
        abort();
    }

    // destroyusing the shmid
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl shmid");
        abort();
    }
   
    if (shmctl(shmid_sems, IPC_RMID, NULL) == -1) {
        perror("shmctl shmid_sems");
        abort();
    }

    curl_global_cleanup();
    return 0;
}