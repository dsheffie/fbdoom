// Emacs style mode select   -*- C++ -*- 
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// $Log:$
//
// DESCRIPTION:
//	DOOM graphics stuff for X11, UNIX.
//
//-----------------------------------------------------------------------------

static const char
rcsid[] = "$Id: i_x.c,v 1.6 1997/02/03 22:45:10 b1 Exp $";

#include "config.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_event.h"
#include "d_main.h"
#include "i_video.h"
#include "z_zone.h"

#include "tables.h"
#include "doomkeys.h"
#include "rvprint.h"

#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include <stdarg.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/mman.h>

int fb_scaling = 1;
int usemouse = 0;

struct color {
    uint32_t b:8;
    uint32_t g:8;
    uint32_t r:8;
    uint32_t a:8;
};

static struct color colors[256];

// The screen buffer; this is modified to draw things to the screen
byte *I_VideoBuffer = NULL, *I_VideoBuffer_FB = NULL;
static size_t fbsz = 0;

/* framebuffer file descriptor */
int fd_fb = 0;

int X_width,  X_height;

// If true, game is running as a screensaver

boolean screensaver_mode = false;

// Flag indicating whether the screen is currently visible:
// when the screen isnt visible, don't render the screen

boolean screenvisible;

// Mouse acceleration
//
// This emulates some of the behavior of DOS mouse drivers by increasing
// the speed when the mouse is moved fast.
//
// The mouse input values are input directly to the game, but when
// the values exceed the value of mouse_threshold, they are multiplied
// by mouse_acceleration to increase the speed.

float mouse_acceleration = 2.0;
int mouse_threshold = 10;

// Gamma correction level to use

int usegamma = 0;

typedef struct
{
  byte r;
  byte g;
  byte b;
} col_t;

// Palette converted to RGB565

static uint16_t rgb565_palette[256];

static double timestamp() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + ((double)tv.tv_usec)*1e-6;
}


#define __asm_syscall(...) \
        __asm__ __volatile__ ("ecall\n\t" \
        : "+r"(a0) : __VA_ARGS__ : "memory"); \
	return a0; \


static inline long va2pa(void *a)
{
  register long a7 __asm__("a7") = 257;
  register long a0 __asm__("a0") = (uintptr_t)a;
  __asm_syscall("r"(a7), "0"(a0))
}


static void* mmap_mem(size_t n_bytes) {
  void *ptr = NULL;
  if(n_bytes > getpagesize()) {
    ptr = mmap(0, n_bytes, PROT_READ|PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_HUGETLB, -1, 0);
  }
  if(ptr == (void*)(-1L) || ptr == NULL ) {
    ptr = mmap(0, n_bytes, PROT_READ|PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  }
  printf("buffer of size %zu starts at %p, mapped to phys addr %lx\n", n_bytes, ptr, va2pa(ptr));
  return ptr;
}

static size_t next_pow2(size_t x) {
  size_t y = 1;
  while(y < x) {
    y *= 2;
  }
  return y;
}

void I_InitGraphics (void)
{
    printf("I_InitGraphics: DOOM screen size: w x h: %d x %d\n", SCREENWIDTH, SCREENHEIGHT);
    /* Allocate screen to draw to */
    I_VideoBuffer = (byte*)mmap_mem(SCREENWIDTH * SCREENHEIGHT);

    fbsz = next_pow2(SCREENWIDTH * SCREENHEIGHT * 4);
    I_VideoBuffer_FB = (byte*)mmap_mem(fbsz*2);

    screenvisible = true;
    
    extern int I_InitInput(void);
    I_InitInput();
}

void I_ShutdownGraphics (void) {
  munmap(I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
  munmap(I_VideoBuffer_FB, 2*fbsz);
}

void I_StartFrame (void)
{

}

__attribute__ ((weak)) void I_GetEvent (void){ }

__attribute__ ((weak)) void I_StartTic (void)
{
	I_GetEvent();
}

void I_UpdateNoBlit (void) {}



static uint64_t n_frames_rendered = 0;

static uint64_t l1d[2] = {0};
static uint64_t l1i[2] = {0};
static uint64_t l2[2] = {0};

static uint64_t last_icnt = 0;
static uint64_t last_cycle = 0;
static uint64_t last_mispred = 0;

static uint64_t last_l1d[2] = {0};
static uint64_t last_l1i[2] = {0};
static uint64_t last_l2[2] = {0};


static double last_time = 0.0;

#define FRAMES_PER_STAT (512)
int bufid = 0;

/* #define PREFETCH(X) { (void)*((volatile unsigned char*)(X)); }*/
#define PREFETCH(X) { __builtin_prefetch(X, 0, 1); }

static inline uint64_t extract_byte(uint64_t u64, int b) {
  return (u64 >> (b * 8))& 0xff;
}


void I_FinishUpdate (void) {
  unsigned char *line_in = (unsigned char *) I_VideoBuffer;
  volatile struct color *line_out = (volatile struct color*) &I_VideoBuffer_FB[bufid*fbsz];
  volatile uint64_t *line_out64 = (volatile uint64_t*) &I_VideoBuffer_FB[bufid*fbsz];
  uint32_t *colors_32 = (uint32_t*)colors;  
  bufid = (bufid+1) & 1;

  PREFETCH(&line_in[0]);
  PREFETCH(&line_in[16]);
  PREFETCH(&line_in[32]);  
  
  for(int ii = 0, j=0; ii < SCREENWIDTH*SCREENHEIGHT; ii+=16) {
    /* pretetch pixel on next cacheline */
    //unsigned char pf = *((volatile unsigned char*)&line_in[ii+16]);
    //(void)pf;
    PREFETCH(&line_in[ii+48]);
    
    //PREFETCH(&line_out64[j+8]);
    // PREFETCH(&line_out64[j+10]);
    //PREFETCH(&line_out64[j+12]);
    //PREFETCH(&line_out64[j+14]);            
    asm __volatile__("": : :"memory");
    
    for(int i = ii; i < (ii+16); i+=2) {
      uint64_t c = colors_32[line_in[i+0]];
      c |=  ((uint64_t)colors_32[line_in[i+1]]) <<32;
      line_out64[j++] = c;
    }
  }
  
  //for(int i = 0,j=0; i < SCREENWIDTH*SCREENHEIGHT; i+=2,j++) {
  //uint64_t c = colors_32[line_in[i+0]];
  //c |=  ((uint64_t)colors_32[line_in[i+1]]) <<32;
  //line_out64[j] = c;
  //}
  // asm volatile ("fence.i" ::: "memory"); 

  
  if((n_frames_rendered % FRAMES_PER_STAT) == 0) {
    uint64_t icnt = rdinstret();
    uint64_t cycle = rdcycle();
    uint64_t mispred = rdbrmispred();

    rd_l1d(l1d);
    rd_l1i(l1i);
    rd_l2(l2);
    
    uint64_t insn_delta = icnt-last_icnt;
    double ipc = ((double)icnt) / (cycle);
    double jpki = (1000.0 * (mispred)) / icnt;
    double insn_per_frame = ((double)insn_delta)/FRAMES_PER_STAT;

    uint64_t l2_misses = (l2[1] - l2[0]);
    uint64_t l1d_misses = (l1d[1] - l1d[0]);
    uint64_t l1i_misses = (l1i[1] - l1i[0]);

    double l1d_mpki = 1000.0 * (((double)l1d_misses) / icnt);
    double l1i_mpki = 1000.0 * (((double)l1i_misses) / icnt);    
    double l2_mpki = 1000.0 * (((double)l2_misses) / icnt);

    double now = timestamp();
    
    printf("%lu insn, %g insn per frame, %g ipc, %g jpki, %g l1d mpki, %g l1i mpki, %g l2 mpki, %g fps\n",
	   insn_delta, insn_per_frame, ipc, jpki,
	   l1d_mpki,
	   l1i_mpki,
	   l2_mpki,
	   ((double)FRAMES_PER_STAT)/(now-last_time));
    
    last_icnt = icnt;
    last_cycle = cycle;
    last_mispred = mispred;
    last_l1d[0] = l1d[0];
    last_l1i[0] = l1i[0];
    last_l2[0] = l2[0];
    last_l1d[1] = l1d[1];
    last_l1i[1] = l1i[1];
    last_l2[1] = l2[1];
    last_time = now;
  }
  ++n_frames_rendered;  
}

//
// I_ReadScreen
//
void I_ReadScreen (byte* scr)
{
    memcpy (scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT);
}

//
// I_SetPalette
//
#define GFX_RGB565(r, g, b)			((((r & 0xF8) >> 3) << 11) | (((g & 0xFC) >> 2) << 5) | ((b & 0xF8) >> 3))
#define GFX_RGB565_R(color)			((0xF800 & color) >> 11)
#define GFX_RGB565_G(color)			((0x07E0 & color) >> 5)
#define GFX_RGB565_B(color)			(0x001F & color)

void I_SetPalette (byte* palette)
{
  /* performance boost:
   * map to the right pixel format over here! */
  
  for (int i =0; i<256; ++i ) {
    colors[i].a = 0;
    colors[i].r = gammatable[usegamma][*palette++];
    colors[i].g = gammatable[usegamma][*palette++];
    colors[i].b = gammatable[usegamma][*palette++];
  }
}

// Given an RGB value, find the closest matching palette index.

int I_GetPaletteIndex (int r, int g, int b)
{
    int best, best_diff, diff;
    int i;
    col_t color;

    printf("I_GetPaletteIndex\n");

    best = 0;
    best_diff = INT_MAX;

    for (i = 0; i < 256; ++i)
    {
    	color.r = GFX_RGB565_R(rgb565_palette[i]);
    	color.g = GFX_RGB565_G(rgb565_palette[i]);
    	color.b = GFX_RGB565_B(rgb565_palette[i]);

        diff = (r - color.r) * (r - color.r)
             + (g - color.g) * (g - color.g)
             + (b - color.b) * (b - color.b);

        if (diff < best_diff)
        {
            best = i;
            best_diff = diff;
        }

        if (diff == 0)
        {
            break;
        }
    }

    return best;
}

void I_BeginRead (void) {}

void I_EndRead (void) {}

void I_SetWindowTitle (char *title) {}

void I_GraphicsCheckCommandLine (void) {}

void I_SetGrabMouseCallback (grabmouse_callback_t func) {}

void I_EnableLoadingDisk(void) {}

void I_BindVideoVariables (void) {}

void I_DisplayFPSDots (boolean dots_on)
{
}

void I_CheckIsScreensaver (void)
{
}
