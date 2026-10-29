/* Host-side harness for src/river.c: runs the generator for N tile rows and
 * prints "distance state[16] | row[48]" per row.  tests/host/compare.py checks
 * the output against tools/river_model.py.  See tests/host_river.ps1. */#include <stdio.h>
#include <string.h>
#include "nes/nes.h"
#include "river.h"
volatile uint8_t playfield_scroll_y;
uint8_t game_ticks = 1;
void hud_set_playfield_scroll(uint8_t y){ playfield_scroll_y=y; }
void set_bkg_data(uint8_t f,uint8_t n,const uint8_t*d){(void)f;(void)n;(void)d;}
void set_vram_byte(uint8_t*a,uint8_t v){(void)a;(void)v;}
extern uint8_t river_raw_row[49];
extern volatile uint8_t river_debug_state[16];
int main(int argc,char**argv){
  int rows = argc>1? atoi(argv[1]) : 1000;
  river_init();
  uint16_t last = river_distance;
  int guard=0;
  while ((int)river_distance < rows && guard < rows*9+100) {
    river_update(); guard++;
    if (river_distance != last) {
      last = river_distance;
      // print state of tile row just finished
      printf("%u", river_distance);
      for (int i=0;i<16;i++) printf(" %u", river_debug_state[i]);
      printf(" |");
      for (int i=0;i<48;i++) printf("%02x", river_raw_row[i]);
      printf("\n");
    }
  }
  return 0;
}
