/* A tiny stand-in for the parts of FluidSynth's API that mkxp-z uses, built on TinySoundFont.
 *
 * mkxp-z parses MIDI files itself and only needs a synthesizer: create it, load a SoundFont, send
 * note/controller/program events, and pull out blocks of 16 bit audio. FluidSynth itself needs glib
 * and a lot more; TinySoundFont (https://github.com/schellingb/TinySoundFont, MIT) is one header.
 *
 * Implemented: new_fluid_settings, delete_fluid_settings, fluid_settings_set{num,int,str},
 * new_fluid_synth, delete_fluid_synth, fluid_synth_sfload, fluid_synth_system_reset,
 * fluid_synth_write_s16, fluid_synth_{noteon,noteoff,cc,program_change,pitch_bend,channel_pressure}.
 *
 * Copyright (C) 2026 the easyrpg-ps5 authors. GPLv3 or later.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TSF_IMPLEMENTATION
#include "tsf.h"

#include "fluidsynth.h"

#define FLUID_OK 0
#define FLUID_FAILED (-1)
#define CHANNELS 16
#define MAX_VOICES 128

struct _fluid_hashtable_t {
	double gain;
	double sample_rate;
};

struct _fluid_synth_t {
	double gain;
	double sample_rate;
	tsf* sf;
};

/* The SoundFont is loaded once and every synth gets a copy that shares the (large) sample data. */
static tsf* master;
static char master_path[1024];

fluid_settings_t* new_fluid_settings(void) {
	fluid_settings_t* s = calloc(1, sizeof *s);
	if (s) {
		s->gain = 1.0;
		s->sample_rate = 44100.0;
	}
	return s;
}

void delete_fluid_settings(fluid_settings_t* settings) {
	free(settings);
}

int fluid_settings_setnum(fluid_settings_t* settings, const char* name, double val) {
	if (!settings || !name) return FLUID_FAILED;
	if (!strcmp(name, "synth.gain")) settings->gain = val;
	else if (!strcmp(name, "synth.sample-rate")) settings->sample_rate = val;
	return FLUID_OK;
}

int fluid_settings_setint(fluid_settings_t* settings, const char* name, int val) {
	(void)settings; (void)name; (void)val; /* chorus / reverb: not supported by TinySoundFont */
	return FLUID_OK;
}

int fluid_settings_setstr(fluid_settings_t* settings, const char* name, const char* str) {
	(void)settings; (void)name; (void)str;
	return FLUID_OK;
}

fluid_synth_t* new_fluid_synth(fluid_settings_t* settings) {
	fluid_synth_t* synth = calloc(1, sizeof *synth);
	if (!synth) return NULL;
	synth->gain = settings ? settings->gain : 1.0;
	synth->sample_rate = settings ? settings->sample_rate : 44100.0;
	return synth;
}

void delete_fluid_synth(fluid_synth_t* synth) {
	if (!synth) return;
	if (synth->sf) tsf_close(synth->sf);
	free(synth);
}

/* Channel 10 (index 9) plays percussion, everything else starts on piano. */
static void init_channels(tsf* sf) {
	for (int c = 0; c < CHANNELS; c++) {
		tsf_channel_set_presetnumber(sf, c, 0, c == 9);
	}
}

int fluid_synth_sfload(fluid_synth_t* synth, const char* filename, int reset_presets) {
	(void)reset_presets;
	if (!synth || !filename) return FLUID_FAILED;
	if (!master || strcmp(master_path, filename) != 0) {
		tsf* loaded = tsf_load_filename(filename);
		if (!loaded) return FLUID_FAILED;
		master = loaded; /* the previous master, if any, stays alive for the synths that copied it */
		snprintf(master_path, sizeof master_path, "%s", filename);
	}
	tsf* sf = tsf_copy(master);
	if (!sf) return FLUID_FAILED;
	if (synth->sf) tsf_close(synth->sf);
	synth->sf = sf;

	float gain_db = synth->gain > 0.0 ? 20.0f * log10f((float)synth->gain) : -100.0f;
	tsf_set_output(sf, TSF_STEREO_INTERLEAVED, (int)synth->sample_rate, gain_db);
	tsf_set_max_voices(sf, MAX_VOICES);
	init_channels(sf);
	return 1; /* a SoundFont id */
}

int fluid_synth_system_reset(fluid_synth_t* synth) {
	if (!synth || !synth->sf) return FLUID_FAILED;
	tsf_reset(synth->sf);
	init_channels(synth->sf);
	return FLUID_OK;
}

int fluid_synth_write_s16(fluid_synth_t* synth, int len, void* lout, int loff, int lincr, void* rout, int roff, int rincr) {
	short* left = (short*)lout;
	short* right = (short*)rout;
	if (!synth || len <= 0) return FLUID_FAILED;

	/* mkxp-z asks for one interleaved stereo buffer: left at 0 step 2, right at 1 step 2. */
	if (synth->sf && lout == rout && loff == 0 && roff == 1 && lincr == 2 && rincr == 2) {
		tsf_render_short(synth->sf, left, len, 0);
		return FLUID_OK;
	}

	short* tmp = calloc((size_t)len * 2, sizeof(short));
	if (!tmp) return FLUID_FAILED;
	if (synth->sf) tsf_render_short(synth->sf, tmp, len, 0);
	for (int i = 0; i < len; i++) {
		left[loff + i * lincr] = tmp[i * 2];
		right[roff + i * rincr] = tmp[i * 2 + 1];
	}
	free(tmp);
	return FLUID_OK;
}

#define ACTIVE(synth) ((synth) && (synth)->sf)

int fluid_synth_noteon(fluid_synth_t* synth, int chan, int key, int vel) {
	if (!ACTIVE(synth)) return FLUID_FAILED;
	if (vel <= 0) {
		tsf_channel_note_off(synth->sf, chan, key);
		return FLUID_OK;
	}
	tsf_channel_note_on(synth->sf, chan, key, vel / 127.0f);
	return FLUID_OK;
}

int fluid_synth_noteoff(fluid_synth_t* synth, int chan, int key) {
	if (!ACTIVE(synth)) return FLUID_FAILED;
	tsf_channel_note_off(synth->sf, chan, key);
	return FLUID_OK;
}

int fluid_synth_cc(fluid_synth_t* synth, int chan, int ctrl, int val) {
	if (!ACTIVE(synth)) return FLUID_FAILED;
	tsf_channel_midi_control(synth->sf, chan, ctrl, val);
	return FLUID_OK;
}

int fluid_synth_program_change(fluid_synth_t* synth, int chan, int program) {
	if (!ACTIVE(synth)) return FLUID_FAILED;
	tsf_channel_set_presetnumber(synth->sf, chan, program, chan == 9);
	return FLUID_OK;
}

int fluid_synth_pitch_bend(fluid_synth_t* synth, int chan, int val) {
	if (!ACTIVE(synth)) return FLUID_FAILED;
	tsf_channel_set_pitchwheel(synth->sf, chan, val);
	return FLUID_OK;
}

int fluid_synth_channel_pressure(fluid_synth_t* synth, int chan, int val) {
	(void)synth; (void)chan; (void)val; /* aftertouch: not supported by TinySoundFont */
	return FLUID_OK;
}
