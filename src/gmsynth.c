/* gmsynth -- general-midi fluidsynth
 *
 * Copyright (C) 2016,2017 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>

#define GFS_URN "http://gareus.org/oss/lv2/gmsynth"

#ifdef HAVE_LV2_1_18_6
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/core/lv2.h>
#include <lv2/log/logger.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#else
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/log/logger.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#endif

#include "midnam_lv2.h"
#include "bankpatch_lv2.h"

#include "fluidsynth.h"

#ifdef _WIN32
#define PATH_SEP "\\"
#else
#define PATH_SEP "/"
#endif

enum {
	GFS_PORT_CONTROL = 0,
	GFS_PORT_OUT_L,
	GFS_PORT_OUT_R,
	GFS_PORT_LAST
};

enum {
	CMD_APPLY    = 0,
	CMD_FREE     = 1,
};

struct Program {
	char* name;
	int program;
	struct Program* next;
};

struct Bank {
	int bank;
	struct Program* pgm;
	struct Bank* next;
};

static char* xml_escape (const char *s)
{
	char* rv = strdup (s);
	char* tmp;
	while ((tmp = strchr (rv, '"'))) {
		*tmp = '\'';
	}

	tmp = rv;
	int cnt = 0;
	while (*tmp && (tmp = strchr (tmp, '&'))) {
		++cnt;
		++tmp;
	}
	if (cnt == 0) {
		return rv;
	}

	rv = realloc (rv, 1 + strlen (rv) + 4 * cnt);
	tmp = rv;
	while (*tmp && (tmp = strchr (tmp, '&'))) {
		memmove (tmp + 4, tmp, strlen (tmp) + 1);
		strncpy (tmp, "&amp;", 5);
		++tmp;
	}
	return rv;
}

static struct Program* get_pgmlist (struct Bank* b, int bank)
{
	while (b->next) {
		if (b->bank == bank) {
			return b->pgm;
		}
		b = b->next;
	}
	b->next = calloc (1, sizeof (struct Bank));
	b->bank = bank;
	b->pgm = calloc (1, sizeof (struct Program));
	return b->pgm;
}

static void add_program (struct Program* p, const char* n, int program)
{
	while (p->next) {
		p = p->next;
	}
	p->next = calloc (1, sizeof (struct Program));
	p->name = xml_escape (n);
	p->program = program;
}

static void free_programs (struct Program* p) {
	while (p) {
		free (p->name);
		struct Program* c = p;
		p = p->next;
		free (c);
	}
}

static void clear_banks (struct Bank* b) {
	while (b) {
		free_programs (b->pgm);
		struct Bank* c = b;
		b = b->next;
		free (c);
	}
}

static bool
is_drum_program (int bank, uint8_t pgm)
{
	if (bank == 128 || bank == 120) {
		return true;
	}
	return false;
}

typedef struct {
	/* ports */
	const LV2_Atom_Sequence* control;

	float* p_ports[GFS_PORT_LAST];

	/* fluid synth */
	fluid_settings_t* settings;
	fluid_synth_t*    synth;
	int               synthId;

	/* LV2 URID */
	LV2_URID midi_MidiEvent;

	/* LV2 extensions */
	LV2_Log_Log*         log;
	LV2_Log_Logger       logger;

	/* midnam/presets */
	LV2_Midnam*          midnam;
	struct Bank*         presets;
	pthread_mutex_t      bp_lock;

	/* bank/patch notifications */
	LV2_BankPatch*       bankpatch;

	/* state */
	bool panic;
	bool send_bankpgm;

	uint8_t last_bank_lsb[16];
	uint8_t last_bank_msb[16];
	uint8_t last_program[16];
	bool    is_drums[16];

	fluid_midi_event_t* fmidi_event;

} GFSSynth;

/* *****************************************************************************
 * helpers
 */

static bool
load_sf2 (GFSSynth* self, const char* fn)
{
	const int synth_id = fluid_synth_sfload (self->synth, fn, 1);

	pthread_mutex_lock (&self->bp_lock);
	clear_banks (self->presets);
	self->presets = calloc (1, sizeof (struct Bank));
	pthread_mutex_unlock (&self->bp_lock);

	if (synth_id == FLUID_FAILED) {
		return false;
	}

	fluid_sfont_t* const sfont = fluid_synth_get_sfont_by_id (self->synth, synth_id);
	if (!sfont) {
		return false;
	}

	int chn;
	fluid_preset_t* preset;
	fluid_sfont_iteration_start (sfont);
	pthread_mutex_lock (&self->bp_lock);
	for (chn = 0; (preset = fluid_sfont_iteration_next (sfont)); ++chn) {
		int bank         = fluid_preset_get_banknum (preset);
		int pgm          = fluid_preset_get_num (preset);
		const char* name = fluid_preset_get_name (preset);
		if (chn < 16) {
			fluid_synth_program_select (self->synth, chn, synth_id, bank, pgm);
			self->last_bank_msb[chn] = bank >> 7;
			self->last_bank_lsb[chn] = bank & 127;
			self->last_program[chn]  = pgm;

		}
		if (0 == strcmp (name, "Standard 1")) {
			/* set default drumkit to channel 10 */
			fluid_synth_program_select (self->synth, 9, synth_id, bank, pgm);
			self->last_bank_msb[9] = bank >> 7;
			self->last_bank_lsb[9] = bank & 127;
			self->last_program[9]  = pgm;
			self->is_drums[9]      = true;
		}

		add_program (get_pgmlist (self->presets, bank), name, pgm);
	}
	pthread_mutex_unlock (&self->bp_lock);

	if (chn == 0) {
		return false;
	}

	return true;
}

static int file_exists (const char *filename) {
	struct stat s;
	if (!filename || strlen(filename) < 1) return 0;
	int result= stat (filename, &s);
	if (result != 0) return 0; /* stat() failed */
	if (S_ISREG(s.st_mode)) return 1; /* is a regular file - ok */
	return 0;
}

/* *****************************************************************************
 * LV2 Plugin
 */

static LV2_Handle
instantiate (const LV2_Descriptor*     descriptor,
             double                    rate,
             const char*               bundle_path,
             const LV2_Feature* const* features)
{
	GFSSynth* self = (GFSSynth*)calloc (1, sizeof (GFSSynth));

	if (!self || !bundle_path) {
		return NULL;
	}

	LV2_URID_Map* map = NULL;

	for (int i=0; features[i] != NULL; ++i) {
		if (!strcmp (features[i]->URI, LV2_URID__map)) {
			map = (LV2_URID_Map*)features[i]->data;
		} else if (!strcmp (features[i]->URI, LV2_LOG__log)) {
			self->log = (LV2_Log_Log*)features[i]->data;
		} else if (!strcmp (features[i]->URI, LV2_MIDNAM__update)) {
			self->midnam = (LV2_Midnam*)features[i]->data;
		} else if (!strcmp (features[i]->URI, LV2_BANKPATCH__notify)) {
			self->bankpatch = (LV2_BankPatch*)features[i]->data;
		}
	}

	lv2_log_logger_init (&self->logger, map, self->log);

	if (!map) {
		lv2_log_error (&self->logger, "gmsynth.lv2: Host does not support urid:map\n");
		free (self);
		return NULL;
	}

	char sf2_file_path[1024];
	snprintf (sf2_file_path, sizeof (sf2_file_path), "%s" PATH_SEP "GeneralUser_LV2.sf2", bundle_path);
	sf2_file_path[sizeof(sf2_file_path) - 1] = '\0';

	if (!file_exists (sf2_file_path)) {
		lv2_log_error (&self->logger, "gmsynth.lv2: Cannot find soundfont: '%s'\n", sf2_file_path);
		free (self);
		return NULL;
	}

	/* initialize fluid synth */
	self->settings = new_fluid_settings ();

	if (!self->settings) {
		lv2_log_error (&self->logger, "gmsynth.lv2: cannot allocate Fluid Settings\n");
		free (self);
		return NULL;
	}

	fluid_settings_setnum (self->settings, "synth.sample-rate", rate);
	fluid_settings_setint (self->settings, "synth.threadsafe-api", 0);
	fluid_settings_setstr (self->settings, "synth.midi-bank-select", "mma");
	fluid_settings_setint (self->settings, "synth.audio-channels", 1); // stereo pairs

	self->synth = new_fluid_synth (self->settings);

	if (!self->synth) {
		lv2_log_error (&self->logger, "gmsynth.lv2: cannot allocate Fluid Synth\n");
		delete_fluid_settings (self->settings);
		free (self);
		return NULL;
	}

	fluid_synth_set_gain (self->synth, 1.0f);
	fluid_synth_set_polyphony (self->synth, 256);
	fluid_synth_set_sample_rate (self->synth, (float)rate);

	self->fmidi_event = new_fluid_midi_event ();

	if (!self->fmidi_event) {
		lv2_log_error (&self->logger, "gmsynth.lv2: cannot allocate Fluid Event\n");
		delete_fluid_synth (self->synth);
		delete_fluid_settings (self->settings);
		free (self);
		return NULL;
	}

	/* initialize plugin state */

	pthread_mutex_init (&self->bp_lock, NULL);
	self->presets = calloc (1, sizeof (struct Bank));
	self->midi_MidiEvent = map->map (map->handle, LV2_MIDI__MidiEvent);

	self->panic = false;
	self->send_bankpgm = true;

	for (uint8_t chn = 0; chn < 16; ++chn) {
		self->last_program[chn] = 255;
		self->is_drums[chn] = false;
	}

	/* load .sf2 */
	if (load_sf2 (self, sf2_file_path)) {
		fluid_synth_all_notes_off (self->synth, -1);
		fluid_synth_all_sounds_off (self->synth, -1);
		self->panic = false;
		/* boostrap synth engine. */
		float b[1024];
		fluid_synth_write_float (self->synth, 1024, b, 0, 1, b, 0, 1);
	} else {
		lv2_log_error (&self->logger, "gmsynth.lv2: cannot load SoundFont\n");
		delete_fluid_synth (self->synth);
		delete_fluid_settings (self->settings);
		free (self->presets);
		free (self);
		return NULL;
	}

	return (LV2_Handle)self;
}

static void
connect_port (LV2_Handle instance,
              uint32_t   port,
              void*      data)
{
	GFSSynth* self = (GFSSynth*)instance;

	switch (port) {
		case GFS_PORT_CONTROL:
			self->control = (const LV2_Atom_Sequence*)data;
			break;
		default:
			if (port < GFS_PORT_LAST) {
				self->p_ports[port] = (float*)data;
			}
			break;
	}
}

static void
deactivate (LV2_Handle instance)
{
	GFSSynth* self = (GFSSynth*)instance;
	self->panic = true;
}

static void
run (LV2_Handle instance, uint32_t n_samples)
{
	GFSSynth* self = (GFSSynth*)instance;

	if (!self->control) {
		return;
	}

	if (self->panic) {
		fluid_synth_all_notes_off (self->synth, -1);
		fluid_synth_all_sounds_off (self->synth, -1);
		self->panic = false;
	}

	uint32_t offset = 0;

	LV2_ATOM_SEQUENCE_FOREACH (self->control, ev) {
		if (ev->body.type == self->midi_MidiEvent) {
			if (ev->body.size > 3 || ev->time.frames >= n_samples) {
				continue;
			}

			if (ev->time.frames > offset) {
				fluid_synth_write_float (
						self->synth,
						ev->time.frames - offset,
						&self->p_ports[GFS_PORT_OUT_L][offset], 0, 1,
						&self->p_ports[GFS_PORT_OUT_R][offset], 0, 1);
			}

			offset = ev->time.frames;

			const uint8_t* const data = (const uint8_t*)(ev + 1);
			fluid_midi_event_set_type (self->fmidi_event, data[0] & 0xf0);
			fluid_midi_event_set_channel (self->fmidi_event, data[0] & 0x0f);
			if (ev->body.size > 1) {
				fluid_midi_event_set_key (self->fmidi_event, data[1]);
			}
			if (ev->body.size == 2 && 0xc0 /* Pgm */ == fluid_midi_event_get_type (self->fmidi_event)) {
				int chn = fluid_midi_event_get_channel (self->fmidi_event);
				self->last_program[chn] = data[1];
				if (self->bankpatch) {
					self->bankpatch->notify (self->bankpatch->handle, chn,
							(self->last_bank_msb[chn] << 7) | self->last_bank_lsb[chn], data[1]);
				}

				bool is_drum = is_drum_program ((self->last_bank_msb[chn] << 7) | self->last_bank_lsb[chn], data[1]);
				if (self->midnam && self->is_drums[chn] != is_drum) {
					self->midnam->update (self->midnam->handle);
				}
				self->is_drums[chn] = is_drum;
			}
			if (ev->body.size > 2) {
				if (0xe0 /*PITCH_BEND*/ == fluid_midi_event_get_type (self->fmidi_event)) {
					fluid_midi_event_set_value (self->fmidi_event, 0);
					fluid_midi_event_set_pitch (self->fmidi_event, ((data[2] & 0x7f) << 7) | (data[1] & 0x7f));
				} else {
					fluid_midi_event_set_value (self->fmidi_event, data[2]);
				}
				if (0xb0 /* CC */ == fluid_midi_event_get_type (self->fmidi_event)) {
					int chn = fluid_midi_event_get_channel (self->fmidi_event);
					if (data[1] == 0x00) { self->last_bank_msb[chn] = data[2]; }
					if (data[1] == 0x20) { self->last_bank_lsb[chn] = data[2]; }
				}
			}

			fluid_synth_handle_midi_event (self->synth, self->fmidi_event);
		}
	}

	if (n_samples > offset) {
		fluid_synth_write_float (
				self->synth,
				n_samples - offset,
				&self->p_ports[GFS_PORT_OUT_L][offset], 0, 1,
				&self->p_ports[GFS_PORT_OUT_R][offset], 0, 1);
	}

	if (self->send_bankpgm && self->bankpatch) {
		self->send_bankpgm = false;
		for (uint8_t chn = 0; chn < 16; ++chn) {
			self->bankpatch->notify (self->bankpatch->handle, chn,
					(self->last_bank_msb[chn] << 7) | self->last_bank_lsb[chn],
					self->last_program[chn] > 127 ? 255 : self->last_program[chn]);
		}
	}
}

static void cleanup (LV2_Handle instance)
{
	GFSSynth* self = (GFSSynth*)instance;
	delete_fluid_synth (self->synth);
	delete_fluid_settings (self->settings);
	delete_fluid_midi_event (self->fmidi_event);
	clear_banks (self->presets);
	pthread_mutex_destroy (&self->bp_lock);
	free (self);
}

/* *****************************************************************************
 * LV2 Extensions
 */

static char*
mn_file (LV2_Handle instance)
{
	GFSSynth* self = (GFSSynth*)instance;
	char* rv = NULL;
	char tmp[1024];

	rv = (char*) calloc (1, sizeof (char));

#define pf(...)                                              \
	do {                                                       \
	  snprintf (tmp, sizeof(tmp), __VA_ARGS__);                \
	  tmp[sizeof(tmp) - 1] = '\0';                             \
	  rv = (char*)realloc (rv, strlen (rv) + strlen(tmp) + 1); \
	  strcat (rv, tmp);                                        \
	} while (0)

	pf ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<!DOCTYPE MIDINameDocument PUBLIC \"-//MIDI Manufacturers Association//DTD MIDINameDocument 1.0//EN\" \"http://dev.midi.org/dtds/MIDINameDocument10.dtd\">\n"
			"<MIDINameDocument>\n"
			"  <Author/>\n"
			"  <MasterDeviceNames>\n"
			"    <Manufacturer>Ardour Foundation</Manufacturer>\n"
			"    <Model>GM Synth LV2:%p</Model>\n", (void*) self);


	pf ("    <CustomDeviceMode Name=\"Default\">\n");
	pf ("      <ChannelNameSetAssignments>\n");
	for (int c = 0; c < 16; ++c) {
		if (self->is_drums[c]) {
			pf ("        <ChannelNameSetAssign Channel=\"%d\" NameSet=\"GM Drums\"/>\n", c + 1);
		} else {
			pf ("        <ChannelNameSetAssign Channel=\"%d\" NameSet=\"GM Notes\"/>\n", c + 1);
		}
	}
	pf ("      </ChannelNameSetAssignments>\n");
	pf ("    </CustomDeviceMode>\n");

	/* ********************/

	pf ("    <ChannelNameSet Name=\"GM Notes\">\n");
	pf ("      <AvailableForChannels>\n");
	for (int c = 0; c < 16; ++c) {
		if (!self->is_drums[c]) {
			pf ("        <AvailableChannel Channel=\"%d\" Available=\"true\"/>\n", c + 1);
		}
	}
	pf ("      </AvailableForChannels>\n");
	pf ("      <UsesControlNameList Name=\"Controls\"/>\n");

	pthread_mutex_lock (&self->bp_lock);
	struct Bank* b = self->presets;
	while (b->next) {
		struct Program* p = b->pgm;
		pf ("      <PatchBank Name=\"Patch Bank %d\">\n", b->bank);
		pf ("        <MIDICommands>\n");
		pf ("            <ControlChange Control=\"0\" Value=\"%d\"/>\n", (b->bank >> 7) & 127);
		pf ("            <ControlChange Control=\"32\" Value=\"%d\"/>\n", b->bank & 127);
		pf ("        </MIDICommands>\n");
		if (p->next) {
			pf ("      <UsesPatchNameList Name=\"Patch Bank Names %d\"/>\n", b->bank);
		}
		pf ("      </PatchBank>\n");
		b = b->next;
	}
	pf ("    </ChannelNameSet>\n");

	/* ********************/

	pf ("    <ChannelNameSet Name=\"GM Drums\">\n");
	pf ("      <AvailableForChannels>\n");
	for (int c = 0; c < 16; ++c) {
		if (self->is_drums[c]) {
			pf ("        <AvailableChannel Channel=\"%d\" Available=\"true\"/>\n", c + 1);
		}
	}
	pf ("      </AvailableForChannels>\n");
	pf ("      <UsesControlNameList Name=\"Controls\"/>\n");
	pf ("      <UsesNoteNameList Name=\"General MIDI Drums\"/>\n");

	b = self->presets;
	while (b->next) {
		struct Program* p = b->pgm;
		pf ("      <PatchBank Name=\"Patch Bank %d\">\n", b->bank);
		pf ("        <MIDICommands>\n");
		pf ("            <ControlChange Control=\"0\" Value=\"%d\"/>\n", (b->bank >> 7) & 127);
		pf ("            <ControlChange Control=\"32\" Value=\"%d\"/>\n", b->bank & 127);
		pf ("        </MIDICommands>\n");
		if (p->next) {
			pf ("      <UsesPatchNameList Name=\"Patch Bank Names %d\"/>\n", b->bank);
		}
		pf ("      </PatchBank>\n");
		b = b->next;
	}
	pf ("    </ChannelNameSet>\n");

	/* ********************/

	b = self->presets;
	while (b->next) {
		struct Program* p = b->pgm;
		if (!p->next) {
			b = b->next;
			continue;
		}
		pf ("    <PatchNameList Name=\"Patch Bank Names %d\">\n", b->bank);
		int n = 0;
		while (p->next) {
			pf ("      <Patch Number=\"%d\" Name=\"%s\" ProgramChange=\"%d\"/>\n", n, p->name, p->program);
			p = p->next;
			++n;
		}
		pf ("    </PatchNameList>\n");
		b = b->next;
	}
	pthread_mutex_unlock (&self->bp_lock);


	pf ("    <ControlNameList Name=\"Controls\">\n");
	pf ("       <Control Type=\"7bit\" Number=\"1\" Name=\"Modulation\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"2\" Name=\"Breath\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"5\" Name=\"Portamento Time\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"7\" Name=\"Channel Volume\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"8\" Name=\"Stereo Balance\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"10\" Name=\"Pan\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"11\" Name=\"Expression\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"37\" Name=\"Portamento Time (Fine)\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"64\" Name=\"Sustain On/Off\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"65\" Name=\"Portamento On/Off\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"66\" Name=\"Sostenuto On/Off\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"68\" Name=\"Legato On/Off\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"91\" Name=\"Reverb\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"93\" Name=\"Chorus\"/>\n");
	pf ("    </ControlNameList>\n");

	pf ("    <NoteNameList Name=\"General MIDI Drums\">\n");
	pf ("      <Note Number=\"35\" Name=\"Bass Drum 2\"/>\n");
	pf ("      <Note Number=\"36\" Name=\"Bass Drum 1\"/>\n");
	pf ("      <Note Number=\"37\" Name=\"Side Stick/Rimshot\"/>\n");
	pf ("      <Note Number=\"38\" Name=\"Snare Drum 1\"/>\n");
	pf ("      <Note Number=\"39\" Name=\"Hand Clap\"/>\n");
	pf ("      <Note Number=\"40\" Name=\"Snare Drum 2\"/>\n");
	pf ("      <Note Number=\"41\" Name=\"Low Tom 2\"/>\n");
	pf ("      <Note Number=\"42\" Name=\"Closed Hi-hat\"/>\n");
	pf ("      <Note Number=\"43\" Name=\"Low Tom 1\"/>\n");
	pf ("      <Note Number=\"44\" Name=\"Pedal Hi-hat\"/>\n");
	pf ("      <Note Number=\"45\" Name=\"Mid Tom 2\"/>\n");
	pf ("      <Note Number=\"46\" Name=\"Open Hi-hat\"/>\n");
	pf ("      <Note Number=\"47\" Name=\"Mid Tom 1\"/>\n");
	pf ("      <Note Number=\"48\" Name=\"High Tom 2\"/>\n");
	pf ("      <Note Number=\"49\" Name=\"Crash Cymbal 1\"/>\n");
	pf ("      <Note Number=\"50\" Name=\"High Tom 1\"/>\n");
	pf ("      <Note Number=\"51\" Name=\"Ride Cymbal 1\"/>\n");
	pf ("      <Note Number=\"52\" Name=\"Chinese Cymbal\"/>\n");
	pf ("      <Note Number=\"53\" Name=\"Ride Bell\"/>\n");
	pf ("      <Note Number=\"54\" Name=\"Tambourine\"/>\n");
	pf ("      <Note Number=\"55\" Name=\"Splash Cymbal\"/>\n");
	pf ("      <Note Number=\"56\" Name=\"Cowbell\"/>\n");
	pf ("      <Note Number=\"57\" Name=\"Crash Cymbal 2\"/>\n");
	pf ("      <Note Number=\"58\" Name=\"Vibra Slap\"/>\n");
	pf ("      <Note Number=\"59\" Name=\"Ride Cymbal 2\"/>\n");
	pf ("      <Note Number=\"60\" Name=\"High Bongo\"/>\n");
	pf ("      <Note Number=\"61\" Name=\"Low Bongo\"/>\n");
	pf ("      <Note Number=\"62\" Name=\"Mute High Conga\"/>\n");
	pf ("      <Note Number=\"63\" Name=\"Open High Conga\"/>\n");
	pf ("      <Note Number=\"64\" Name=\"Low Conga\"/>\n");
	pf ("      <Note Number=\"65\" Name=\"High Timbale\"/>\n");
	pf ("      <Note Number=\"66\" Name=\"Low Timbale\"/>\n");
	pf ("      <Note Number=\"67\" Name=\"High Agogô\"/>\n");
	pf ("      <Note Number=\"68\" Name=\"Low Agogô\"/>\n");
	pf ("      <Note Number=\"69\" Name=\"Cabasa\"/>\n");
	pf ("      <Note Number=\"70\" Name=\"Maracas\"/>\n");
	pf ("      <Note Number=\"71\" Name=\"Short Whistle\"/>\n");
	pf ("      <Note Number=\"72\" Name=\"Long Whistle\"/>\n");
	pf ("      <Note Number=\"73\" Name=\"Short Güiro\"/>\n");
	pf ("      <Note Number=\"74\" Name=\"Long Güiro\"/>\n");
	pf ("      <Note Number=\"75\" Name=\"Claves\"/>\n");
	pf ("      <Note Number=\"76\" Name=\"High Wood Block\"/>\n");
	pf ("      <Note Number=\"77\" Name=\"Low Wood Block\"/>\n");
	pf ("      <Note Number=\"78\" Name=\"Mute Cuíca\"/>\n");
	pf ("      <Note Number=\"79\" Name=\"Open Cuíca\"/>\n");
	pf ("      <Note Number=\"80\" Name=\"Mute Triangle\"/>\n");
	pf ("      <Note Number=\"81\" Name=\"Open Triangle\"/>\n");
	pf ("    </NoteNameList>\n");

	pf (
			"  </MasterDeviceNames>\n"
			"</MIDINameDocument>"
		 );

	//printf("-----\n%s\n------\n", rv);
	return rv;
}

static char*
mn_model (LV2_Handle instance)
{
	GFSSynth* self = (GFSSynth*)instance;
	char* rv = (char*) malloc (64 * sizeof (char));
	snprintf (rv, 64, "GM Synth LV2:%p", (void*) self);
	rv[63] = 0;
	return rv;
}

static void
mn_free (char* v)
{
	free (v);
}

static const void*
extension_data (const char* uri)
{
	static const LV2_Midnam_Interface midnam = { mn_file, mn_model, mn_free };
	if (!strcmp (uri, LV2_MIDNAM__interface)) {
		return &midnam;
	}
	return NULL;
}

static const LV2_Descriptor descriptor = {
	GFS_URN,
	instantiate,
	connect_port,
	NULL,
	run,
	deactivate,
	cleanup,
	extension_data
};

#undef LV2_SYMBOL_EXPORT
#ifdef _WIN32
#    define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
#    define LV2_SYMBOL_EXPORT  __attribute__ ((visibility ("default")))
#endif
LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor (uint32_t index)
{
	switch (index) {
	case 0:
		return &descriptor;
	default:
		return NULL;
	}
}
