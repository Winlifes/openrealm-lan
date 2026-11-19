/* Parse supplied MDX files with the production reader, without a GL context.
 * Paths are read from stdin, one per line. No retail asset is a test fixture. */
#define GL_SILENCE_DEPRECATION 1
#include "renderer/r_local.h"
#include "games/warcraft-3/renderer/mdx/r_mdx.h"
#include <stdio.h>
#include <stdlib.h>

refImport_t ri;
typedef struct allocation_s { struct allocation_s *next; void *data; } allocation_t;
static allocation_t *allocations;
static handle_t audit_alloc(long size) {
    if (size<0 || size>(1L<<29)) {fprintf(stderr,"invalid allocation: %ld\n",size);exit(2);}
    allocation_t *a=malloc(sizeof(*a));a->data=calloc(1,size?size:1);a->next=allocations;allocations=a;return a->data;
}
static void audit_free(handle_t p) {(void)p;}
#include "games/warcraft-3/renderer/mdx/r_mdx_load.c"

int main(void) {
    char path[4096];int failures=0,files=0;
    ri.MemAlloc=audit_alloc;ri.MemFree=audit_free;
    while(fgets(path,sizeof(path),stdin)) {
        path[strcspn(path,"\r\n")]=0;FILE *f=fopen(path,"rb");if(!f){perror(path);return 2;}
        fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);void *data=malloc(size);
        if(fread(data,1,size,f)!=(size_t)size){fclose(f);free(data);return 2;}fclose(f);
        mdxModel_t *model=audit_alloc(sizeof(*model));
        sizeBuf_t buffer={.data=data,.cursize=size,.readcount=4};s_mdx_file_version=800;
        bool ok=size>=4&&!memcmp(data,"MDLX",4)&&MSG_ReadBlock(&buffer,R_MDLX,model)==BLOCKREAD_OK;
        files++;if(!ok){failures++;printf("FAIL %s offset=%u/%ld\n",path,buffer.readcount,size);}
        else if(strstr(path,"mainmenu3d_exp")) {
            printf("MENU %s version=%u sequences=%d\n",path,model->version,model->num_sequences);
            FOR_LOOP(i,model->num_sequences)printf(" SEQ %s %u..%u\n",model->sequences[i].name,model->sequences[i].interval[0],model->sequences[i].interval[1]);
        }
        free(data);while(allocations){allocation_t *a=allocations;allocations=a->next;free(a->data);free(a);}
    }
    printf("MDX audit: %d files, %d failures\n",files,failures);return !files?2:failures?1:0;
}
