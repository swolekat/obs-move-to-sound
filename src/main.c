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
#define MTS_QUIETX "MTS_QUIETX"
#define MTS_QUIETY "MTS_QUIETY"
#define MTS_LOUDX "MTS_LOUDX"
#define MTS_LOUDY "MTS_LOUDY"
#define MTS_ANIMATIONTIME "MTS_ANIMATIONTIME"
#define MTS_FADETIME "MTS_FADETIME"

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

	float audio_level;

	gs_effect_t *mover;
	obs_source_t *audio_source;

	long long quiet_x;
    long long quiet_y;
    long long loud_x;
    long long loud_y;

    bool audio_is_playing;
    vec2 src_position;
    bool at_top;
    bool at_bottom;
    double move_down_buffer_remaining;
    double animation_time;
    vec2 velocity_per_second;
    double fade_time;
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

	return mtsf;
}
static void filter_update(void *data, obs_data_t *settings)
{
	struct move_to_sound_data *mtsf = data;

	obs_source_t *target = obs_filter_get_target(mtsf->context);
	mtsf->target = target;

	mtsf->quiet_x = obs_data_get_int(settings, MTS_QUIETX);
	mtsf->quiet_y = obs_data_get_int(settings, MTS_QUIETY);
	mtsf->loud_x = obs_data_get_int(settings, MTS_LOUDX);
	mtsf->loud_y = obs_data_get_int(settings, MTS_LOUDY);

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

    double animation_time = obs_data_get_double(settings, MTS_ANIMATIONTIME) / 1000;
	mtsf->velocity_per_second = vec2((mtsf->loud_x - mtsf->quiet_x)/animation_time, (mtsf->loud_y - mtsf->quiet_y)/animation_time);

	mtsf->fade_time = obs_data_get_double(settings, MTS_ANIMATIONTIME) / 1000;
	mtsf->move_down_buffer_remaining = mtsf->fade_time;
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

	obs_properties_add_bool(p, MTS_SCALEW, "Scale Width");
	obs_properties_add_bool(p, MTS_SCALEH, "Scale Height");

    obs_properties_add_int(p, MTS_QUIETX, "Quiet X", -8192, 8192, 1);
    obs_properties_add_int(p, MTS_QUIETY, "Quiet Y", -8192, 8192, 1);

    obs_properties_add_int(p, MTS_LOUDX, "Loud X", -8192, 8192, 1);
    obs_properties_add_int(p, MTS_LOUDY, "Loud Y", -8192, 8192, 1);

	obs_property_t *anitime = obs_properties_add_float_slider(p, MTS_ANIMATIONTIME, "Fade Time", 0, 10000, 1);
	obs_property_float_set_suffix(anitime, "ms");

	obs_property_t *fadetime = obs_properties_add_float_slider(p, MTS_FADETIME, "Animation Time", 0, 10000, 1);
    obs_property_float_set_suffix(fadetime, "ms");

	return p;
}
static void filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, MTS_MINLVL, -40);
	obs_data_set_default_int(settings, MTS_QUIETX, 0);
	obs_data_set_default_int(settings, MTS_QUIETY, 0);
	obs_data_set_default_int(settings, MTS_LOUDX, 0);
	obs_data_set_default_int(settings, MTS_LOUDY, 0);
	obs_data_set_default_int(settings, MTS_ANIMATIONTIME, 1000);
	obs_data_set_default_int(settings, MTS_FADETIME, 3000);
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
	mtsf->animation_time = mtsf->animation_time + seconds;

//	todo here?


}

static obs_sceneitem_t *get_scene_item() {
    obs_source_t current_scene = obs_frontend_get_current_scene();
    if(current_scene == null){
        return null;
    }
    obs_source_release(current_scene)
    obs_scene_t scene = obs.obs_scene_from_source(current_scene)
    return obs.obs_scene_find_source(scene, mtsf->target.get_name)
}

static bool is_at_top(vec2 *scene_item_position){
    return scene_item_position.x == mtsf->loud_x && scene_item_position.y == mtsf->loud_y;
}

static bool is_at_bottom(vec2 *scene_item_position){
    return scene_item_position.x == mtsf->quiet_x && scene_item_position.y == mtsf->quiet_y;
}

static void move_up(vec2 *scene_item_position) {
    vec2 animation_time_vec = vec2(mtsf->animation_time, mtsf->animation_time);
    vec2 change = mtsf->velocity_per_second * mtsf->animation_time_vec;
    vec2 new_pos = scene_item_position + change;
//    todo make this generic
    if(new_pos.x > mtsf->loud_x){
        new_pos.x = mtsf->loud_x;
    }
    if(new_pos.y > mtsf->loud_y){
        new_pos.y = mtsf->loud_y;
    }
    obs_sceneitem_set_pos(scene_item, new_pos)
}

static void move_down(obs_sceneitem_t *scene_item, vec2 *scene_item_position) {
    vec2 animation_time_vec = vec2(mtsf->animation_time * -1, mtsf->animation_time * -1);
    vec2 change = mtsf->velocity_per_second * mtsf->animation_time_vec;
    vec2 new_pos = scene_item_position + change;
    //    todo make this generic
    if(new_pos.x < mtsf->quiet_x){
        new_pos.x = mtsf->quiet_x;
    }
    if(new_pos.y < mtsf->quiet_y){
        new_pos.y = mtsf->quiet_y;
    }
    obs_sceneitem_set_pos(scene_item, new_pos)
}

static void filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

// todo here
	struct move_to_sound_data *mtsf = data;

	// 	struct vec4 move_vec;


	double min_audio_level = mtsf->minimum_audio_level;
	double audio_level = mtsf->audio_level;

	if(min_audio_level >= 0) min_audio_level = -0.5f;

	if(audio_level>=min_audio_level){
	    mtsf->audio_is_playing = true;
	    mtsf->move_down_buffer_remaining = mtsf->fade_time;
	} else {
	    mtsf->move_down_buffer_remaining = mtsf->move_down_buffer_remaining - mtsf->animation_time;
	    mtsf->animation_time = 0;
	    if(mtsf->move_down_buffer_remaining < 0){
	        mtsf->audio_is_playing = false;
	    }
	}

	obs_sceneitem_t *scene_item = get_scene_item();
	if(scene_item == null){
	    return;
	}
	vec2 *scene_item_position;
	obs_sceneitem_get_pos(scene_item, scene_item_position);

	bool at_top = is_at_top(scene_item_position);
    bool at_bottom = is_at_bottom(scene_item_position);

    if audio_is_playing && !at_top {
        move_up(scene_item, scene_item_position);
    }
    if !audio_is_playing && !at_bottom {
        move_down(scene_item, scene_item_position);
    }

    mtsf->animation_time = 0;
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
