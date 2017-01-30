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

#include <lv2/lv2plug.in/ns/lv2core/lv2.h>
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/util.h>
#include <lv2/lv2plug.in/ns/ext/log/logger.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>

#include "midnam_lv2.h"

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

static char* xml_escape (char *s)
{
	char* rv = strdup (s);
	char* tmp;
	while ((tmp = strchr (rv, '"'))) {
		*tmp = '\'';
	}
	// '&' -> "&amp;" // not needed with current .sf2
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

static void add_program (struct Program* p, char* n, int program)
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
	LV2_Worker_Schedule* schedule;

	/* midnam/presets */
	LV2_Midnam*          midnam;
	struct Bank*         presets;
	pthread_mutex_t      bp_lock;

	/* state */
	bool panic;
	bool initialized;
	bool inform_ui;

	char queue_sf2_file_path[1024];
	bool reinit_in_progress; // set in run, cleared in work_response
	bool queue_reinit; // set in restore, cleared in work_response

	uint8_t last_bank_lsb;
	uint8_t last_bank_msb;

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
	fluid_preset_t preset;
	sfont->iteration_start (sfont);
	pthread_mutex_lock (&self->bp_lock);
	for (chn = 0; sfont->iteration_next (sfont, &preset); ++chn) {
		if (chn < 16) {
			fluid_synth_program_select (self->synth, chn, synth_id,
					preset.get_banknum (&preset), preset.get_num (&preset));
		}

		add_program (get_pgmlist (self->presets, preset.get_banknum (&preset)),
					preset.get_name (&preset),
					preset.get_num (&preset));
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
		} else if (!strcmp (features[i]->URI, LV2_WORKER__schedule)) {
			self->schedule = (LV2_Worker_Schedule*)features[i]->data;
		} else if (!strcmp (features[i]->URI, LV2_MIDNAM__update)) {
			self->midnam = (LV2_Midnam*)features[i]->data;
		}
	}

	lv2_log_logger_init (&self->logger, map, self->log);

	if (!map) {
		lv2_log_error (&self->logger, "gmsynth.lv2: Host does not support urid:map\n");
		free (self);
		return NULL;
	}

	if (!self->schedule) {
		lv2_log_error (&self->logger, "gmsynth.lv2: Host does not support worker:schedule\n");
		free (self);
		return NULL;
	}

	snprintf (self->queue_sf2_file_path, sizeof (self->queue_sf2_file_path), "%s" PATH_SEP "GeneralUser_LV2.sf2", bundle_path);
	self->queue_sf2_file_path[sizeof(self->queue_sf2_file_path) - 1] = '\0';

	if (!file_exists (self->queue_sf2_file_path)) {
		lv2_log_error (&self->logger, "gmsynth.lv2: Cannot find soundfont: '%s'\n", self->queue_sf2_file_path);
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
	fluid_settings_setint (self->settings, "synth.parallel-render", 1);
	fluid_settings_setint (self->settings, "synth.threadsafe-api", 0);
	fluid_settings_setint (self->settings, "synth.audio-channels", 1); // stereo pairs

	self->synth = new_fluid_synth (self->settings);

	if (!self->synth) {
		lv2_log_error (&self->logger, "gmsynth.lv2: cannot allocate Fluid Synth\n");
		delete_fluid_settings (self->settings);
		free (self);
		return NULL;
	}

	fluid_synth_set_gain (self->synth, 1.0f);
	fluid_synth_set_polyphony (self->synth, 32);
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
	self->inform_ui = false;
	self->presets = calloc (1, sizeof (struct Bank));

	self->panic = false;
	self->initialized = false;
	self->reinit_in_progress = false;
	self->queue_reinit = true;

	self->midi_MidiEvent = map->map (map->handle, LV2_MIDI__MidiEvent);

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

	if (!self->initialized || self->reinit_in_progress) {
		memset (self->p_ports[GFS_PORT_OUT_L], 0, n_samples * sizeof (float));
		memset (self->p_ports[GFS_PORT_OUT_R], 0, n_samples * sizeof (float));
	} else if (self->panic) {
		fluid_synth_all_notes_off (self->synth, -1);
		fluid_synth_all_sounds_off (self->synth, -1);
		self->panic = false;
	}

	uint32_t offset = 0;

	LV2_ATOM_SEQUENCE_FOREACH (self->control, ev) {
		if (ev->body.type == self->midi_MidiEvent && self->initialized && !self->reinit_in_progress) {
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
			if (ev->body.size > 2) {
				if (0xe0 /*PITCH_BEND*/ == fluid_midi_event_get_type (self->fmidi_event)) {
					fluid_midi_event_set_value (self->fmidi_event, 0);
					fluid_midi_event_set_pitch (self->fmidi_event, ((data[2] & 0x7f) << 7) | (data[1] & 0x7f));
				} else {
					fluid_midi_event_set_value (self->fmidi_event, data[2]);
				}
				if (0xb0 /* CC */ == fluid_midi_event_get_type (self->fmidi_event)) {
					if (data[1] == 0x00) { self->last_bank_msb = data[2]; }
					if (data[1] == 0x20) { self->last_bank_lsb = data[2]; }
				}
			}

			fluid_synth_handle_midi_event (self->synth, self->fmidi_event);
		}
	}

	if (self->queue_reinit && !self->reinit_in_progress) {
		self->reinit_in_progress = true;
		int magic = 0x08154711;
		self->schedule->schedule_work (self->schedule->handle, sizeof (int), &magic);
	}

	if (self->inform_ui && self->midnam) {
		self->inform_ui = false;
		self->midnam->update (self->midnam->handle);
	}

	if (n_samples > offset && self->initialized && !self->reinit_in_progress) {
		fluid_synth_write_float (
				self->synth,
				n_samples - offset,
				&self->p_ports[GFS_PORT_OUT_L][offset], 0, 1,
				&self->p_ports[GFS_PORT_OUT_R][offset], 0, 1);
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

static LV2_Worker_Status
work (LV2_Handle                  instance,
      LV2_Worker_Respond_Function respond,
      LV2_Worker_Respond_Handle   handle,
      uint32_t                    size,
      const void*                 data)
{
	GFSSynth* self = (GFSSynth*)instance;

	if (size != sizeof (int)) {
		return LV2_WORKER_ERR_UNKNOWN;
	}
	int magic = *((const int*)data);
	if (magic != 0x08154711) {
		return LV2_WORKER_ERR_UNKNOWN;
	}

	self->initialized = load_sf2 (self, self->queue_sf2_file_path);

	if (self->initialized) {
		fluid_synth_all_notes_off (self->synth, -1);
		fluid_synth_all_sounds_off (self->synth, -1);
		self->panic = false;
		/* boostrap synth engine. */
		float b[1024];
		fluid_synth_write_float (self->synth, 1024, b, 0, 1, b, 0, 1);
	}

	respond (handle, 1, "");
	return LV2_WORKER_SUCCESS;
}

static LV2_Worker_Status
work_response (LV2_Handle  instance,
               uint32_t    size,
               const void* data)
{
	GFSSynth* self = (GFSSynth*)instance;
	self->reinit_in_progress = false;
	self->inform_ui = true;
	self->queue_reinit = false;
	return LV2_WORKER_SUCCESS;
}

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
			"    <Model>%s:%p</Model>\n", GFS_URN, (void*) self);


	pf ("    <CustomDeviceMode Name=\"Default\">\n");
	pf ("      <ChannelNameSetAssignments>\n");
	for (int c = 0; c < 16; ++c) {
		pf ("        <ChannelNameSetAssign Channel=\"%d\" NameSet=\"Presets\"/>\n", c + 1);
	}
	pf ("      </ChannelNameSetAssignments>\n");
	pf ("    </CustomDeviceMode>\n");

	// TODO collect used banks, std::set<> would be a nice here

	pf ("    <ChannelNameSet Name=\"Presets\">\n");
	pf ("      <AvailableForChannels>\n");
	for (int c = 0; c < 16; ++c) {
		pf ("        <AvailableChannel Channel=\"%d\" Available=\"true\"/>\n", c + 1);
	}
	pf ("      </AvailableForChannels>\n");
	pf ("      <UsesControlNameList Name=\"Controls\"/>\n");

	pthread_mutex_lock (&self->bp_lock);
	struct Bank* b = self->presets;
	while (b->next) {
		struct Program* p = b->pgm;
		pf ("      <PatchBank Name=\"Patch Bank %d\">\n", b->bank);
		if (p->next) {
			pf ("        <PatchNameList>\n");
			int n = 0;
			while (p->next) {
				pf ("      <Patch Number=\"%d\" Name=\"%s\" ProgramChange=\"%d\"/>\n",
						n, p->name, p->program);
				p = p->next;
				++n;
			}
			pf ("        </PatchNameList>\n");
		}
		pf ("      </PatchBank>\n");
		b = b->next;
	}
	pthread_mutex_unlock (&self->bp_lock);

	pf ("    </ChannelNameSet>\n");

	pf ("    <ControlNameList Name=\"Controls\">\n");
	pf ("       <Control Type=\"7bit\" Number=\"7\" Name=\"Channel Volume\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"10\" Name=\"Pan\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"39\" Name=\"Channel Volume (Fine)\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"42\" Name=\"Pan (Fine)\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"64\" Name=\"Damper Pedal (Sustain)\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"66\" Name=\"Sostenuto\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"91\" Name=\"Reverb\"/>\n");
	pf ("       <Control Type=\"7bit\" Number=\"93\" Name=\"Chorus\"/>\n");
	pf ("    </ControlNameList>\n");

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
	snprintf (rv, 64, "%s:%p", GFS_URN, (void*) self);
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
	static const LV2_Worker_Interface worker = { work, work_response, NULL };
	static const LV2_Midnam_Interface midnam = { mn_file, mn_model, mn_free };
	if (!strcmp (uri, LV2_WORKER__interface)) {
		return &worker;
	}
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
