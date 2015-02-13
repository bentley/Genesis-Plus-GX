#ifdef __WIN32__
#include <windows.h>
#else
#define MessageBox(owner, text, caption, type) printf("%s: %s\n", caption, text)
#endif

#include "SDL.h"
#include "SDL_thread.h"

#include "shared.h"
#include "sms_ntsc.h"
#include "md_ntsc.h"
#include "utils.h"

#ifdef GCWZERO
#include <SDL_ttf.h>
#include <SDL_image.h>
static int gcw0_w;
static int gcw0_h;
#endif

//DK no difference? Might as well save some cycles. #define SOUND_FREQUENCY 48000
#define SOUND_FREQUENCY    44100
#define SOUND_SAMPLES_SIZE 2048

#define VIDEO_WIDTH  320
#define VIDEO_HEIGHT 240

int joynum = 0;

int log_error   = 0;
int debug_on    = 0;
int turbo_mode  = 0;
int use_sound   = 1;
int fullscreen  = 1; /* SDL_FULLSCREEN */

uint32 crc = 0;

/* sound */

struct
{
    char* current_pos;
    char* buffer;
    int current_emulated_samples;
} sdl_sound;


static uint8 brm_format[0x40] =
{
    0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x5f,0x00,0x00,0x00,0x00,0x40,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x53,0x45,0x47,0x41,0x5f,0x43,0x44,0x5f,0x52,0x4f,0x4d,0x00,0x01,0x00,0x00,0x00,
    0x52,0x41,0x4d,0x5f,0x43,0x41,0x52,0x54,0x52,0x49,0x44,0x47,0x45,0x5f,0x5f,0x5f
};


static short soundframe[SOUND_SAMPLES_SIZE];

static void sdl_sound_callback(void *userdata, Uint8 *stream, int len)
{
    if(sdl_sound.current_emulated_samples < len)
    {
        memset(stream, 0, len);
    }
    else
    {
        memcpy(stream, sdl_sound.buffer, len);
        /* loop to compensate desync */
        do
        {
            sdl_sound.current_emulated_samples -= len;
        }
        while(sdl_sound.current_emulated_samples > 2 * len);
        memcpy(sdl_sound.buffer,
               sdl_sound.current_pos - sdl_sound.current_emulated_samples,
               sdl_sound.current_emulated_samples);
        sdl_sound.current_pos = sdl_sound.buffer + sdl_sound.current_emulated_samples;
    }
}

static int sdl_sound_init()
{
    int n;
    SDL_AudioSpec as_desired, as_obtained;

    if(SDL_Init(SDL_INIT_AUDIO) < 0)
    {
        MessageBox(NULL, "SDL Audio initialization failed", "Error", 0);
        return 0;
    }

    as_desired.freq     = SOUND_FREQUENCY;
    as_desired.format   = AUDIO_S16LSB;
    as_desired.channels = 2;
    as_desired.samples  = SOUND_SAMPLES_SIZE;
    as_desired.callback = sdl_sound_callback;

    if(SDL_OpenAudio(&as_desired, &as_obtained) == -1)
    {
        MessageBox(NULL, "SDL Audio open failed", "Error", 0);
        return 0;
    }

    if(as_desired.samples != as_obtained.samples)
    {
        MessageBox(NULL, "SDL Audio wrong setup", "Error", 0);
        return 0;
    }

    sdl_sound.current_emulated_samples = 0;
    n = SOUND_SAMPLES_SIZE * 2 * sizeof(short) * 20;
    sdl_sound.buffer = (char*)malloc(n);
    if(!sdl_sound.buffer)
    {
        MessageBox(NULL, "Can't allocate audio buffer", "Error", 0);
        return 0;
    }
    memset(sdl_sound.buffer, 0, n);
    sdl_sound.current_pos = sdl_sound.buffer;
    return 1;
}

static void sdl_sound_update(enabled)
{
    int size = audio_update(soundframe) * 2;

    if (enabled)
    {
        int i;
        short *out;

        SDL_LockAudio();
        out = (short*)sdl_sound.current_pos;
        for(i = 0; i < size; i++) 
        {
            *out++ = soundframe[i];
        }
        sdl_sound.current_pos = (char*)out;
        sdl_sound.current_emulated_samples += size * sizeof(short);
        SDL_UnlockAudio();
    }
}

static void sdl_sound_close()
{
    SDL_PauseAudio(1);
    SDL_CloseAudio();
    if (sdl_sound.buffer)
        free(sdl_sound.buffer);
}

/* video */
md_ntsc_t *md_ntsc;
sms_ntsc_t *sms_ntsc;

struct
{
    SDL_Surface* surf_screen;
    SDL_Surface* surf_bitmap;
    SDL_Rect srect;
    SDL_Rect drect;
    Uint32 frames_rendered;
} sdl_video;

static int sdl_video_init()
{
    if(SDL_InitSubSystem(SDL_INIT_VIDEO) < 0)
    {
        MessageBox(NULL, "SDL Video initialization failed", "Error", 0);
        return 0;
    }
#ifdef GCWZERO
    sdl_video.surf_screen  = SDL_SetVideoMode(VIDEO_WIDTH, VIDEO_HEIGHT, 16, SDL_HWSURFACE |  
#else
    sdl_video.surf_screen  = SDL_SetVideoMode(VIDEO_WIDTH, VIDEO_HEIGHT, 16, SDL_HWSURFACE | fullscreen | 
#endif
#ifdef SDL_TRIPLEBUF
    SDL_TRIPLEBUF
#else
    SDL_DOUBLEBUF
#endif
    );
    sdl_video.surf_bitmap = SDL_CreateRGBSurface(SDL_HWSURFACE, 720, 576, 16, 0, 0, 0, 0);
    sdl_video.frames_rendered = 0;
    SDL_ShowCursor(0);
    return 1;
}

static void sdl_video_update()
{
    if (system_hw == SYSTEM_MCD)
    {
        system_frame_scd(0);
    }
    else if ((system_hw & SYSTEM_PBC) == SYSTEM_MD)
    {
        system_frame_gen(0);
    }
    else
    {
        system_frame_sms(0);
    }

    /* viewport size changed */
    if(bitmap.viewport.changed & 1)
    {
        bitmap.viewport.changed &= ~1;

        /* source bitmap */
        sdl_video.srect.w = bitmap.viewport.w+2*bitmap.viewport.x;
        sdl_video.srect.h = bitmap.viewport.h+2*bitmap.viewport.y;
        sdl_video.srect.x = 0;
        sdl_video.srect.y = 0;
        if (sdl_video.srect.w > VIDEO_WIDTH)
        {
            sdl_video.srect.x = (sdl_video.srect.w - VIDEO_WIDTH) / 2;
            sdl_video.srect.w = VIDEO_WIDTH;
        }
        if (sdl_video.srect.h > VIDEO_HEIGHT)
        {
            sdl_video.srect.y = (sdl_video.srect.h - VIDEO_HEIGHT) / 2;
            sdl_video.srect.h = VIDEO_HEIGHT;
        }

        /* destination bitmap */
        sdl_video.drect.w = sdl_video.srect.w;
        sdl_video.drect.h = sdl_video.srect.h;
        sdl_video.drect.x = (VIDEO_WIDTH  - sdl_video.drect.w) / 2;
        sdl_video.drect.y = (VIDEO_HEIGHT - sdl_video.drect.h) / 2;

        /* clear destination surface */
        SDL_FillRect(sdl_video.surf_screen, 0, 0);
#ifdef GCWZERO //triple buffering so stop flicker
	SDL_Flip(sdl_video.surf_screen);
        SDL_FillRect(sdl_video.surf_screen, 0, 0);
	SDL_Flip(sdl_video.surf_screen);
        SDL_FillRect(sdl_video.surf_screen, 0, 0);
#endif
#if 0
        if (config.render && (interlaced || config.ntsc))  rect.h *= 2;
        if (config.ntsc) rect.w = (reg[12]&1) ? MD_NTSC_OUT_WIDTH(rect.w) : SMS_NTSC_OUT_WIDTH(rect.w);
        if (config.ntsc)
        {
            sms_ntsc = (sms_ntsc_t *)malloc(sizeof(sms_ntsc_t));
            md_ntsc = (md_ntsc_t *)malloc(sizeof(md_ntsc_t));

            switch (config.ntsc)
            {
            case 1:
                sms_ntsc_init(sms_ntsc, &sms_ntsc_composite);
                md_ntsc_init(md_ntsc, &md_ntsc_composite);
                break;
            case 2:
                sms_ntsc_init(sms_ntsc, &sms_ntsc_svideo);
                md_ntsc_init(md_ntsc, &md_ntsc_svideo);
                break;
            case 3:
                sms_ntsc_init(sms_ntsc, &sms_ntsc_rgb);
                md_ntsc_init(md_ntsc, &md_ntsc_rgb);
                break;
            }
        }
        else
        {
            if (sms_ntsc)
            {
                free(sms_ntsc);
                sms_ntsc = NULL;
            }

            if (md_ntsc)
            {
                free(md_ntsc);
                md_ntsc = NULL;
            }
        }
#endif
    }

//DK IPU scaling for gg/sms roms
#ifdef GCWZERO
    if (config.gcw0_fullscreen) {
        if( (gcw0_w != sdl_video.drect.w) || (gcw0_h != sdl_video.drect.h) ) {
            sdl_video.drect.w = sdl_video.srect.w;
            sdl_video.drect.h = sdl_video.srect.h;
            sdl_video.drect.x = 0;
            sdl_video.drect.y = 0;
            gcw0_w=sdl_video.drect.w;
            gcw0_h=sdl_video.drect.h;

            sdl_video.surf_screen  = SDL_SetVideoMode(gcw0_w,gcw0_h, 16, SDL_HWSURFACE |  
            #ifdef SDL_TRIPLEBUF
            SDL_TRIPLEBUF);
            #else
            SDL_DOUBLEBUF);
            #endif
        }
    }
#endif

    SDL_BlitSurface(sdl_video.surf_bitmap, &sdl_video.srect, sdl_video.surf_screen, &sdl_video.drect);
    //SDL_UpdateRect(sdl_video.surf_screen, 0, 0, 0, 0);

//DK frameskip testing, uncomment below to set to 30fps flipped (virtual racing tests)
//static int eo;
//if(eo){
    SDL_Flip(sdl_video.surf_screen);
//eo--;}
//else eo++;
    ++sdl_video.frames_rendered;
}

static void sdl_video_close()
{
    if (sdl_video.surf_bitmap)
        SDL_FreeSurface(sdl_video.surf_bitmap);
    if (sdl_video.surf_screen)
        SDL_FreeSurface(sdl_video.surf_screen);
}

/* Timer Sync */

struct
{
    SDL_sem* sem_sync;
    unsigned ticks;
} sdl_sync;

static Uint32 sdl_sync_timer_callback(Uint32 interval)
{
    SDL_SemPost(sdl_sync.sem_sync);
    sdl_sync.ticks++;
    if (sdl_sync.ticks == (vdp_pal ? 50 : 20))
    {
        SDL_Event event;
        SDL_UserEvent userevent;

        userevent.type = SDL_USEREVENT;
        userevent.code = vdp_pal ? (sdl_video.frames_rendered / 3) : sdl_video.frames_rendered;
        userevent.data1 = NULL;
        userevent.data2 = NULL;
        sdl_sync.ticks = sdl_video.frames_rendered = 0;

        event.type = SDL_USEREVENT;
        event.user = userevent;

        SDL_PushEvent(&event);
    }
    return interval;
}

static int sdl_sync_init()
{
    if(SDL_InitSubSystem(SDL_INIT_TIMER|SDL_INIT_EVENTTHREAD) < 0)
    {
        MessageBox(NULL, "SDL Timer initialization failed", "Error", 0);
        return 0;
    }

    sdl_sync.sem_sync = SDL_CreateSemaphore(0);
    sdl_sync.ticks = 0;
    return 1;
}

static void sdl_sync_close()
{
    if(sdl_sync.sem_sync)
        SDL_DestroySemaphore(sdl_sync.sem_sync);
}

static const uint16 vc_table[4][2] =
{
    /* NTSC, PAL */
    {0xDA , 0xF2},  /* Mode 4 (192 lines) */
    {0xEA , 0x102}, /* Mode 5 (224 lines) */
    {0xDA , 0xF2},  /* Mode 4 (192 lines) */
    {0x106, 0x10A}  /* Mode 5 (240 lines) */
};

static int sdl_control_update(SDLKey keystate)
{
    switch (keystate)
    {
#ifndef GCWZERO
    case SDLK_TAB:
    {
        system_reset();
        break;
    }
#endif

    case SDLK_F1:
    {
        if (SDL_ShowCursor(-1)) SDL_ShowCursor(0);
        else SDL_ShowCursor(1);
        break;
    }

    case SDLK_F2:
    {
        if (fullscreen) fullscreen = 0;
        else fullscreen = SDL_FULLSCREEN;
        sdl_video.surf_screen = SDL_SetVideoMode(VIDEO_WIDTH, VIDEO_HEIGHT, 16,  SDL_SWSURFACE | fullscreen);
        break;
    }

    case SDLK_F3:
    {
        if (config.bios == 0) config.bios = 3;
        else if (config.bios == 3) config.bios = 1;
        break;
    }

    case SDLK_F4:
    {
        if (!turbo_mode) use_sound ^= 1;
        break;
    }

    case SDLK_F5:
    {
        log_error ^= 1;
        break;
    }

    case SDLK_F6:
    {
        if (!use_sound)
        {
            turbo_mode ^=1;
            sdl_sync.ticks = 0;
        }
        break;
    }

    case SDLK_F7:
    {
        char save_state_file[256];
        sprintf(save_state_file,"%s/%X.gp0", get_save_directory(), rominfo.realchecksum);
        FILE *f = fopen(save_state_file,"rb");
        
        if (f)
        {
            uint8 buf[STATE_SIZE];
            fread(&buf, STATE_SIZE, 1, f);
            state_load(buf);
            fclose(f);
        }
        break;
    }

    case SDLK_F8:
    {
        char save_state_file[256];
        sprintf(save_state_file,"%s/%X.gp0", get_save_directory(), rominfo.realchecksum);
        FILE *f = fopen(save_state_file,"wb");
        if (f)
        {
            uint8 buf[STATE_SIZE];
            int len = state_save(buf);
            fwrite(&buf, len, 1, f);
            fclose(f);
        }
        break;
    }

    case SDLK_F9:
    {
        config.region_detect = (config.region_detect + 1) % 5;
        get_region(0);

        /* framerate has changed, reinitialize audio timings */
        audio_init(snd.sample_rate, 0);

        /* system with region BIOS should be reinitialized */
        if ((system_hw == SYSTEM_MCD) || ((system_hw & SYSTEM_SMS) && (config.bios & 1)))
        {
            system_init();
            system_reset();
        }
        else
        {
            /* reinitialize I/O region register */
            if (system_hw == SYSTEM_MD)
            {
                io_reg[0x00] = 0x20 | region_code | (config.bios & 1);
            }
            else
            {
                io_reg[0x00] = 0x80 | (region_code >> 1);
            }

            /* reinitialize VDP */
            if (vdp_pal)
            {
                status |= 1;
                lines_per_frame = 313;
            }
            else
            {
                status &= ~1;
                lines_per_frame = 262;
            }

            /* reinitialize VC max value */
            switch (bitmap.viewport.h)
            {
            case 192:
                vc_max = vc_table[0][vdp_pal];
                break;
            case 224:
                vc_max = vc_table[1][vdp_pal];
                break;
            case 240:
                vc_max = vc_table[3][vdp_pal];
                break;
            }
        }
        break;
    }

    case SDLK_F10:
    {
        gen_reset(0);
        break;
    }

    case SDLK_F11:
    {
        config.overscan =  (config.overscan + 1) & 3;
        if ((system_hw == SYSTEM_GG) && !config.gg_extra)
        {
            bitmap.viewport.x = (config.overscan & 2) ? 14 : -48;
        }
        else
        {
            bitmap.viewport.x = (config.overscan & 2) * 7;
        }
        bitmap.viewport.changed = 3;
        break;
    }

    case SDLK_F12:
    {
        joynum = (joynum + 1) % MAX_DEVICES;
        while (input.dev[joynum] == NO_DEVICE)
        {
            joynum = (joynum + 1) % MAX_DEVICES;
        }
        break;
    }

    case SDLK_ESCAPE:
    {
#ifndef GCWZERO
	/* exit */
        return 0;
#endif
    }

    default:
        break;
    }

    return 1;
}

static void shutdown() {
	FILE *fp;
	crc = crc32(0, cart.rom, cart.romsize);
	
	//TODO: verify SCD backup room dir and files
    if (system_hw == SYSTEM_MCD)
    {
        /* save internal backup RAM (if formatted) */
        if (!memcmp(scd.bram + 0x2000 - 0x20, brm_format + 0x20, 0x20))
        {
            fp = fopen("./scd.brm", "wb");
            if (fp!=NULL)
            {
                fwrite(scd.bram, 0x2000, 1, fp);
                fclose(fp);
            }
        }

        /* save cartridge backup RAM (if formatted) */
        if (scd.cartridge.id)
        {
            if (!memcmp(scd.cartridge.area + scd.cartridge.mask + 1 - 0x20, brm_format + 0x20, 0x20))
            {
                fp = fopen("./cart.brm", "wb");
                if (fp!=NULL)
                {
                    fwrite(scd.cartridge.area, scd.cartridge.mask + 1, 1, fp);
                    fclose(fp);
                }
            }
        }
    }

    if (sram.on)
    {
        /* save SRAM */
        char save_file[256];
        sprintf(save_file,"%s/%X.srm", get_save_directory(), crc);
        fp = fopen(save_file, "wb");
        if (fp!=NULL)
        {
            fwrite(sram.sram,0x10000,1, fp);
            fclose(fp);
        }
    }
    audio_shutdown();
    error_shutdown();

    sdl_video_close();
    sdl_sound_close();
    sdl_sync_close();
    SDL_Quit();
}

#ifdef GCWZERO //menu!
static int gcw0menu(void)
{
    /* display menu */
//  change video mode
    sdl_video.surf_screen  = SDL_SetVideoMode(320,240, 16, SDL_HWSURFACE |  
    #ifdef SDL_TRIPLEBUF
    SDL_TRIPLEBUF);
    #else
    SDL_DOUBLEBUF);
    #endif
//  blank screen
    SDL_FillRect(sdl_video.surf_screen, 0, 0);
//  set up menu surface
    SDL_Surface *menuSurface = NULL;
    menuSurface = SDL_CreateRGBSurface(SDL_HWSURFACE, 320, 240, 16, 0, 0, 0, 0);

    int showmainmenu          = 1;
    int showgraphicsoptions   = 0;
    const char *gcw0menu_graphicsoptionslist[8]={
        "Scaling",
        "Keep aspect ratio",
    };
    const char *gcw0menu_onofflist[2]={
        "On",
        "Off",
    };
//  start menu loop - default = display main menu
    int done;
    bitmap.viewport.changed=1; //change screen res if required
    while(!done) 
    {

//  identify system we are using to show correct background just cos we can :P
	if      (  system_hw == SYSTEM_PICO) //Sega Pico
	{
            SDL_Surface *tempbgSurface;
            SDL_Surface *bgSurface;
	    tempbgSurface = IMG_Load( "./PICO.png" );
	    bgSurface = SDL_DisplayFormat( tempbgSurface );
      	    SDL_BlitSurface(bgSurface, NULL, menuSurface, NULL);
	    SDL_FreeSurface(tempbgSurface);
	    SDL_FreeSurface(bgSurface);
        }
        else if ( (system_hw == SYSTEM_SG)      || (system_hw == SYSTEM_SGII) ) //SG-1000 I&II
        {
            SDL_Surface *tempbgSurface;
            SDL_Surface *bgSurface;
	    tempbgSurface = IMG_Load( "./SG1000.png" );
	    bgSurface = SDL_DisplayFormat( tempbgSurface );
      	    SDL_BlitSurface(bgSurface, NULL, menuSurface, NULL);
	    SDL_FreeSurface(tempbgSurface);
	    SDL_FreeSurface(bgSurface);
	}
	else if ( (system_hw == SYSTEM_MARKIII) || (system_hw == SYSTEM_SMS) || (system_hw == SYSTEM_SMS2) || (system_hw == SYSTEM_PBC) ) //Mark III & Sega Master System I&II & Megadrive with power base converter
	{
            SDL_Surface *tempbgSurface;
            SDL_Surface *bgSurface;
	    tempbgSurface = IMG_Load( "./SMS.png" );
	    bgSurface = SDL_DisplayFormat( tempbgSurface );
      	    SDL_BlitSurface(bgSurface, NULL, menuSurface, NULL);
	    SDL_FreeSurface(tempbgSurface);
	    SDL_FreeSurface(bgSurface);
	}
	else if (  system_hw == SYSTEM_GG)   //Game gear
	{
            SDL_Surface *tempbgSurface;
            SDL_Surface *bgSurface;
	    tempbgSurface = IMG_Load( "./GG.png" );
	    bgSurface = SDL_DisplayFormat( tempbgSurface );
      	    SDL_BlitSurface(bgSurface, NULL, menuSurface, NULL);
	    SDL_FreeSurface(tempbgSurface);
	    SDL_FreeSurface(bgSurface);
	}
	else if (  system_hw == SYSTEM_MD)   //Megadrive
	{
            SDL_Surface *tempbgSurface;
            SDL_Surface *bgSurface;
	    tempbgSurface = IMG_Load( "./MD.png" );
	    bgSurface = SDL_DisplayFormat( tempbgSurface );
      	    SDL_BlitSurface(bgSurface, NULL, menuSurface, NULL);
	    SDL_FreeSurface(tempbgSurface);
	    SDL_FreeSurface(bgSurface);
	}
	else if (  system_hw == SYSTEM_MCD)  //MegaCD
	{
            SDL_Surface *tempbgSurface;
            SDL_Surface *bgSurface;
	    tempbgSurface = IMG_Load( "./MCD.png" );
	    bgSurface = SDL_DisplayFormat( tempbgSurface );
      	    SDL_BlitSurface(bgSurface, NULL, menuSurface, NULL);
	    SDL_FreeSurface(tempbgSurface);
	    SDL_FreeSurface(bgSurface);
	}

//      show menu
	TTF_Init();
        TTF_Font *ttffont = NULL;
        SDL_Color text_color = {180, 180, 180};
        SDL_Color selected_text_color = {23, 86, 155}; //selected colour = Sega blue ;)
        SDL_Surface *textSurface;

	int i;
        static int selectedoption = 0;
        const char *gcw0menu_mainlist[8]={
            "Save state",
    	    "Load state",
	    "Graphics options",
	    "Remap buttons",
	    "Resume game",
	    "", //spacer
	    "Reset",
	    "Quit"
        };
//	Show title
	ttffont = TTF_OpenFont("./ProggyTiny.ttf", 16);
        SDL_Rect destination;
	    destination.x = 100;
            destination.y = 40;
	    destination.w = 100;
	    destination.h = 50;
        textSurface = TTF_RenderText_Solid(ttffont, "Genesis Plus GX", text_color);
	SDL_BlitSurface(textSurface, NULL, menuSurface, &destination);
	SDL_FreeSurface(textSurface);
        TTF_CloseFont (ttffont);

	if (showmainmenu) 
        {
	    for(i=0;i<8;i++) 
	    {
		ttffont = TTF_OpenFont("./ProggyTiny.ttf", 16);
        	SDL_Rect destination;
	        destination.x = 100;
             	destination.y = 70+(15*i);
	        destination.w = 100;
	        destination.h = 50;
		if (i == selectedoption)
		    textSurface = TTF_RenderText_Solid(ttffont, gcw0menu_mainlist[i], selected_text_color);
		else
	            textSurface = TTF_RenderText_Solid(ttffont, gcw0menu_mainlist[i], text_color);
	      	SDL_BlitSurface(textSurface, NULL, menuSurface, &destination);
		SDL_FreeSurface(textSurface);
        	TTF_CloseFont (ttffont);
	    }
	} else if (showgraphicsoptions) 
	{
	    for(i=0;i<2;i++) 
	    {
		ttffont = TTF_OpenFont("./ProggyTiny.ttf", 16);
                SDL_Rect destination;
		destination.x = 100;
               	destination.y = 40+(15*i);
	       	destination.w = 100; 
	       	destination.h = 50;
		textSurface = TTF_RenderText_Solid(ttffont, gcw0menu_graphicsoptionslist[i], text_color);
		SDL_BlitSurface(textSurface, NULL, menuSurface, &destination);
		SDL_FreeSurface(textSurface);
        	TTF_CloseFont (ttffont);
	    }
	}

//TODO other menu's go here

//TODO add on/off depending on variables.

	    /* Update display */
	SDL_Rect dest;
	dest.w = 320;
	dest.h = 240;
	dest.x = 0;
	dest.y = 0;
	SDL_BlitSurface(menuSurface, NULL, sdl_video.surf_screen, &dest);
	SDL_Flip(sdl_video.surf_screen);

        /* Check for user input */
        SDL_Event event;
        while(SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_KEYDOWN:
		    sdl_control_update(event.key.keysym.sym);
                    break;
                default:
		    break;
	    }
	}
	uint8 *keystate2 = SDL_GetKeyState(NULL);
        if(keystate2[SDLK_DOWN]) {
	    selectedoption++;
	    if (selectedoption == 5) selectedoption = 6;
	    if (selectedoption>7)
	        selectedoption=0;
            SDL_Delay(170);
	}
        if(keystate2[SDLK_UP]) {
	    if (!selectedoption)
	        selectedoption = 7;
	    else
	        selectedoption--;
	    if (selectedoption == 5) selectedoption = 4;
	    SDL_Delay(170);
	}
//	if(keystate2[SDLK_LALT]) {
//	    break;
//	}
	if(keystate2[SDLK_LCTRL]) {
	    if (selectedoption == 0) { //Save
		char save_state_file[256];
		sprintf(save_state_file,"%s/%X.gp0", get_save_directory(), crc);
		FILE *f = fopen(save_state_file,"wb");
		if (f)
		{
		    uint8 buf[STATE_SIZE];
		    int len = state_save(buf);
		    fwrite(&buf, len, 1, f);
		    fclose(f);
		}
	        SDL_Delay(170);
		break;
	    }
	    if (selectedoption == 1) { //Load
		char save_state_file[256];
		sprintf(save_state_file,"%s/%X.gp0", get_save_directory(), crc );
		FILE *f = fopen(save_state_file,"rb");
		if (f)
		{
		    uint8 buf[STATE_SIZE];
		    fread(&buf, STATE_SIZE, 1, f);
		    state_load(buf);
		    fclose(f);
		}
		selectedoption=0;
	        SDL_Delay(170);
		break;
	    }
	    if (selectedoption == 2) { //Graphics
		config.gcw0_fullscreen = !config.gcw0_fullscreen;//toggle
		if(!config.gcw0_fullscreen) {
		    gcw0_w=320;
		    gcw0_h=240;
		    sdl_video.surf_screen  = SDL_SetVideoMode(gcw0_w,gcw0_h, 16, SDL_HWSURFACE |  
		    #ifdef SDL_TRIPLEBUF
		    SDL_TRIPLEBUF);
		    #else
		    SDL_DOUBLEBUF);
	            #endif
	        }
			selectedoption=0;
			config_save();
	        SDL_Delay(170);
	        break;
	    }
	    if (selectedoption == 3) { //Remap
//TODO
		selectedoption=0;
	    }
	    if (selectedoption == 4) { //Resume
		selectedoption=0;
	        SDL_Delay(170);
	        break;
	    }
	    if (selectedoption == 6) { //Reset
//TODO
		selectedoption=0;
	        system_reset();
	        SDL_Delay(170);
		break;
	    }
	    if (selectedoption == 7) { //Quit
//TODO
		shutdown();
	        SDL_Delay(170);
	        break;
	    }
	}
//change variables
    }//done

    return 1;
}
#endif

int sdl_input_update(void)
{
    uint8 *keystate = SDL_GetKeyState(NULL);

    /* reset input */
    input.pad[joynum] = 0;

    switch (input.dev[joynum])
    {
    case DEVICE_LIGHTGUN:
    {
        /* get mouse coordinates (absolute values) */
        int x,y;
        int state = SDL_GetMouseState(&x,&y);

        /* X axis */
        input.analog[joynum][0] =  x - (VIDEO_WIDTH-bitmap.viewport.w)/2;

        /* Y axis */
        input.analog[joynum][1] =  y - (VIDEO_HEIGHT-bitmap.viewport.h)/2;

        /* TRIGGER, B, C (Menacer only), START (Menacer & Justifier only) */
        if(state & SDL_BUTTON_LMASK) input.pad[joynum] |= INPUT_A;
        if(state & SDL_BUTTON_RMASK) input.pad[joynum] |= INPUT_B;
        if(state & SDL_BUTTON_MMASK) input.pad[joynum] |= INPUT_C;
        if(keystate[SDLK_f])  input.pad[joynum] |= INPUT_START;
        break;
    }

    case DEVICE_PADDLE:
    {
        /* get mouse (absolute values) */
        int x;
        int state = SDL_GetMouseState(&x, NULL);

        /* Range is [0;256], 128 being middle position */
        input.analog[joynum][0] = x * 256 /VIDEO_WIDTH;

        /* Button I -> 0 0 0 0 0 0 0 I*/
        if(state & SDL_BUTTON_LMASK) input.pad[joynum] |= INPUT_B;

        break;
    }

    case DEVICE_SPORTSPAD:
    {
        /* get mouse (relative values) */
        int x,y;
        int state = SDL_GetRelativeMouseState(&x,&y);

        /* Range is [0;256] */
        input.analog[joynum][0] = (unsigned char)(-x & 0xFF);
        input.analog[joynum][1] = (unsigned char)(-y & 0xFF);

        /* Buttons I & II -> 0 0 0 0 0 0 II I*/
        if(state & SDL_BUTTON_LMASK) input.pad[joynum] |= INPUT_B;
        if(state & SDL_BUTTON_RMASK) input.pad[joynum] |= INPUT_C;

        break;
    }

    case DEVICE_MOUSE:
    {
        /* get mouse (relative values) */
        int x,y;
        int state = SDL_GetRelativeMouseState(&x,&y);

        /* Sega Mouse range is [-256;+256] */
        input.analog[joynum][0] = x * 2;
        input.analog[joynum][1] = y * 2;

        /* Vertical movement is upsidedown */
        if (!config.invert_mouse)
            input.analog[joynum][1] = 0 - input.analog[joynum][1];

        /* Start,Left,Right,Middle buttons -> 0 0 0 0 START MIDDLE RIGHT LEFT */
        if(state & SDL_BUTTON_LMASK) input.pad[joynum] |= INPUT_B;
        if(state & SDL_BUTTON_RMASK) input.pad[joynum] |= INPUT_C;
        if(state & SDL_BUTTON_MMASK) input.pad[joynum] |= INPUT_A;
        if(keystate[SDLK_f])  input.pad[joynum] |= INPUT_START;

        break;
    }

    case DEVICE_XE_1AP:
    {
        /* A,B,C,D,Select,START,E1,E2 buttons -> E1(?) E2(?) START SELECT(?) A B C D */
        if(keystate[SDLK_a])  input.pad[joynum] |= INPUT_START;
        if(keystate[SDLK_s])  input.pad[joynum] |= INPUT_A;
        if(keystate[SDLK_d])  input.pad[joynum] |= INPUT_C;
        if(keystate[SDLK_f])  input.pad[joynum] |= INPUT_Y;
        if(keystate[SDLK_z])  input.pad[joynum] |= INPUT_B;
        if(keystate[SDLK_x])  input.pad[joynum] |= INPUT_X;
        if(keystate[SDLK_c])  input.pad[joynum] |= INPUT_MODE;
        if(keystate[SDLK_v])  input.pad[joynum] |= INPUT_Z;

        /* Left Analog Stick (bidirectional) */
        if(keystate[SDLK_UP])     input.analog[joynum][1]-=2;
        else if(keystate[SDLK_DOWN])   input.analog[joynum][1]+=2;
        else input.analog[joynum][1] = 128;
        if(keystate[SDLK_LEFT])   input.analog[joynum][0]-=2;
        else if(keystate[SDLK_RIGHT])  input.analog[joynum][0]+=2;
        else input.analog[joynum][0] = 128;

        /* Right Analog Stick (unidirectional) */
        if(keystate[SDLK_KP8])    input.analog[joynum+1][0]-=2;
        else if(keystate[SDLK_KP2])   input.analog[joynum+1][0]+=2;
        else if(keystate[SDLK_KP4])   input.analog[joynum+1][0]-=2;
        else if(keystate[SDLK_KP6])  input.analog[joynum+1][0]+=2;
        else input.analog[joynum+1][0] = 128;

        /* Limiters */
        if (input.analog[joynum][0] > 0xFF) input.analog[joynum][0] = 0xFF;
        else if (input.analog[joynum][0] < 0) input.analog[joynum][0] = 0;
        if (input.analog[joynum][1] > 0xFF) input.analog[joynum][1] = 0xFF;
        else if (input.analog[joynum][1] < 0) input.analog[joynum][1] = 0;
        if (input.analog[joynum+1][0] > 0xFF) input.analog[joynum+1][0] = 0xFF;
        else if (input.analog[joynum+1][0] < 0) input.analog[joynum+1][0] = 0;
        if (input.analog[joynum+1][1] > 0xFF) input.analog[joynum+1][1] = 0xFF;
        else if (input.analog[joynum+1][1] < 0) input.analog[joynum+1][1] = 0;

        break;
    }

    case DEVICE_PICO:
    {
        /* get mouse (absolute values) */
        int x,y;
        int state = SDL_GetMouseState(&x,&y);

        /* Calculate X Y axis values */
        input.analog[0][0] = 0x3c  + (x * (0x17c-0x03c+1)) / VIDEO_WIDTH;
        input.analog[0][1] = 0x1fc + (y * (0x2f7-0x1fc+1)) / VIDEO_HEIGHT;

        /* Map mouse buttons to player #1 inputs */
        if(state & SDL_BUTTON_MMASK) pico_current = (pico_current + 1) & 7;
        if(state & SDL_BUTTON_RMASK) input.pad[0] |= INPUT_PICO_RED;
        if(state & SDL_BUTTON_LMASK) input.pad[0] |= INPUT_PICO_PEN;

        break;
    }

    case DEVICE_TEREBI:
    {
        /* get mouse (absolute values) */
        int x,y;
        int state = SDL_GetMouseState(&x,&y);

        /* Calculate X Y axis values */
        input.analog[0][0] = (x * 250) / VIDEO_WIDTH;
        input.analog[0][1] = (y * 250) / VIDEO_HEIGHT;

        /* Map mouse buttons to player #1 inputs */
        if(state & SDL_BUTTON_RMASK) input.pad[0] |= INPUT_B;

        break;
    }

    case DEVICE_GRAPHIC_BOARD:
    {
        /* get mouse (absolute values) */
        int x,y;
        int state = SDL_GetMouseState(&x,&y);

        /* Calculate X Y axis values */
        input.analog[0][0] = (x * 255) / VIDEO_WIDTH;
        input.analog[0][1] = (y * 255) / VIDEO_HEIGHT;

        /* Map mouse buttons to player #1 inputs */
        if(state & SDL_BUTTON_LMASK) input.pad[0] |= INPUT_GRAPHIC_PEN;
        if(state & SDL_BUTTON_RMASK) input.pad[0] |= INPUT_GRAPHIC_MENU;
        if(state & SDL_BUTTON_MMASK) input.pad[0] |= INPUT_GRAPHIC_DO;

        break;
    }

    case DEVICE_ACTIVATOR:
    {
        if(keystate[SDLK_g])  input.pad[joynum] |= INPUT_ACTIVATOR_7L;
        if(keystate[SDLK_h])  input.pad[joynum] |= INPUT_ACTIVATOR_7U;
        if(keystate[SDLK_j])  input.pad[joynum] |= INPUT_ACTIVATOR_8L;
        if(keystate[SDLK_k])  input.pad[joynum] |= INPUT_ACTIVATOR_8U;
    }

    default:
    {
#ifdef GCWZERO
        if(keystate[SDLK_LSHIFT])    input.pad[joynum] |= INPUT_A;//x
        if(keystate[SDLK_LALT])      input.pad[joynum] |= INPUT_B;//b
        if(keystate[SDLK_LCTRL])     input.pad[joynum] |= INPUT_C;//a
        if(keystate[SDLK_RETURN])    input.pad[joynum] |= INPUT_START;
        if(keystate[SDLK_TAB])       input.pad[joynum] |= INPUT_X;//l
        if(keystate[SDLK_SPACE])     input.pad[joynum] |= INPUT_Y; //y
        if(keystate[SDLK_BACKSPACE]) input.pad[joynum] |= INPUT_Z; //r

//DK load/save better handled by menu?
	if (keystate[SDLK_ESCAPE]) {
	    gcw0menu();
	}
#else
        if(keystate[SDLK_a])  input.pad[joynum] |= INPUT_A;
        if(keystate[SDLK_s])  input.pad[joynum] |= INPUT_B;
        if(keystate[SDLK_d])  input.pad[joynum] |= INPUT_C;
        if(keystate[SDLK_f])  input.pad[joynum] |= INPUT_START;
        if(keystate[SDLK_z])  input.pad[joynum] |= INPUT_X;
        if(keystate[SDLK_x])  input.pad[joynum] |= INPUT_Y;
        if(keystate[SDLK_c])  input.pad[joynum] |= INPUT_Z;
        if(keystate[SDLK_v])  input.pad[joynum] |= INPUT_MODE;
#endif


        if     (keystate[SDLK_UP]   )  input.pad[joynum] |= INPUT_UP;
        else if(keystate[SDLK_DOWN] )  input.pad[joynum] |= INPUT_DOWN;
        if     (keystate[SDLK_LEFT] )  input.pad[joynum] |= INPUT_LEFT;
        else if(keystate[SDLK_RIGHT])  input.pad[joynum] |= INPUT_RIGHT;

        break;
    }
    }
    return 1;
}



int main (int argc, char **argv)
{
    FILE *fp;
    int running = 1;
    atexit(shutdown);

    /* Print help if no game specified */
    if(argc < 2)
    {
        char caption[256];
        sprintf(caption, "Genesis Plus GX\\SDL\nusage: %s gamename\n", argv[0]);
        MessageBox(NULL, caption, "Information", 0);
        exit(1);
    }

    error_init();
    create_default_directories();
    /* set default config */
    set_config_defaults();

    /* mark all BIOS as unloaded */
    system_bios = 0;

    /* Genesis BOOT ROM support (2KB max) */
    memset(boot_rom, 0xFF, 0x800);
    fp = fopen(MD_BIOS, "rb");
    if (fp != NULL)
    {
        int i;

        /* read BOOT ROM */
        fread(boot_rom, 1, 0x800, fp);
        fclose(fp);

        /* check BOOT ROM */
        if (!memcmp((char *)(boot_rom + 0x120),"GENESIS OS", 10))
        {
            /* mark Genesis BIOS as loaded */
            system_bios = SYSTEM_MD;
        }

        /* Byteswap ROM */
        for (i=0; i<0x800; i+=2)
        {
            uint8 temp = boot_rom[i];
            boot_rom[i] = boot_rom[i+1];
            boot_rom[i+1] = temp;
        }
    }

    /* initialize SDL */
    if(SDL_Init(0) < 0)
    {
        char caption[256];
        sprintf(caption, "SDL initialization failed");
        MessageBox(NULL, caption, "Error", 0);
        exit(1);
    }
    sdl_video_init();
    if (use_sound) sdl_sound_init();
    sdl_sync_init();

    /* initialize Genesis virtual system */
    SDL_LockSurface(sdl_video.surf_bitmap);
    memset(&bitmap, 0, sizeof(t_bitmap));
    bitmap.width        = 720;
    bitmap.height       = 576;
#if defined(USE_8BPP_RENDERING)
    bitmap.pitch        = (bitmap.width * 1);
#elif defined(USE_15BPP_RENDERING)
    bitmap.pitch        = (bitmap.width * 2);
#elif defined(USE_16BPP_RENDERING)
    bitmap.pitch        = (bitmap.width * 2);
#elif defined(USE_32BPP_RENDERING)
    bitmap.pitch        = (bitmap.width * 4);
#endif
    bitmap.data         = sdl_video.surf_bitmap->pixels;
    SDL_UnlockSurface(sdl_video.surf_bitmap);
    bitmap.viewport.changed = 3;

    /* Load game file */
    if(!load_rom(argv[1]))
    {
        char caption[256];
        sprintf(caption, "Error loading file `%s'.", argv[1]);
        MessageBox(NULL, caption, "Error", 0);
        exit(1);
    }
    
    crc = crc32(0, cart.rom, cart.romsize);

    /* initialize system hardware */
    audio_init(SOUND_FREQUENCY, 0);
    system_init();

    /* Mega CD specific */
    if (system_hw == SYSTEM_MCD)
    {
        /* load internal backup RAM */
        fp = fopen("./scd.brm", "rb");
        if (fp!=NULL)
        {
            fread(scd.bram, 0x2000, 1, fp);
            fclose(fp);
        }

        /* check if internal backup RAM is formatted */
        if (memcmp(scd.bram + 0x2000 - 0x20, brm_format + 0x20, 0x20))
        {
            /* clear internal backup RAM */
            memset(scd.bram, 0x00, 0x200);

            /* Internal Backup RAM size fields */
            brm_format[0x10] = brm_format[0x12] = brm_format[0x14] = brm_format[0x16] = 0x00;
            brm_format[0x11] = brm_format[0x13] = brm_format[0x15] = brm_format[0x17] = (sizeof(scd.bram) / 64) - 3;

            /* format internal backup RAM */
            memcpy(scd.bram + 0x2000 - 0x40, brm_format, 0x40);
        }

        /* load cartridge backup RAM */
        if (scd.cartridge.id)
        {
            fp = fopen("./cart.brm", "rb");
            if (fp!=NULL)
            {
                fread(scd.cartridge.area, scd.cartridge.mask + 1, 1, fp);
                fclose(fp);
            }

            /* check if cartridge backup RAM is formatted */
            if (memcmp(scd.cartridge.area + scd.cartridge.mask + 1 - 0x20, brm_format + 0x20, 0x20))
            {
                /* clear cartridge backup RAM */
                memset(scd.cartridge.area, 0x00, scd.cartridge.mask + 1);

                /* Cartridge Backup RAM size fields */
                brm_format[0x10] = brm_format[0x12] = brm_format[0x14] = brm_format[0x16] = (((scd.cartridge.mask + 1) / 64) - 3) >> 8;
                brm_format[0x11] = brm_format[0x13] = brm_format[0x15] = brm_format[0x17] = (((scd.cartridge.mask + 1) / 64) - 3) & 0xff;

                /* format cartridge backup RAM */
                memcpy(scd.cartridge.area + scd.cartridge.mask + 1 - sizeof(brm_format), brm_format, sizeof(brm_format));
            }
        }
    }

    if (sram.on)
    {
        /* load SRAM */
        char save_file[256];
        sprintf(save_file,"%s/%X.srm", get_save_directory(), crc);
        fp = fopen(save_file, "rb");
        if (fp!=NULL)
        {
            fread(sram.sram,0x10000,1, fp);
            fclose(fp);
        }
    }

    /* reset system hardware */
    system_reset();

    if(use_sound) SDL_PauseAudio(0);

    /* 3 frames = 50 ms (60hz) or 60 ms (50hz) */
    if(sdl_sync.sem_sync)
        SDL_SetTimer(vdp_pal ? 60 : 50, sdl_sync_timer_callback);

    /* emulation loop */
    while(running)
    {
        SDL_Event event;
        if (SDL_PollEvent(&event))
        {
            switch(event.type)
            {
            case SDL_USEREVENT:
            {
                char caption[100];
                sprintf(caption,"Genesis Plus GX - %d fps - %s)", event.user.code, (rominfo.international[0] != 0x20) ? rominfo.international : rominfo.domestic);
                SDL_WM_SetCaption(caption, NULL);
                break;
            }

            case SDL_QUIT:
            {
                running = 0;
                break;
            }

            case SDL_KEYDOWN:
            {
                running = sdl_control_update(event.key.keysym.sym);
                break;
            }
            }
        }

        sdl_video_update();
        sdl_sound_update(use_sound);

        if(!turbo_mode && sdl_sync.sem_sync && sdl_video.frames_rendered % 3 == 0)
        {
            SDL_SemWait(sdl_sync.sem_sync);
        }
    }
    
    return 0;

}
