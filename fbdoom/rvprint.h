#ifndef _rvprinth_
#define _rvprinth_

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define __ASM_STR(x)    #x

#define csr_read(csr)                                           \
({                                                              \
        register unsigned long __v;                             \
        __asm__ __volatile__ ("csrr %0, " __ASM_STR(csr)        \
                              : "=r" (__v) :                    \
                              : "memory");                      \
        __v;                                                    \
})

#define csr_write(csr, val)                                     \
({                                                              \
        unsigned long __v = (unsigned long)(val);               \
        __asm__ __volatile__ ("csrw " __ASM_STR(csr) ", %0"     \
                              : : "rK" (__v)                    \
                              : "memory");                      \
})


static inline void csr_print(char *buf, int len, int add_nl) {
  int i;
  for(i = 0; i < len; i++) {
    while(csr_read(0xc03) != 0) {}
    csr_write(0xc03, buf[i]);
  }
  if(add_nl) {
    while(csr_read(0xc03) != 0) {}    
    csr_write(0xc03, '\n');
  }
}

static inline int printf_(const char *fmt, ...) {
  va_list args;
  char buf[256];
  va_start(args, fmt);
  int d = vsprintf(buf, fmt, args);
  va_end(args);
  csr_print(buf, strlen(buf), 0);
  return d;
}


inline static uint64_t rdcycle(void) {
    uint64_t cycles;
    asm volatile ("rdcycle %0" : "=r" (cycles));
    return cycles;
}

inline static uint64_t rdinstret(void) {
    uint64_t ret;
    asm volatile ("rdinstret %0" : "=r" (ret));
    return ret;
}

inline static uint64_t rdbrmispred(void) {
  return csr_read(0xc04);
}

inline static void rd_l1d(uint64_t* x) {
  x[0] = csr_read(0xc09) - x[0];
  x[1] = csr_read(0xc0a) - x[1];
}

inline static void rd_l1i(uint64_t* x) {
  x[0] = csr_read(0xc0b) - x[0];
  x[1] = csr_read(0xc0c) - x[1];
}

inline static void rd_l2(uint64_t* x) {
  x[0] = csr_read(0xc0d) - x[0];
  x[1] = csr_read(0xc0e) - x[1];
}

#endif
