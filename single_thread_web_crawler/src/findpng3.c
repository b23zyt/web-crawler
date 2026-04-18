#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <curl/multi.h>
#include <search.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  9
#define CT_HTML_LEN 9

typedef struct recv_buf2 {
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/   
} RECV_BUF;

typedef struct Node{
    char* url;
    struct Node* next;
} Node;

Node* head = NULL;
Node* tail = NULL;

struct hsearch_data visited;
ENTRY memory_list[10000]; //used to free the items in visited
int curr_pos;
int max_png;
int png_count;
FILE* png_fp;
FILE* all_fp;

int is_png(unsigned char *buf, size_t n) {
    if(n < 8) return -1;
    unsigned char png_sig[8] = {0x89, 0x50, 0x4e, 0x47, 0x0D,0x0A,0x1A,0x0A};
    if(memcmp(buf, png_sig, 8) == 0){
        return 0;
    }else{
        return -1;
    }
}

size_t write_cb(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max((size_t)BUF_INC, realsize + 1);   
        char *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* out of memory */
            return 0;
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

CURL *easy_handle_init(RECV_BUF *ptr, const char *url)
{
    CURL *curl_handle = NULL;

    if ( ptr == NULL || url == NULL) {
        return NULL;
    }

    /* init user defined call back function buffer */
    if ( recv_buf_init(ptr, BUF_SIZE) != 0 ) {
        return NULL;
    }
    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        //fprintf(stderr, "curl_easy_init: returned NULL\n");
        return NULL;
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)ptr);

    /* register header call back function to process received header data */
    //curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    //curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)ptr);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ece252 lab4 crawler");

    //enable redirect
    /* follow HTTP 3XX redirects curl will follow the relocated links. */ 
    // Example: http://ece252-1.uwaterloo.ca/lab4/ relocates to http://ece252-1.uwaterloo.ca/~yqhuang/lab4/
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    /* continue to send authentication credentials when following locations */
    curl_easy_setopt(curl_handle, CURLOPT_UNRESTRICTED_AUTH, 1L);
    /* max numbre of redirects to follow sets to 5 */
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 5L);
    /* supports all built-in encodings */ 
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");

    /* Max time in seconds that the connection phase to the server to take */
    //curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 5L);
    /* Max time in seconds that libcurl transfer operation is allowed to take */
    //curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 10L);
    /* Time out for Expect: 100-continue response in milliseconds */
    //curl_easy_setopt(curl_handle, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);

    /* Enable the cookie engine without reading any initial cookies */
    curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
    /* allow whatever auth the proxy speaks */
    curl_easy_setopt(curl_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    /* allow whatever auth the server speaks */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

    return curl_handle;
}

htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | \
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);
    
    if ( doc == NULL ) {
        //fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath)
{
	
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        //printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        //printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        //printf("No result\n");
        return NULL;
    }
    return result;
}

int find_http(char *buf, int size, int follow_relative_links, const char *base_url)
{

    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;
		
    if (buf == NULL) {
        return 1;
    }

    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset (doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i=0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if ( follow_relative_links ) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *) base_url);
                xmlFree(old);
            }
            if ( href != NULL && !strncmp((const char *)href, "http", 4) ) {
                //printf("All href: %s\n", href); //print all the http in the html (have to push all of those in the queue)
                char *new_url = (char *)href;
                ENTRY item;
                ENTRY *retval;
                item.key = new_url; //it copies to address!
                item.data = NULL;
                int ret = hsearch_r(item, FIND, &retval, &visited);
                if(ret == 0){ //not in the set
                    ENTRY new_item;
                    new_item.key = strdup(new_url);
                    new_item.data = NULL;
                    hsearch_r(new_item, ENTER, &retval, &visited);
                    memory_list[curr_pos++] = new_item;

                    Node* newNode = malloc(sizeof(Node));
                    newNode->url = strdup((char *)href);
                    newNode->next = NULL;

                    if(tail == NULL){
                        head = newNode;
                        tail = newNode;
                    }else{
                        tail->next = newNode;
                        tail = newNode;
                    }
                }
            }
            xmlFree(href);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    return 0;
}


void process_html(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    int follow_relative_link = 1;
    char *url = NULL; 
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, url); //print all the http
}

void process_png(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    char *eurl = NULL;          /* effective URL */
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    //printf("%s\n\n", eurl);
    if ( eurl != NULL) {
        //printf("The PNG url is: %s\n", eurl);
        if(is_png((unsigned char *)p_recv_buf->buf, p_recv_buf->size) == 0){
            char n_eurl[1024];
            sprintf(n_eurl, "%s\n", eurl);
            if(png_count < max_png){
                if(png_count == max_png - 1){
                    fwrite(eurl, strlen(eurl), 1, png_fp);
                }else{
                    fwrite(n_eurl, strlen(n_eurl), 1, png_fp);
                }
                png_count++;
            }
        }
    }

}


/**
 * @brief process teh download data by curl
 * @param CURL *curl_handle is the curl handler
 * @param RECV_BUF p_recv_buf contains the received data. 
 * @return 0 on success; non-zero otherwise
 */

void process_data(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    CURLcode res;
    long response_code;

    res = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code); //get the response code

    if ( response_code >= 400 ) { 
    	//fprintf(stderr, "Error.\n");
        return;
    }

    char *ct = NULL;
    res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct); //get the content type
    if ( res == CURLE_OK && ct != NULL ) {
    	//printf("Content-Type: %s, len=%ld\n", ct, strlen(ct));
    } else {
        //fprintf(stderr, "Failed obtain Content-Type\n");
        return;
    }

    if ( strstr(ct, CT_HTML) ) {
        process_html(curl_handle, p_recv_buf); // another web
    } else if ( strstr(ct, CT_PNG) ) {
        process_png(curl_handle, p_recv_buf); //include disguise images
    }
}


int main(int argc, char** argv){
    if(argc <= 2){
        fprintf(stderr, "Usage: %s <directory name>\n", argv[0]);
        exit(1);
    }
    int c;
    int t = 1; //number of concurrent connections
    int m = 50; //number of img need to find (default = 50)
    char *log = NULL; //log file (default none)

    char *str = "option requires an argument";
    char* first_url;
    
    while ((c = getopt (argc, argv, "t:m:v:")) != -1) {
        switch (c) {
            case 't':
                t = strtoul(optarg, NULL, 10);
                if (t <= 0) {
                        fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
                        return -1;
                    }
                break;
            case 'm':
                m = strtoul(optarg, NULL, 10);
                if (m <= 0) {
                    fprintf(stderr, "%s: %s > 0 -- 'm'\n", argv[0], str);
                    return -1;
                }
                break;
            case 'v' :
                log = optarg;
                break;
            default:
                return -1;
        }
    }
    if (optind >= argc) {
        fprintf(stderr, "Usage: %s <directory name>\n", argv[0]);
        exit(1);
    }else{
        first_url = argv[optind];
    }
    //printf("%s: URL is %s\n", argv[0], first_url);
    //printf("t: %d, m: %d, log: %s\n", t, m, log);
    max_png = m;
    png_count = 0;
    int max_conn = t;
    int curr_conn = 0;
    int running = 0;
    curr_pos = 0;
    hcreate_r(10000, &visited);

    Node *newNode = malloc(sizeof(Node));
    newNode->url = strdup(first_url);
    newNode->next = NULL;
    head = newNode;
    tail = newNode;

    ENTRY item;
    ENTRY *retval;
    item.key = strdup((char *)first_url);
    item.data = NULL;
    memory_list[curr_pos++] = item;
    hsearch_r(item, ENTER, &retval, &visited);
    
    png_fp = fopen("png_urls.txt", "w");
    all_fp = NULL;
    if(log != NULL){
        all_fp = fopen(log, "w");
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURLM *cm = curl_multi_init();

    double times[2];
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;

    //crawler begins
    while(head != NULL && png_count <= max_png){
        while(curr_conn < max_conn && head != NULL){
            Node* temp = head;
            char* url = temp->url;
            head = head->next;
            if(head == NULL)
                tail = NULL;
            if(all_fp != NULL){
                char n_url[1024];
                sprintf(n_url, "%s\n", url);
                fwrite(n_url, strlen(n_url), 1, all_fp);
            }
            RECV_BUF* recv_buf = malloc(sizeof(RECV_BUF));
            CURL *eh = easy_handle_init(recv_buf, url);
            curl_easy_setopt(eh, CURLOPT_PRIVATE, recv_buf);
            curl_multi_add_handle(cm, eh);
            curr_conn++;
            free(temp->url);
            free(temp);
        }

        curl_multi_perform(cm, &running);

        do{
            const int timeout = 1000; //1s
            int nFd = 0;
            curl_multi_wait(cm, NULL, 0, timeout, &nFd);
            curl_multi_perform(cm, &running);
            CURLMsg *msg = NULL;
            int msgLeft = 0;
            while((msg = curl_multi_info_read(cm, &msgLeft)) != NULL){
                assert(msg->msg == CURLMSG_DONE);
                CURL *eh = msg->easy_handle;
                RECV_BUF* done_buf = NULL;
                curl_easy_getinfo(eh, CURLINFO_PRIVATE, &done_buf);
                if(msg->data.result != CURLE_OK){
                    //printf("UNKNOWN\n");
                    //printf("Request Failed\n");
                }else{
                    process_data(eh, done_buf);
                }
                recv_buf_cleanup(done_buf);
                free(done_buf);
                curl_multi_remove_handle(cm, eh);
                curl_easy_cleanup(eh);
                curr_conn--;
            }
        }while (running > 0);
        if (head == NULL && curr_conn == 0) break;
        if (png_count >= max_png) break;
    }

    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    printf("findpng3 execution time: %.2f seconds\n", times[1] - times[0]);

    while(head != NULL){
        Node* temp = head;
        head = head->next;
        free(temp->url);
        free(temp);
    }
    if(all_fp != NULL){
        fclose(all_fp);
    }
    fclose(png_fp);
    
    for(int i = 0; i < curr_pos; i++){
        free(memory_list[i].key);
    }
    hdestroy_r(&visited);
    xmlCleanupParser();

    curl_multi_cleanup(cm);
    curl_global_cleanup();
    return 0;
}



