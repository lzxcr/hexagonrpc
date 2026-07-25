#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../hexagonrpcd/hexagonfs.h"
#include "../hexagonrpcd/iobuffer.h"
#include "../hexagonrpcd/rpcd_builder.h"

static int total=0, passed=0;
static void T(const char *n, int c) {
    total++;
    if(c){passed++;printf("  \033[32mOK\033[0m  %s\n",n);}
    else {printf("  \033[31mFAIL\033[0m %s\n",n);}
}

int main(int argc, char **argv) {
    const char *root = "/tmp/hex_test";
    int do_write = 0;
    for (int i=1; i<argc; i++) {
        if (!strcmp(argv[i],"--write")) do_write=1;
        else root=argv[i];
    }

    printf("HexagonRPC DSP Simulation Test\nRoot: %s\n\n", root);

    /* Setup: mkdir -p all at once, then create files */
    char cmd[1024];
    snprintf(cmd,sizeof(cmd),"mkdir -p %s/acdb %s/dsp/adsp %s/sensors/config %s/sensors/registry %s/socinfo 2>/dev/null",root,root,root,root,root);
    system(cmd);

    int fd; char p[512], buf[256];
    snprintf(p,sizeof(p),"%s/acdb/test.bin",root);
    fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0){write(fd,"hello_acdb_data",15);close(fd);}
    snprintf(p,sizeof(p),"%s/dsp/adsp/libtest_skel.so",root);
    fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0){write(fd,"SKEL_DATA_FOR_LOAD",18);close(fd);}
    snprintf(p,sizeof(p),"%s/dsp/adsp/libtest2.so",root);
    fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0){write(fd,"ABCDEFGHIJKLMNOPQRSTUVWXYZ",26);close(fd);}
    snprintf(p,sizeof(p),"%s/sensors/config/test.json",root);
    fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0){write(fd,"{sns:1}",7);close(fd);}
    snprintf(p,sizeof(p),"%s/sensors/registry/test.reg",root);
    fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0){write(fd,"REG_1234",8);close(fd);}
    snprintf(p,sizeof(p),"%s/sensors/sns_reg.conf",root);
    fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0){write(fd,"#CFG",4);close(fd);}
    snprintf(p,sizeof(p),"%s/socinfo/machine",root);
    fd=open(p,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0){write(fd,"TEST_SOC",8);close(fd);}

    /* Build HexagonFS */
    struct hexagonfs_fd *fds[256]={0};
    struct hexagonfs_dirent *rt=construct_root_dir_with_prefix(root,"adsp");
    int rf=hexagonfs_open_root(fds,rt);
    T("open HexagonFS root",rf>=0);
    if(rf<0){fprintf(stderr,"FATAL\n");return 1;}

    struct stat st; int r; off_t pos; ssize_t n;
    char nm[256];

    /* Phase 1: Skel library loading */
    printf("\n\033[1;34m--- Phase 1: DSP loads skel libraries ---\033[0m\n");

    fd=hexagonfs_openat(fds,rf,rf,"/usr/lib/qcom/adsp/libtest_skel.so");
    T("1a open skel lib",fd>=0);
    if(fd>=0){n=hexagonfs_read(fds,fd,sizeof(buf),buf);T("1a read 18",n==18);
        T("1a content",memcmp(buf,"SKEL_DATA_FOR_LOAD",18)==0);hexagonfs_close(fds,fd);}

    fd=hexagonfs_openat(fds,rf,rf,"/usr/lib/qcom/adsp/libtest_skel.so");
    if(fd>=0){T("1b stat size=18",hexagonfs_fstat(fds,fd,&st)==0&&st.st_size==18);hexagonfs_close(fds,fd);}

    fd=hexagonfs_openat(fds,rf,rf,"/usr/lib/qcom/adsp/libtest2.so");
    T("1c open libtest2",fd>=0);if(fd>=0)hexagonfs_close(fds,fd);

    /* Phase 2: ACDB data */
    printf("\n\033[1;34m--- Phase 2: ACDB calibration ---\033[0m\n");

    fd=hexagonfs_openat(fds,rf,rf,"/vendor/etc/acdbdata/test.bin");
    T("2a open acdb",fd>=0);
    if(fd>=0){n=hexagonfs_read(fds,fd,sizeof(buf),buf);T("2a content",memcmp(buf,"hello_acdb_data",15)==0);hexagonfs_close(fds,fd);}

    fd=hexagonfs_openat(fds,rf,rf,"/system/vendor/etc/acdbdata/test.bin");
    T("2b hardlink /system/vendor",fd>=0);
    if(fd>=0){hexagonfs_read(fds,fd,sizeof(buf),buf);T("2b same",memcmp(buf,"hello_acdb_data",15)==0);hexagonfs_close(fds,fd);}

    /* Phase 3: Sensor config */
    printf("\n\033[1;34m--- Phase 3: Sensor configuration ---\033[0m\n");

    fd=hexagonfs_openat(fds,rf,rf,"/vendor/etc/sensors/config/test.json");
    T("3a open sensor cfg",fd>=0);
    if(fd>=0){hexagonfs_read(fds,fd,sizeof(buf),buf);T("3a content",strstr(buf,"sns:1")!=NULL);hexagonfs_close(fds,fd);}

    fd=hexagonfs_openat(fds,rf,rf,"/vendor/etc/sensors/sns_reg_config");
    T("3b sns_reg_config",fd>=0);if(fd>=0)hexagonfs_close(fds,fd);

    fd=hexagonfs_openat(fds,rf,rf,"/persist/sensors/registry/registry/test.reg");
    T("3c persist reg",fd>=0);
    if(fd>=0){hexagonfs_read(fds,fd,sizeof(buf),buf);T("3c content",memcmp(buf,"REG_1234",8)==0);hexagonfs_close(fds,fd);}

    fd=hexagonfs_openat(fds,rf,rf,"/mnt/vendor/persist/sensors/registry/registry/test.reg");
    T("3d mnt hardlink",fd>=0);if(fd>=0)hexagonfs_close(fds,fd);

    /* Phase 4: SoC info */
    printf("\n\033[1;34m--- Phase 4: SoC info ---\033[0m\n");

    fd=hexagonfs_openat(fds,rf,rf,"/sys/devices/soc0/machine");
    T("4a socinfo",fd>=0);
    if(fd>=0){hexagonfs_read(fds,fd,sizeof(buf),buf);T("4a content",strstr(buf,"TEST_SOC")!=NULL);hexagonfs_close(fds,fd);}

    /* Phase 5: Seek / tell */
    printf("\n\033[1;34m--- Phase 5: Seek / tell / stat ---\033[0m\n");

    fd=hexagonfs_openat(fds,rf,rf,"/usr/lib/qcom/adsp/libtest2.so");
    T("5a open 26-byte",fd>=0);
    if(fd>=0){
        T("5a stat=26",hexagonfs_fstat(fds,fd,&st)==0&&st.st_size==26);
        hexagonfs_read(fds,fd,5,buf);buf[5]=0;
        T("5b read ABCDE",strcmp(buf,"ABCDE")==0);

        hexagonfs_lseek(fds,fd,0,SEEK_SET);
        hexagonfs_read(fds,fd,5,buf);
        T("5c re-read ABCDE",strcmp(buf,"ABCDE")==0);

        hexagonfs_lseek(fds,fd,10,SEEK_CUR);
        hexagonfs_read(fds,fd,5,buf);
        T("5d seek+10 reads PQRST",strcmp(buf,"PQRST")==0);

        hexagonfs_lseek(fds,fd,0,SEEK_END);
        hexagonfs_read(fds,fd,1,buf);
        T("5e seek END → EOF(read=0)",hexagonfs_read(fds,fd,1,buf)==0);

        hexagonfs_lseek(fds,fd,0,SEEK_SET);
        hexagonfs_read(fds,fd,3,buf);buf[3]=0;
        T("5f seek SET→ABC",strcmp(buf,"ABC")==0);
        hexagonfs_close(fds,fd);
    }

    /* Phase 6: Directory ops */
    printf("\n\033[1;34m--- Phase 6: Directories ---\033[0m\n");

    fd=hexagonfs_openat(fds,rf,rf,"/vendor/etc/acdbdata/");
    T("6a opendir acdb",fd>=0);
    if(fd>=0){int c=0;while(1){r=hexagonfs_readdir(fds,fd,sizeof(nm),nm);if(r||nm[0]==0)break;c++;}T("6a readdir entries>=1",c>=1);hexagonfs_close(fds,fd);}

    fd=hexagonfs_openat(fds,rf,rf,"/usr/lib/qcom/adsp/");
    T("6b opendir adsp",fd>=0);
    if(fd>=0){int c=0;bool fl=false;while(1){r=hexagonfs_readdir(fds,fd,sizeof(nm),nm);if(r||nm[0]==0)break;if(strstr(nm,"libtest"))fl=true;c++;}T("6b entries>=2",c>=2);T("6b found libtest",fl);hexagonfs_close(fds,fd);}

    /* Phase 7: Error paths */
    printf("\n\033[1;34m--- Phase 7: Error paths ---\033[0m\n");

    fd=hexagonfs_openat(fds,rf,rf,"/vendor/etc/acdbdata/NOEXIST.bin");
    T("7a missing→ENOENT",fd<0);
    fd=hexagonfs_openat(fds,rf,rf,"/virt/never/defined");
    T("7b bad vpath",fd<0);
    T("7c bad fd read",hexagonfs_read(fds,999,1,NULL)<0);
    T("7d bad fd write",hexagonfs_write(fds,999,1,NULL)<0);
    T("7e bad fd close",hexagonfs_close(fds,999)<0);

    /* Phase 8: fileExists style */
    printf("\n\033[1;34m--- Phase 8: fileExists ---\033[0m\n");
    fd=hexagonfs_openat(fds,rf,rf,"/vendor/etc/acdbdata/test.bin");
    T("8a exists",fd>=0);if(fd>=0)hexagonfs_close(fds,fd);
    fd=hexagonfs_openat(fds,rf,rf,"/vendor/etc/acdbdata/nope.bin");
    T("8b nope",fd<0);

    /* Phase 9: Write (optional) */
    if(do_write){
        printf("\n\033[1;34m--- Phase 9: Write ops ---\033[0m\n");
        fd=hexagonfs_openat(fds,rf,rf,"/vendor/etc/acdbdata/test.bin");
        if(fd>=0){hexagonfs_lseek(fds,fd,0,SEEK_SET);n=hexagonfs_write(fds,fd,5,"WRITE");T("9a write5",n==5);hexagonfs_close(fds,fd);}
        T("9b mkdir",hexagonfs_mkdir(fds,rf,"_td",0755)==0);
        T("9c rmdir",hexagonfs_rmdir(fds,rf,"_td")==0);
    }

    printf("\n\033[1m=== %d/%d passed ===\033[0m\n",passed,total);
    /* cleanup */
    snprintf(cmd,sizeof(cmd),"rm -rf %s",root);system(cmd);
    return passed<total ? 1 : 0;
}
