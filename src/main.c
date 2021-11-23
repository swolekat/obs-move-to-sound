/*
obs-scale-to-sound
Copyright (C) 2021 Dimitris Papaioannou and Swolekat

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>

#define SOURCE_NAME "Move To Sound"

#define MTS_AUDSRC "MTS_AUDSRC"
#define MTS_MINLVL "MTS_MIN_LVL"
#define MTS_MINPER "MTS_MINPER"
#define MTS_MAXPER "MTS_MAXPER"
#define MTS_INVSCL "MTS_INVSCL"
#define MTS_SCALEW "MTS_SCALEW"
#define MTS_SCALEH "MTS_SCALEH"
#define MTS_STARTX "MTS_STARTX"
#define MTS_STARTY "MTS_STARTY"
#define MTS_ENDX "MTS_ENDX"
#define MTS_ENDY "MTS_ENDY"

OBS_DECLARE_MODULE()
const char *get_source_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return SOURCE_NAME;
}
struct move_to_sound_data {
	obs_source_t *context;
	obs_source_t *target;

	obs_property_t *sources_list;
	double minimum_audio_level;
	bool invert;
	long long min;
	long long max;
	bool scale_w;
	bool scale_h;

	uint32_t src_w;
	uint32_t src_h;

	long long min_w;
	long long min_h;
	long long max_w;
	long long max_h;

	float audio_level;

	gs_effect_t *mover;
	obs_source_t *audio_source;
};

static void calculate_audio_level(void *param, obs_source_t *source, const struct audio_data *data, bool muted)
{
	UNUSED_PARAMETER(source);

	struct move_to_sound_data *mtsf = param;

	if(muted) {
		mtsf->audio_level = mtsf->minimum_audio_level;
		return;
	}

	//Taken from libobs/obs-audio-controls.c volmeter_process_magnitude and slightly modified
	size_t nr_samples = data->frames;

	float *samples = (float *)data->data[0];
	if (!samples) {
		mtsf->audio_level = mtsf->minimum_audio_level;
		return;
	}
	float sum = 0.0;
	for (size_t i = 0; i < nr_samples; i++) {
		float sample = samples[i];
		sum += sample * sample;
	}

	mtsf->audio_level = obs_mul_to_db(sqrtf(sum / nr_samples));
}

static void *filter_create(obs_data_t *settings, obs_source_t *source)
{
	UNUSED_PARAMETER(settings);

	struct move_to_sound_data *mtsf = bzalloc(sizeof(*mtsf));
	mtsf->context = source;

	char *effect_file = obs_module_file("default_move.effect");
	obs_enter_graphics();
	mtsf->mover = gs_effect_create_from_file(effect_file, NULL);
	obs_leave_graphics();
	bfree(effect_file);

	return mtsf;
}
static void filter_update(void *data, obs_data_t *settings)
{
	struct move_to_sound_data *mtsf = data;

	obs_source_t *target = obs_filter_get_target(mtsf->context);
	mtsf->target = target;

	long long min = obs_data_get_int(settings, MTS_MINPER);
	long long max = obs_data_get_int(settings, MTS_MAXPER);

	uint32_t w = obs_source_get_base_width(target);
	uint32_t h = obs_source_get_base_height(target);

	mtsf->src_w = w;
	mtsf->src_h = h;

	if (max <= min) {
		obs_data_set_int(settings, MTS_MAXPER, min + 1);
		mtsf->max = min + 1;
	}
	else {
		mtsf->max = max;
	}
	mtsf->min = min;

	mtsf->invert = obs_data_get_bool(settings, MTS_INVSCL);

	mtsf->scale_w = obs_data_get_bool(settings, MTS_SCALEW);
	mtsf->scale_h = obs_data_get_bool(settings, MTS_SCALEH);

	mtsf->min_w = w * min / 100;
	mtsf->min_h = h * min / 100;
	mtsf->max_w = w * max / 100;
	mtsf->max_h = h * max / 100;

	double min_audio_level = obs_data_get_double(settings, MTS_MINLVL);
	mtsf->minimum_audio_level = min_audio_level;

	obs_source_t *audio_source = obs_get_source_by_name(obs_data_get_string(settings, MTS_AUDSRC));

	if (mtsf->audio_source != audio_source) {
		obs_source_remove_audio_capture_callback(mtsf->audio_source, calculate_audio_level, mtsf);
		obs_source_release(mtsf->audio_source);
		obs_source_add_audio_capture_callback(audio_source, calculate_audio_level, mtsf);

		mtsf->audio_source = audio_source;
	}
	else {
		obs_source_release(audio_source);
	}
}
static void filter_load(void *data, obs_data_t *settings)
{
	struct move_to_sound_data *mtsf = data;
	filter_update(mtsf, settings);
}

static bool enum_audio_sources(void *data, obs_source_t *source)
{
	struct move_to_sound_data *mtsf = data;
	uint32_t flags = obs_source_get_output_flags(source);

	if ((flags & OBS_SOURCE_AUDIO) != 0) {
		const char *name = obs_source_get_name(source);
		obs_property_list_add_string(mtsf->sources_list, name, name);
	}
	return true;
}
static obs_properties_t *filter_properties(void *data)
{
	struct move_to_sound_data *mtsf = data;

	obs_properties_t *p = obs_properties_create();

	obs_property_t *sources = obs_properties_add_list(p, MTS_AUDSRC, "Audio Source", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	mtsf->sources_list = sources;
	obs_enum_sources(enum_audio_sources, mtsf);

	obs_property_t *minlvl = obs_properties_add_float_slider(p, MTS_MINLVL, "Audio Threshold", -100, -0.5, 0.5);
	obs_property_float_set_suffix(minlvl, "dB");

	obs_property_t *minper = obs_properties_add_int_slider(p, MTS_MINPER, "Minimum Size", 0, 99, 1);
	obs_property_int_set_suffix(minper, "%");

	obs_property_t *maxper = obs_properties_add_int_slider(p, MTS_MAXPER, "Maximum Size", 1, 100, 1);
	obs_property_int_set_suffix(maxper, "%");

	obs_properties_add_bool(p, MTS_INVSCL, "Inverse Scaling");

	obs_properties_add_bool(p, MTS_SCALEW, "Scale Width");
	obs_properties_add_bool(p, MTS_SCALEH, "Scale Height");

	obs_properties_add_bool(p, MTS_STARTX, "Start X");
    obs_properties_add_bool(p, MTS_STARTY, "Start Y");

    obs_properties_add_bool(p, MTS_ENDX, "End X");
    obs_properties_add_bool(p, MTS_ENDY, "End Y");

	return p;
}
static void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, MTS_MINLVL, -40);

	obs_data_set_default_int(settings, MTS_MINPER, 90);
	obs_data_set_default_int(settings, MTS_MAXPER, 100);

	obs_data_set_default_bool(settings, MTS_INVSCL, false);

	obs_data_set_default_bool(settings, MTS_SCALEW, true);
	obs_data_set_default_bool(settings, MTS_SCALEH, true);
}

static void filter_destroy(void *data)
{
	struct move_to_sound_data *mtsf = data;

	obs_source_remove_audio_capture_callback(mtsf->audio_source, calculate_audio_level, mtsf);
	obs_source_release(mtsf->audio_source);

	obs_enter_graphics();
	gs_effect_destroy(mtsf->mover);
	obs_leave_graphics();

	bfree(mtsf);
}

static void target_update(void *data, float seconds) {
	UNUSED_PARAMETER(seconds);
	
	//!This should really be done using a signal but I could not get those working so here we are...
	struct move_to_sound_data *mtsf = data;

	obs_source_t *target = mtsf->target;

	uint32_t w = mtsf->src_w;
	uint32_t h = mtsf->src_h;

	uint32_t new_w = obs_source_get_base_width(target);
	uint32_t new_h = obs_source_get_base_height(target);

	if(new_w != w || new_h != h) {
		obs_data_t *settings = obs_source_get_settings(mtsf->context);
		filter_update(mtsf, settings);
		obs_data_release(settings);
	}
}
static void filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	struct move_to_sound_data *mtsf = data;

	uint32_t w = mtsf->src_w;
	uint32_t h = mtsf->src_h;

	uint32_t min_scale_percent = mtsf->min;
	uint32_t max_scale_percent = mtsf->max;

	double min_audio_level = mtsf->minimum_audio_level;
	double audio_level = mtsf->audio_level;

	if(min_audio_level >= 0) min_audio_level = -0.5f;
	double scale_percent = fabs(min_audio_level) - fabs(audio_level);

	//Scale the calculated from audio precentage down to the user-set range
	scale_percent = (scale_percent * (max_scale_percent - min_scale_percent)) / fabs(min_audio_level) + min_scale_percent;
	if(scale_percent < min_scale_percent || audio_level >= 0) scale_percent = min_scale_percent;

	if(mtsf->invert) scale_percent = min_scale_percent + max_scale_percent - scale_percent;

	uint32_t audio_w = mtsf->scale_w ? w * scale_percent / 100 : w;
	uint32_t audio_h = mtsf->scale_h ? h * scale_percent / 100 : h;

	if((audio_level < min_audio_level && !mtsf->invert) || audio_w < mtsf->min_w || audio_h < mtsf->min_h) {
		audio_w = mtsf->scale_w ? mtsf->min_w : w;
		audio_h = mtsf->scale_h ? mtsf->min_h : h;
	}
	
	if(audio_w > mtsf->max_w) audio_w = mtsf->scale_w ? mtsf->max_w : w;
	if(audio_h > mtsf->max_h) audio_h = mtsf->scale_h ? mtsf->max_h : h;

	obs_enter_graphics();
	obs_source_process_filter_begin(mtsf->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING);

	gs_effect_t *move_effect = mtsf->mover;
	gs_eparam_t *move_val = gs_effect_get_param_by_name(move_effect, "inputPos");
	gs_eparam_t *show = gs_effect_get_param_by_name(move_effect, "show");

	gs_effect_set_float(show, 1.0f);
	if(audio_w <= 0 || audio_h <= 0) {
		gs_effect_set_float(show, 0.0f);
		audio_w = 1;
		audio_h = 1;
	}

	//Change the position everytime so it looks like it's scaling from the center
	struct vec4 move_vec;
	vec4_set(&move_vec, (w - audio_w) / 2, (h - audio_h) / 2, 0.0f, 0.0f);

	gs_effect_set_vec4(move_val, &move_vec);

	obs_source_process_filter_end(mtsf->context, move_effect, audio_w, audio_h);
	obs_leave_graphics();
}

struct obs_source_info move_to_sound = {
	.id = "move_to_sound",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = get_source_name,
	.get_defaults = filter_defaults,
	.get_properties = filter_properties,
	.create = filter_create,
	.load = filter_load,
	.update = filter_update,
	.video_tick = target_update,
	.video_render = filter_render,
	.destroy = filter_destroy
};

bool obs_module_load(void)
{
	obs_register_source(&move_to_sound);
	return true;
}
