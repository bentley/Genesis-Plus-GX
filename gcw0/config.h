
#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <pwd.h>

#define CONFIG_VERSION "GENPLUS-GX 1.7.5"
/****************************************************************************
 * Config Option
 *
 ****************************************************************************/
typedef struct
{
  uint8 padtype;
} t_input_config;

typedef struct
{
  char version[16];
  uint8 gcw0_fullscreen;
  uint8 hq_fm;
  uint8 filter;
  uint8 psgBoostNoise;
  uint8 dac_bits;
  uint8 ym2413;
  int16 psg_preamp;
  int16 fm_preamp;
  uint32 lp_range;
  int16 low_freq;
  int16 high_freq;
  int16 lg;
  int16 mg;
  int16 hg;
  uint8 mono;
  uint8 system;
  uint8 region_detect;
  uint8 vdp_mode;
  uint8 master_clock;
  uint8 force_dtack;
  uint8 addr_error;
  uint8 bios;
  uint8 lock_on;
  uint8 hot_swap;
  uint8 invert_mouse;
  uint8 gun_cursor[2];
  uint8 overscan;
  uint8 gg_extra;
  uint8 ntsc;
  uint8 lcd;
  uint8 render;
  t_input_config input[MAX_INPUTS];
} t_config;

/* Global variables */
extern t_config config;
extern void config_save(void);
extern void set_config_defaults(void);

#endif /* _CONFIG_H_ */

