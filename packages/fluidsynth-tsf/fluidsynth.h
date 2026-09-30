/* The subset of FluidSynth's public API that mkxp-z uses (see fluid_tsf.c). */
#ifndef FLUIDSYNTH_H
#define FLUIDSYNTH_H

#define FLUIDSYNTH_VERSION_MAJOR 2

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _fluid_hashtable_t fluid_settings_t;
typedef struct _fluid_synth_t fluid_synth_t;

fluid_settings_t* new_fluid_settings(void);
void delete_fluid_settings(fluid_settings_t* settings);
int fluid_settings_setnum(fluid_settings_t* settings, const char* name, double val);
int fluid_settings_setint(fluid_settings_t* settings, const char* name, int val);
int fluid_settings_setstr(fluid_settings_t* settings, const char* name, const char* str);

fluid_synth_t* new_fluid_synth(fluid_settings_t* settings);
void delete_fluid_synth(fluid_synth_t* synth);

int fluid_synth_sfload(fluid_synth_t* synth, const char* filename, int reset_presets);
int fluid_synth_system_reset(fluid_synth_t* synth);
int fluid_synth_write_s16(fluid_synth_t* synth, int len, void* lout, int loff, int lincr, void* rout, int roff, int rincr);
int fluid_synth_noteon(fluid_synth_t* synth, int chan, int key, int vel);
int fluid_synth_noteoff(fluid_synth_t* synth, int chan, int key);
int fluid_synth_channel_pressure(fluid_synth_t* synth, int chan, int val);
int fluid_synth_pitch_bend(fluid_synth_t* synth, int chan, int val);
int fluid_synth_cc(fluid_synth_t* synth, int chan, int ctrl, int val);
int fluid_synth_program_change(fluid_synth_t* synth, int chan, int program);

#ifdef __cplusplus
}
#endif

#endif
