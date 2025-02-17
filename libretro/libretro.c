#include "libretro.h"
#include "libretro_core_options.h"
#include "log.h"
#include <stdio.h>
#include <string.h>

#include "../types.h"
#include "../state.h"
#include "../neopopsound.h"
#include "../sound.h"
#include "../input.h"
#include "../flash.h"
#include "../tlcs900h.h"
#include "../race-memory.h"
#include "../graphics.h"
#include "../state.h"

static retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

#define RACE_NAME_MODULE "race"
#define RACE_NAME "RACE"
#define RACE_VERSION "v2.16"
#define RACE_EXTENSIONS "ngp|ngc|ngpc|npc"
#define RACE_TIMING_FPS 60.25
#define RACE_GEOMETRY_BASE_W 160
#define RACE_GEOMETRY_BASE_H 152
#define RACE_GEOMETRY_MAX_W 160
#define RACE_GEOMETRY_MAX_H 152
#define RACE_GEOMETRY_ASPECT_RATIO 1.05

#define FB_WIDTH 160
#define FB_HEIGHT 152

/* core options */
static int RETRO_SAMPLE_RATE = 44100;

struct ngp_screen* screen;
int setting_ngp_language; /* 0x6F87 - language */
int gfx_hacks;
int tipo_consola; /* 0x6F91 - OS version */
static bool libretro_supports_input_bitmasks;

char retro_save_directory[2048];

struct map
{
   unsigned retro;
   unsigned ngp;
};

static struct map btn_map[] = {
   { RETRO_DEVICE_ID_JOYPAD_A, 0x20 },
   { RETRO_DEVICE_ID_JOYPAD_B, 0x10 },
   { RETRO_DEVICE_ID_JOYPAD_RIGHT, 0x08 },
   { RETRO_DEVICE_ID_JOYPAD_LEFT, 0x04 },
   { RETRO_DEVICE_ID_JOYPAD_UP, 0x01 },
   { RETRO_DEVICE_ID_JOYPAD_DOWN, 0x02 },
   { RETRO_DEVICE_ID_JOYPAD_START, 0x40 },
};

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void graphics_paint(void)
{
   video_cb(screen->pixels, screen->w, screen->h, FB_WIDTH << 1);
}

static void check_variables(void)
{
   struct retro_variable var  = {0};
   unsigned dark_filter_level = 0;

   var.key = "race_language";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
   {
      /* user must manually restart core for change to happen
       * > 0: English
       * > 1: Japanese
       */
      if (!strcmp(var.value, "japanese"))
         setting_ngp_language = 1;
      else if (!strcmp(var.value, "english"))
         setting_ngp_language = 0;
   }

   var.key   = "race_dark_filter_level";
   var.value = NULL;

   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      dark_filter_level = (unsigned)(atoi(var.value));
   graphicsSetDarkFilterLevel(dark_filter_level);
}
void retro_init(void)
{
   char *dir = NULL;
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_RGB565;

   /* set up some logging */
   init_log(environ_cb);

   if (environ_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
      sprintf(retro_save_directory, "%s%c", dir, path_default_slash_c());

   if (log_cb)
      log_cb(RETRO_LOG_INFO, "[RACE]: Save directory: %s.\n", retro_save_directory);

   check_variables();

   if(!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt) && log_cb)
      log_cb(RETRO_LOG_ERROR, "[could not set RGB565]\n");

   if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
      libretro_supports_input_bitmasks = true;
}

void retro_reset(void)
{
   flashShutdown();
   system_sound_chipreset();
   mainemuinit();
}

void retro_deinit(void)
{
    flashShutdown();
    libretro_supports_input_bitmasks = false;
}

void retro_set_environment(retro_environment_t cb)
{
   static const struct retro_system_content_info_override content_overrides[] = {
      {
         RACE_EXTENSIONS, /* extensions */
         false,           /* need_fullpath */
         false            /* persistent_data */
      },
      { NULL, false, false }
   };

   environ_cb = cb;

   libretro_set_core_options(environ_cb);
   environ_cb(RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE,
         (void*)content_overrides);
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

static unsigned get_race_input_bitmasks(void)
{
   unsigned i = 0;
   unsigned res = 0;
   unsigned ret = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
   for (i = 0; i < sizeof(btn_map) / sizeof(struct map); i++)
      res |= (ret & (1 << btn_map[i].retro)) ? btn_map[i].ngp : 0;
   return res;
}

static unsigned get_race_input(void)
{
   unsigned i = 0;
   unsigned res = 0;
   for (i = 0; i < sizeof(btn_map) / sizeof(struct map); i++)
      res |= input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, btn_map[i].retro) ? btn_map[i].ngp : 0;
   return res;
}

static void race_input(void)
{
   ngpInputState = 0;
   input_poll_cb();
   if (libretro_supports_input_bitmasks)
      ngpInputState = get_race_input_bitmasks();
   else
      ngpInputState = get_race_input();
}

static bool race_initialize_sound(void)
{
    system_sound_chipreset();
    return true;
}

static bool race_initialize_system(const char *gamepath,
      const unsigned char *gamedata, size_t gamesize)
{
   mainemuinit();

   if (!handleInputFile(gamepath, gamedata, (int)gamesize))
   {
      handle_error("ERROR handleInputFile");
      return false;
   }

   return true;
}

void retro_set_controller_port_device(unsigned a, unsigned b) { }

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = RACE_NAME;
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif

   info->need_fullpath    = true;
   info->library_version  = RACE_VERSION GIT_VERSION;
   info->valid_extensions = RACE_EXTENSIONS;
   info->block_extract    = false;
}
void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = RACE_TIMING_FPS;
   info->timing.sample_rate    = RETRO_SAMPLE_RATE;
   info->geometry.base_width   = RACE_GEOMETRY_BASE_W;
   info->geometry.base_height  = RACE_GEOMETRY_BASE_H;
   info->geometry.max_width    = RACE_GEOMETRY_MAX_W;
   info->geometry.max_height   = RACE_GEOMETRY_MAX_H;
   info->geometry.aspect_ratio = RACE_GEOMETRY_ASPECT_RATIO;
}

#define CPU_FREQ 6144000

void retro_run(void)
{
   unsigned i;
   bool updated = false;
   static int16_t sampleBuffer[2048];
   static int16_t stereoBuffer[2048];
   int16_t *p = NULL;
   uint16_t samplesPerFrame;

   race_input();

   tlcs_execute(CPU_FREQ / HOST_FPS);

   /* Get the number of samples in a frame */
   samplesPerFrame = RETRO_SAMPLE_RATE / HOST_FPS;

   memset(sampleBuffer, 0, samplesPerFrame * sizeof(int16_t));

   sound_update((uint16_t*)sampleBuffer, samplesPerFrame * sizeof(int16_t)); /* Get sound data */
   dac_update((uint16_t*)sampleBuffer, samplesPerFrame * sizeof(int16_t));

   p = stereoBuffer;
   
   for (i = 0; i < samplesPerFrame; i++)
   {
      p[0] = sampleBuffer[i];
      p[1] = sampleBuffer[i];
      p += 2;
   }

   audio_batch_cb(stereoBuffer, samplesPerFrame);

   /* TODO/FIXME - shouldn't we check this at the top of this function? */
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &updated) && updated)
      check_variables();
}

size_t retro_serialize_size(void)
{
   return state_get_size();
}

bool retro_serialize(void *data, size_t size)
{
   return state_store_mem(data);
}

bool retro_unserialize(const void *data, size_t size)
{
   int ret = state_restore_mem((void*)data);
   return (ret == 1);
}

bool retro_load_game(const struct retro_game_info *info)
{
   const struct retro_game_info_ext *info_ext = NULL;
   const unsigned char *content_data          = NULL;
   size_t content_size                        = 0;
   char content_path[_MAX_PATH];

   struct retro_input_descriptor desc[] = {
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "D-Pad Left" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "D-Pad Up" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "D-Pad Down" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "D-Pad Right" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "A" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "B" },
      { 0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, "Option" },

      { 0 },
   };

   content_path[0] = '\0';

   /* Attempt to fetch extended game info */
   if (environ_cb(RETRO_ENVIRONMENT_GET_GAME_INFO_EXT, &info_ext))
   {
      content_data = (const unsigned char *)info_ext->data;
      content_size = info_ext->size;

      if (info_ext->file_in_archive)
      {
         /* We don't have a 'physical' file in this
          * case, but the core still needs a filename
          * in order to build the save file path.
          * We therefore fake it, using the content
          * directory, canonical content name, and
          * content file extension */
         snprintf(content_path, sizeof(content_path), "%s%c%s.%s",
               info_ext->dir, path_default_slash_c(),
               info_ext->name, info_ext->ext);
      }
      else
      {
         strncpy(content_path, info_ext->full_path, sizeof(content_path));
         content_path[sizeof(content_path) - 1] = '\0';
      }
   }
   else
   {
      if (!info || !info->path)
         return false;

      content_data = NULL;
      content_size = 0;

      strncpy(content_path, info->path, sizeof(content_path));
      content_path[sizeof(content_path) - 1] = '\0';
   }

   environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

   screen         = (struct ngp_screen*)calloc(1, sizeof(*screen));

   if (!screen)
      return false;

   screen->w      = FB_WIDTH;
   screen->h      = FB_HEIGHT;

   screen->pixels = calloc(1, FB_WIDTH * FB_HEIGHT * 2);

   if (!screen->pixels)
   {
      free(screen);
      return false;
   }

   if (!race_initialize_system(content_path,
         content_data, content_size))
      return false;

   if (!race_initialize_sound())
      return false;

   check_variables();

   {
      /* TODO: Mappings might need updating
       * Size is based on what is exposed in Mednafen NGP */
      struct retro_memory_descriptor descs = {
         RETRO_MEMDESC_SYSTEM_RAM, mainram, 0, 0, 0, 0, 16384, "RAM"
      };
      struct retro_memory_map retro_map = {
         &descs, 1
      };
      environ_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &retro_map);
   }

   return true;
}

bool retro_load_game_special(unsigned a, const struct retro_game_info *b, size_t c)
{
   return false;
}

void retro_unload_game(void)
{
   if (screen)
   {
      if (screen->pixels)
         free(screen->pixels);
      free(screen);
      screen = NULL;
   }
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

void *retro_get_memory_data(unsigned type)
{
   return NULL;
}

size_t retro_get_memory_size(unsigned type)
{
   return 0;
}
