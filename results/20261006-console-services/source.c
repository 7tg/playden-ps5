/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Read-only phase-1b hook inventory for 13.60. Full table/entry layout
 * cross-checked against pinned kstuff-lite v1.11; high words are not state.
 * Read-only: an earlier experimental enable attempt stalled console services.
 * Do not reintroduce writes without diagnosing that result. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <ps5/kernel.h>
/* KEKCALL_CHECK register contract from ShadowMountPlus sm_kstuff.c (GPL-3.0). */
static int loaded(void) {
    register uint64_t r10 __asm__("r10")=0, r8 __asm__("r8")=0, r9 __asm__("r9")=0;
    uint64_t result;
    unsigned char error;
    __asm__ volatile("syscall" : "=a"(result), "=@ccc"(error), "+r"(r10), "+r"(r8), "+r"(r9)
                     : "a"(UINT64_C(0xffffffff00000027)), "D"(UINT64_C(0)),
                       "S"(UINT64_C(0)), "d"(UINT64_C(0)) : "rcx", "r11", "memory");
    return !error && result==0;
}
/* PS5 13.60 layout from kstuff-lite's sysentvec/FreeBSD sysent definitions.
 * Read the complete pointer and each hook candidate; a high word cannot tell
 * whether replacement-table INT3 hooks are installed. */
static int table_observation(const char *label, intptr_t vector, intptr_t original) {
    /* Numbers from pinned ps5-kstuff/main.c and FreeBSD syscall headers. */
    static const struct { const char *name; unsigned number; } calls[] = {
        {"getppid",39},{"execve",59},{"mprotect",74},{"mdbg_call",573},
        {"dynlib_load_prx",594},{"get_self_auth_info",607},
        {"get_sdk_compiled_version",647},{"get_ppr_sdk_compiled_version",713},
        {"ioctl",54},{"nmount",378},{"unmount",22}
    };
    uint32_t count=0;
    uint64_t table=0;
    int ok=(uint64_t)vector>>48==0xffff && kernel_copyout(vector,&count,4)==0
           && kernel_copyout(vector+8,&table,8)==0;
    ok &= count>647 && count<4096 && (table>>48)==0xffff;
    printf("{\"probe\":\"syscall_table\",\"label\":\"%s\",\"read_ok\":%s,"
           "\"vector\":\"0x%016lx\",\"table\":\"0x%016lx\","
           "\"original_table\":\"0x%016lx\",\"count\":%u}\n",
           label,ok?"true":"false",(uint64_t)vector,table,(uint64_t)original,count);
    if (!ok) return 0;
    uint64_t hook=(uint64_t)(KERNEL_ADDRESS_DATA_BASE-0xA591C0);
    for (unsigned i=0;i<sizeof(calls)/sizeof(calls[0]);i++) {
        if (calls[i].number>=count) {
            printf("{\"probe\":\"syscall_entry\",\"table_label\":\"%s\","
                   "\"name\":\"%s\",\"number\":%u,\"classification\":\"out_of_range\"}\n",
                   label,calls[i].name,calls[i].number);
            continue;
        }
        uint64_t call=0, stock=0;
        int read=kernel_copyout((intptr_t)table+48*calls[i].number+8,&call,8)==0
              && kernel_copyout(original+48*calls[i].number+8,&stock,8)==0;
        const char *kind=!read?"unreadable":call==hook?"int3_hook":call==stock?"stock":"unexpected";
        printf("{\"probe\":\"syscall_entry\",\"table_label\":\"%s\","
               "\"name\":\"%s\",\"number\":%u,\"target\":\"0x%016lx\","
               "\"stock_target\":\"0x%016lx\",\"classification\":\"%s\"}\n",
               label,calls[i].name,calls[i].number,call,stock,kind);
        ok &= read && (call==hook || call==stock);
    }
    return ok;
}
int main(void) {
    char log[] = "/data/playden/logs/kstuff-state-probe-XXXXXX";
    int fd = mkstemp(log);
    if (fd < 0 || dup2(fd,STDOUT_FILENO)<0) return 2;
    close(fd);
    setvbuf(stdout,0,_IONBF,0);
    uint32_t fw=kernel_get_fw_version();
    printf("{\"probe\":\"firmware\",\"value\":\"0x%08x\"}\n",fw);
    if ((fw & 0xffff0000u) != 0x13600000u) {
        puts("{\"probe\":\"exit_requested\",\"code\":1}");return 1;
    }
    int ready=loaded();
    printf("{\"probe\":\"kstuff_loaded\",\"status\":\"%s\"}\n",ready?"pass":"fail");
    if (!ready) { puts("{\"probe\":\"exit_requested\",\"code\":1}");return 1; }
    intptr_t ps5=KERNEL_ADDRESS_DATA_BASE+0xddd8f8+14;
    intptr_t ps4=KERNEL_ADDRESS_DATA_BASE+0xddda70+14;
    uint16_t a=0,b=0;
    int ok=kernel_copyout(ps5,&a,2)==0 && kernel_copyout(ps4,&b,2)==0;
    printf("{\"probe\":\"sysentvec_high_words\",\"read_ok\":%s,\"ps5\":\"0x%04x\",\"ps4\":\"0x%04x\"}\n",ok?"true":"false",a,b);
    if (ok) {
        ok &= table_observation("ps5",ps5-14,KERNEL_ADDRESS_DATA_BASE+0x1B6E50);
        ok &= table_observation("ps4",ps4-14,KERNEL_ADDRESS_DATA_BASE+0x1AE5C0);
    }

#ifdef KSTUFF_TARGET_PID
    intptr_t proc=kernel_get_proc(KSTUFF_TARGET_PID);
    uint32_t pid=0;
    uint64_t vector=0;
    int identity=proc && kernel_copyout(proc+KERNEL_OFFSET_PROC_P_PID,&pid,4)==0
                 && pid==KSTUFF_TARGET_PID && kernel_copyout(proc+0xA08,&vector,8)==0;
    printf("{\"probe\":\"target_identity\",\"read_ok\":%s,\"pid\":%u,"
           "\"proc\":\"0x%016lx\",\"vector\":\"0x%016lx\"}\n",
           identity?"true":"false",pid,(uint64_t)proc,vector);
    ok &= identity;
    if (identity) ok &= table_observation("target_process",(intptr_t)vector,
        KERNEL_ADDRESS_DATA_BASE+(vector==(uint64_t)(ps4-14)?0x1AE5C0:0x1B6E50));
#endif
    printf("{\"probe\":\"exit_requested\",\"code\":%d}\n",ok?0:1);
    fsync(STDOUT_FILENO);
    return ok?0:1;
}
