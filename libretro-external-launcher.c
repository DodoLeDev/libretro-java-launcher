/*
 * External launcher: a libretro core that runs content with an external
 * program (java, wine, dolphin...) chosen by a "profile".
 *
 * Profiles and presets live in
 * <RetroArch system directory>/external_launcher/profiles.cfg (see README.md).
 * Which profile and presets a game uses are core options, so RetroArch saves
 * them per core, per content directory or per game, like for any other core.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include "libretro.h"

#define MAX_PROFILES 32
#define MAX_ENV      16
#define STR          512

struct profile {
   char name[64];        /* section name */
   char extensions[STR]; /* "jar|exe" */
   char command[STR];    /* template: {file} {args} {width} {height} */
   char args[STR];
   char env[MAX_ENV][STR];
   int  env_count;
   char width[16];
   char height[16];
};

/* Used when the user has no profiles.cfg. */
static const char default_profiles[] =
   "[java]\n"
   "extensions = jar\n"
   "command = java {args} -jar {file}\n"
   "[wine]\n"
   "extensions = exe\n"
   "command = wine {args} {file}\n"
   "[dolphin]\n"
   "extensions = elf|dol|gcm|iso|wbfs|ciso|gcz|wad\n"
   "command = dolphin-emu -b -e {file} {args}\n"
   "[preset:720p]\n"
   "width = 1280\n"
   "height = 720\n"
   "[preset:1080p]\n"
   "width = 1920\n"
   "height = 1080\n";

static struct profile profiles[MAX_PROFILES];
static int profile_count;
static bool loaded;
static char extensions[STR * 4];
static char profile_choices[STR] = "Profile; auto";
static char preset_choices[3][STR];
static const char *preset_keys[3] = { "launcher_preset1", "launcher_preset2", "launcher_preset3" };

#define PRESET_PREFIX "preset:"

static char content_path[STR * 2];
static bool start_was_pressed;
static unsigned frame_count;

static retro_environment_t environ_cb;
static retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

static void fallback_log(enum retro_log_level level, const char *fmt, ...)
{
   (void)level;
   va_list va;
   va_start(va, fmt);
   vfprintf(stderr, fmt, va);
   va_end(va);
}

/* ---- Small string helpers ---------------------------------------------- */

static void copy(char *dst, size_t size, const char *src)
{
   snprintf(dst, size, "%s", src);
}

static void append(char *out, size_t size, const char *text)
{
   strncat(out, text, size - strlen(out) - 1);
}

/* Appends 'text' to 'out' as one single-quoted shell word. */
static void append_quoted(char *out, size_t size, const char *text)
{
   append(out, size, "'");
   for (; *text; text++) {
      char one[5] = { *text, '\0' };
      if (*text == '\'')
         copy(one, sizeof(one), "'\\''");
      append(out, size, one);
   }
   append(out, size, "'");
}

static char *trim(char *s)
{
   char *end;
   while (*s == ' ' || *s == '\t')
      s++;
   end = s + strlen(s);
   while (end > s && strchr(" \t\r\n", end[-1]))
      *--end = '\0';
   return s;
}

/* ---- Config parsing ---------------------------------------------------- */

static void set_key(struct profile *p, const char *key, const char *value)
{
   if (!strcmp(key, "extensions"))   copy(p->extensions, sizeof(p->extensions), value);
   else if (!strcmp(key, "command")) copy(p->command, sizeof(p->command), value);
   else if (!strcmp(key, "args"))    copy(p->args, sizeof(p->args), value);
   else if (!strcmp(key, "width"))   copy(p->width, sizeof(p->width), value);
   else if (!strcmp(key, "height"))  copy(p->height, sizeof(p->height), value);
   else if (!strcmp(key, "env") && p->env_count < MAX_ENV)
      copy(p->env[p->env_count++], STR, value);
   else
      log_cb(RETRO_LOG_WARN, "[launcher] Ignoring unknown key '%s'\n", key);
}

/* Parses "[section]" and "key = value" lines ('#' starts a comment line). */
static void parse(FILE *f)
{
   char line[STR * 2];
   struct profile *cur = NULL;

   while (fgets(line, sizeof(line), f)) {
      char *s = trim(line), *eq = strchr(s, '=');
      if (*s == '[') {
         if (profile_count >= MAX_PROFILES)
            break;
         cur = &profiles[profile_count++];
         memset(cur, 0, sizeof(*cur));
         s[strcspn(s, "]")] = '\0';
         copy(cur->name, sizeof(cur->name), s + 1);
      } else if (cur && eq && *s != '#') {
         *eq = '\0';
         set_key(cur, trim(s), trim(eq + 1));
      }
   }
}

static void load_profiles(void)
{
   char path[STR];
   const char *system_dir = NULL;
   FILE *f = NULL;
   int i;

   if (loaded)
      return;
   loaded = true;
   if (!log_cb)
      log_cb = fallback_log;

   /* <RetroArch system directory>/external_launcher/profiles.cfg */
   if (environ_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_dir) && system_dir) {
      snprintf(path, sizeof(path), "%s/external_launcher/profiles.cfg", system_dir);
      f = fopen(path, "r");
   }
   if (!f)
      f = fmemopen((void *)default_profiles, sizeof(default_profiles) - 1, "r");
   if (f) {
      parse(f);
      fclose(f);
   }

   /* The frontend needs the union of all extensions to detect games. */
   for (i = 0; i < profile_count; i++) {
      if (!profiles[i].extensions[0])
         continue;
      if (extensions[0])
         append(extensions, sizeof(extensions), "|");
      append(extensions, sizeof(extensions), profiles[i].extensions);
   }
}

/* ---- Profile selection ------------------------------------------------- */

static struct profile *find_by_name(const char *name)
{
   int i;
   for (i = 0; i < profile_count; i++)
      if (!strcmp(profiles[i].name, name))
         return &profiles[i];
   return NULL;
}

static struct profile *find_by_extension(const char *path)
{
   const char *ext = strrchr(path, '.');
   size_t len;
   int i;

   if (!ext)
      return NULL;
   ext++;
   len = strlen(ext);
   for (i = 0; i < profile_count; i++) {
      const char *p = profiles[i].extensions;
      while (*p) {
         size_t n = strcspn(p, "|");
         if (n == len && !strncasecmp(p, ext, n))
            return &profiles[i];
         p += n + (p[n] == '|');
      }
   }
   return NULL;
}

/* Value of a core option set in the RetroArch menu, or 'fallback'. */
static const char *get_option(const char *key, const char *fallback)
{
   struct retro_variable var = { key, NULL };
   if (environ_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
      return var.value;
   return fallback;
}

/* Applies a preset on top of the profile. */
static void override(struct profile *dst, const struct profile *src)
{
   int i;
   if (src->command[0]) copy(dst->command, sizeof(dst->command), src->command);
   if (src->args[0])    copy(dst->args, sizeof(dst->args), src->args);
   if (src->width[0])   copy(dst->width, sizeof(dst->width), src->width);
   if (src->height[0])  copy(dst->height, sizeof(dst->height), src->height);
   for (i = 0; i < src->env_count && dst->env_count < MAX_ENV; i++)
      copy(dst->env[dst->env_count++], STR, src->env[i]);
}

/* ---- Command building -------------------------------------------------- */

/* Replaces {file}, {args}, {width} and {height} in the command template. */
static void expand(char *out, size_t size, const struct profile *p, const char *file)
{
   const char *c = p->command;
   while (*c) {
      if (!strncmp(c, "{file}", 6)) {
         append_quoted(out, size, file);
         c += 6;
      } else if (!strncmp(c, "{args}", 6)) {
         append(out, size, p->args); /* raw: may hold several arguments */
         c += 6;
      } else if (!strncmp(c, "{width}", 7)) {
         append(out, size, p->width);
         c += 7;
      } else if (!strncmp(c, "{height}", 8)) {
         append(out, size, p->height);
         c += 8;
      } else {
         char one[2] = { *c++, '\0' };
         append(out, size, one);
      }
   }
}

/*
 * Builds the command for the loaded game from the options currently
 * selected in the menu. Returns the profile used, or NULL if there is none.
 */
static const struct profile *build_command(char *out, size_t size)
{
   const char *wanted = get_option("launcher_profile", "auto");
   struct profile chosen, *base;
   int i;

   base = strcmp(wanted, "auto") ? find_by_name(wanted) : find_by_extension(content_path);
   if (!base || !base->command[0])
      return NULL;

   /* Settings stack up: profile, then presets 1 to 3. */
   chosen = *base;
   for (i = 0; i < 3; i++) {
      char name[STR] = PRESET_PREFIX;
      struct profile *preset;
      append(name, sizeof(name), get_option(preset_keys[i], "none"));
      if ((preset = find_by_name(name)))
         override(&chosen, preset);
   }

   out[0] = '\0';
   if (chosen.env_count) {
      append(out, size, "env");
      for (i = 0; i < chosen.env_count; i++) {
         append(out, size, " ");
         append_quoted(out, size, chosen.env[i]);
      }
      append(out, size, " ");
   }
   expand(out, size, &chosen, content_path);
   return base;
}

/* ---- Launch screen ----------------------------------------------------- */

static void notify(const char *text)
{
   struct retro_message msg = { text, 180 };
   environ_cb(RETRO_ENVIRONMENT_SET_MESSAGE, &msg);
}

/* Runs the game and waits for it; RetroArch closes the content afterwards. */
static void launch(void)
{
   char command[STR * 4];
   int status;

   if (!build_command(command, sizeof(command)))
      return;

   log_cb(RETRO_LOG_INFO, "[launcher] Running: %s\n", command);
   status = system(command);
   /* Games return arbitrary exit codes: only failing to start is an error. */
   if (status == -1 || (WIFEXITED(status) && WEXITSTATUS(status) == 127)) {
      log_cb(RETRO_LOG_ERROR, "[launcher] Could not run: %s\n", command);
      notify("Could not run the program. Check the profile's command.");
      return;
   }
   environ_cb(RETRO_ENVIRONMENT_SHUTDOWN, NULL);
}

/*
 * Until Start is pressed, the user can open the Quick Menu to pick the
 * profile and presets (and save them for this game).
 */
void retro_run(void)
{
   static uint32_t frame[1];
   bool pressed;

   input_poll_cb();
   pressed = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);
   video_cb(frame, 1, 1, sizeof(uint32_t));

   if (pressed && !start_was_pressed) {
      launch();
      frame_count = 1; /* keep any error message visible for a while */
   }
   start_was_pressed = pressed;

   /* Refreshed regularly, so it follows option changes. */
   if (frame_count++ % 120 == 0) {
      char hint[STR], command[STR * 4];
      const struct profile *p = build_command(command, sizeof(command));
      if (p)
         snprintf(hint, sizeof(hint), "Press Start to launch with \"%s\". Settings: Quick Menu > Options.", p->name);
      else
         copy(hint, sizeof(hint), "No profile for this file. Choose one in Quick Menu > Options.");
      notify(hint);
   }
}

/* ---- libretro API ------------------------------------------------------ */

void retro_set_environment(retro_environment_t cb)
{
   struct retro_log_callback logging;
   struct retro_variable vars[5] = { { "launcher_profile", profile_choices } };
   int i, p;

   environ_cb = cb;
   if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &logging))
      log_cb = logging.log;
   load_profiles();

   /*
    * Core options are the settings RetroArch edits and saves per game, but
    * they can only be a choice in a list. So the menu picks a profile and up
    * to three presets, all defined in profiles.cfg.
    */
   for (p = 0; p < 3; p++) {
      snprintf(preset_choices[p], STR, "Preset %d; none", p + 1);
      vars[p + 1].key   = preset_keys[p];
      vars[p + 1].value = preset_choices[p];
   }
   for (i = 0; i < profile_count; i++) {
      const char *name = profiles[i].name;
      if (!strncmp(name, PRESET_PREFIX, strlen(PRESET_PREFIX))) {
         for (p = 0; p < 3; p++) {
            append(preset_choices[p], STR, "|");
            append(preset_choices[p], STR, name + strlen(PRESET_PREFIX));
         }
      } else if (name[0] && profiles[i].command[0]) {
         append(profile_choices, sizeof(profile_choices), "|");
         append(profile_choices, sizeof(profile_choices), name);
      }
   }
   cb(RETRO_ENVIRONMENT_SET_VARIABLES, vars);
}

void retro_get_system_info(struct retro_system_info *info)
{
   /* Profiles load in retro_set_environment, once the system directory is
    * known. Before that there is no extension filter. */
   memset(info, 0, sizeof(*info));
   info->library_name     = "External Launcher";
#ifndef GIT_VERSION
#define GIT_VERSION ""
#endif
   info->library_version  = "2.0.0" GIT_VERSION;
   info->need_fullpath    = true;
   info->valid_extensions = extensions;
}

/* Only remembers the game: it is launched from the launch screen. */
bool retro_load_game(const struct retro_game_info *info)
{
   if (!info || !info->path || !info->path[0]) {
      log_cb(RETRO_LOG_ERROR, "[launcher] No content given.\n");
      return false;
   }
   copy(content_path, sizeof(content_path), info->path);
   /* Ignore the Start/Enter press that may have loaded the game. */
   start_was_pressed = true;
   frame_count = 0;
   return true;
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = 60.0;
   info->timing.sample_rate    = 44100.0;
   info->geometry.base_width   = info->geometry.max_width  = 1;
   info->geometry.base_height  = info->geometry.max_height = 1;
   info->geometry.aspect_ratio = 4.0f / 3.0f;
}

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }
void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }
void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

/* Everything below is required by libretro but unused. */
void retro_init(void) {}
void retro_deinit(void) {}
unsigned retro_api_version(void) { return RETRO_API_VERSION; }
void retro_set_controller_port_device(unsigned port, unsigned device) { (void)port; (void)device; }
void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { (void)cb; }
void retro_reset(void) {}
void retro_unload_game(void) {}
unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num) { (void)type; (void)info; (void)num; return false; }
size_t retro_serialize_size(void) { return 0; }
bool retro_serialize(void *data, size_t size) { (void)data; (void)size; return false; }
bool retro_unserialize(const void *data, size_t size) { (void)data; (void)size; return false; }
void *retro_get_memory_data(unsigned id) { (void)id; return NULL; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
void retro_cheat_reset(void) {}
void retro_cheat_set(unsigned index, bool enabled, const char *code) { (void)index; (void)enabled; (void)code; }
