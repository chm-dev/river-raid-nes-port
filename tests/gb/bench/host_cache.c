/* Host harness: how many river cache slots a GB view needs.  Counts the tiles
 * uploaded per tile row (river_alloc, ring of TILE_RIVER_COUNT) and prints the
 * largest total over a sliding window of rows, the tiles that must be live at
 * once.  Build: see the zig cc line in the commit message. */
#include <stdio.h>
#include <string.h>
#include "nes/nes.h"
#include "config.h"
#include "river.h"
volatile uint8_t playfield_scroll_y;
uint8_t game_ticks = 1;
void entities_row(void){}
void hud_set_playfield_scroll(uint8_t y){ playfield_scroll_y=y; }
void set_bkg_data(uint8_t f,uint8_t n,const uint8_t*d){(void)f;(void)n;(void)d;}
void set_vram_byte(uint8_t*a,uint8_t v){(void)a;(void)v;}
extern uint8_t river_alloc;
#define MAXW 32
int main(int argc,char**argv){
  int rows = argc>1? atoi(argv[1]) : 20000;
  static const int win[] = {18, 20, 22, 28};
  for (int v=0; v<4; v++) {
    river_init(v);
    int best[4]={0,0,0,0}, hist[MAXW]={0}, n=0, peak=0, h=0;
    long total=0; unsigned hash=0, first=0;
    uint16_t last=river_distance;
    uint8_t alloc=river_alloc;
    int guard=0;
    while ((int)river_distance<rows && guard<rows*9+100) {
      river_update(); guard++;
      if (river_distance!=last) {
        last=river_distance;
        int d=((int)river_alloc-(int)alloc+TILE_RIVER_COUNT)%TILE_RIVER_COUNT;
        alloc=river_alloc;
        hist[h]=d; h=(h+1)%MAXW; n++;
        hash=hash*31u+(unsigned)d; if(n==1500) first=hash;
        if (n>40) {               /* skip the start road and the black rows */
          total+=d;
          if (d>peak) peak=d;
          for (int w=0;w<4;w++) {
            int s=0;
            for (int k=1;k<=win[w];k++) s+=hist[(h-k+MAXW)%MAXW];
            if (s>best[w]) best[w]=s;
          }
        }
      }
    }
    printf("level_seed=%u lfsr=%u hash1500=%08x hashAll=%08x ", rg_level, rg_lfsr_a, first, hash); printf("start variant %d: rows %d avg/row %.2f peak/row %d | max live tiles: 18 rows=%d 20 rows=%d 22 rows=%d 28 rows=%d\n",
           v,n,(double)total/(n>40?n-40:1),peak,best[0],best[1],best[2],best[3]);
  }
  return 0;
}
