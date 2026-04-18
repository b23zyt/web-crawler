#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <arpa/inet.h>

#include "../include/lab_png.h"
#include "../include/crc.h"

int is_png(U8 *buf, size_t n) {
    if(n < 8) return -1;
    unsigned char png_sig[8] = {0x89, 0x50, 0x4e, 0x47, 0x0D,0x0A,0x1A,0x0A};
    if(memcmp(buf, png_sig, 8) == 0){
        return 0;
    }else{
        return -1;
    }
}

//input is the pointer to the start of the IHDR chunk
int get_png_data_IHDR(struct data_IHDR *out, FILE *fp, long offset, int whence){
    if(fseek(fp, offset, whence) != 0){ 
        return -1;
    }
    U32 length; //length
    if(fread(&length, 4, 1, fp) != 1) return -1;
    length = ntohl(length);

    U8 type[5] = {0}; //type
    if(fread(type, 1, 4, fp) != 4) return -1;
    type[4] = '\0';

    if (length != DATA_IHDR_SIZE || strcmp((const char *)type, "IHDR") != 0) {
        return -1;
    }

    U8 data[DATA_IHDR_SIZE] = {0};
    if(fread(data, 1, length, fp) != length) return -1;

    U32 temp;
    memcpy(&temp, data, 4);
    out->width = ntohl(temp);

    memcpy(&temp, data+4, 4);
    out->height = ntohl(temp);
    out->bit_depth   = data[8];
    out->color_type  = data[9];
    out->compression = data[10];
    out->filter      = data[11];
    out->interlace   = data[12];
    //leave the crc

    return 0;
}

int get_png_height(struct data_IHDR *buf){
    return buf->height;
}

int get_png_width(struct data_IHDR *buf){
    return buf->width;
}

//input the start of the IHDR chunk 8 offset of SEEK_SET
int get_png_chunks(simple_PNG_p out, FILE* fp, long offset, int whence){
    if(fseek(fp, offset, whence) != 0){ 
        return -1;
    }

    while (1) {
        chunk_p ch = get_chunk(fp);
        if (ch == NULL) {
            return -1;
        }

        if (memcmp(ch->type, "IHDR", 4) == 0) {
            out->p_IHDR = ch;
        } else if (memcmp(ch->type, "IDAT", 4) == 0) {
            out->p_IDAT = ch;
        } else if (memcmp(ch->type, "IEND", 4) == 0) {
            out->p_IEND = ch;
            break;
        } else {
            free_chunk(ch);
        }
    }
    return 0;
}

//fp is at the starter of the chunk
chunk_p get_chunk(FILE *fp){
    U32 length;
    if (fread(&length, 4, 1, fp) != 1) return NULL;
    length = ntohl(length);

    U8 type[5] = {0};
    if (fread(type, 1, 4, fp) != 4) return NULL;

    U8 *data = malloc(length);
    if (!data) return NULL;
    if (fread(data, 1, length, fp) != length) {
        free(data);
        return NULL;
    }

    U32 crc;
    if (fread(&crc, 4, 1, fp) != 1) {
        free(data);
        return NULL;
    }
    crc = ntohl(crc);

    struct chunk *ch = malloc(sizeof(struct chunk));
    if (!ch) {
        free(data);
        return NULL;
    }
    
    ch->length = length;
    memcpy(ch->type, type, 4);
    ch->p_data = data;
    ch->crc = crc;

    return ch;
}

U32 get_chunk_crc(chunk_p in){
    return in->crc;
}

U32 calculate_chunk_crc(chunk_p in){
    U32 length = in->length;
    U8* info = malloc(length + 4);
    memcpy(info, in->type, 4);
    memcpy(info + 4, in->p_data, length);
    U32 result = (U32)crc(info, length + 4);
    free(info);
    return result;
}

simple_PNG_p mallocPNG(){
    simple_PNG_p png = malloc(sizeof(struct simple_PNG));
    if(!png) return NULL;
    png->p_IHDR = NULL;
    png->p_IDAT = NULL;
    png->p_IEND = NULL;
    return png;
}

void free_png( simple_PNG_p in){
    free_chunk(in->p_IHDR);
    free_chunk(in->p_IDAT);
    free_chunk(in->p_IEND);
    free(in);
}

void free_chunk(chunk_p in){
    if(!in) return;
    free(in->p_data);
    free(in);
}

int write_PNG(char* filepath, simple_PNG_p in){
    if(!filepath || !in) return -1;

    FILE* fp = fopen(filepath, "w");
    if(!fp) return -1;
    unsigned char png_sig[8] = {0x89, 0x50, 0x4e, 0x47, 0x0D,0x0A,0x1A,0x0A};
    if(fwrite(png_sig, 1, 8, fp) != 8){
        fclose(fp);
        return -1;
    }

    if(in->p_IHDR == NULL || write_chunk(fp, in->p_IHDR) != 0){
        fclose(fp);
        return -1;
    }
    
    if(in->p_IDAT == NULL || write_chunk(fp, in->p_IDAT) != 0){
        fclose(fp);
        return -1;
    }

    if(in->p_IEND == NULL || write_chunk(fp, in->p_IEND) != 0){
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

int write_chunk(FILE* fp, chunk_p in){
    if(!fp || !in) return -1;
    //length
    U32 length = htonl(in->length);
    if(fwrite(&length, 4, 1, fp) != 1)  return -1;
    length = ntohl(length);

    U8 type[4];
    memcpy(type, in->type, 4);
    if(fwrite(type, 1, 4, fp) != 4) return -1;

    if(length > 0){
        if(fwrite(in->p_data, 1, length, fp) != length) return -1;
    }

    U32 crc = htonl(in->crc);
    if(fwrite(&crc, 4, 1, fp) != 1) return -1;
    
    return 0;
}
  
