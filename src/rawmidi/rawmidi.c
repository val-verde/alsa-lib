/*
 *  RawMIDI Interface - main file
 *  Copyright (c) 2000 by Jaroslav Kysela <perex@suse.cz>
 *                        Abramo Bagnara <abramo@alsa-project.org>
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include "rawmidi_local.h"

const char *snd_rawmidi_name(snd_rawmidi_t *rawmidi)
{
	assert(rawmidi);
	return rawmidi->name;
}

snd_rawmidi_type_t snd_rawmidi_type(snd_rawmidi_t *rawmidi)
{
	assert(rawmidi);
	return rawmidi->type;
}

int snd_rawmidi_close(snd_rawmidi_t *rmidi)
{
	int err;
  	assert(rmidi);
	if ((err = rmidi->ops->close(rmidi)) < 0)
		return err;
	if (rmidi->name)
		free(rmidi->name);
	free(rmidi);
	return 0;
}

int _snd_rawmidi_poll_descriptor(snd_rawmidi_t *rmidi)
{
	assert(rmidi);
	return rmidi->poll_fd;
}

int snd_rawmidi_poll_descriptors_count(snd_rawmidi_t *rmidi)
{
	assert(rmidi);
	return 1;
}

int snd_rawmidi_poll_descriptors(snd_rawmidi_t *rmidi, struct pollfd *pfds, unsigned int space)
{
	assert(rmidi);
	if (space >= 1) {
		pfds->fd = rmidi->poll_fd;
		pfds->events = rmidi->stream == SND_RAWMIDI_STREAM_OUTPUT ? POLLOUT : POLLIN;
		return 1;
	}
	return 0;
}


int snd_rawmidi_nonblock(snd_rawmidi_t *rmidi, int nonblock)
{
	int err;
	assert(rmidi);
	assert(!(rmidi->mode & SND_RAWMIDI_APPEND));
	if ((err = rmidi->ops->nonblock(rmidi, nonblock)) < 0)
		return err;
	if (nonblock)
		rmidi->mode |= SND_RAWMIDI_NONBLOCK;
	else
		rmidi->mode &= ~SND_RAWMIDI_NONBLOCK;
	return 0;
}

int snd_rawmidi_info(snd_rawmidi_t *rmidi, snd_rawmidi_info_t * info)
{
	assert(rmidi);
	assert(info);
	return rmidi->ops->info(rmidi, info);
}

int snd_rawmidi_params(snd_rawmidi_t *rmidi, snd_rawmidi_params_t * params)
{
	int err;
	assert(rmidi);
	assert(params);
	err = rmidi->ops->params(rmidi, params);
	if (err < 0)
		return err;
	rmidi->buffer_size = params->buffer_size;
	rmidi->avail_min = params->avail_min;
	rmidi->no_active_sensing = params->no_active_sensing;
	return 0;
}

int snd_rawmidi_status(snd_rawmidi_t *rmidi, snd_rawmidi_status_t * status)
{
	assert(rmidi);
	assert(status);
	return rmidi->ops->status(rmidi, status);
}

int snd_rawmidi_drop(snd_rawmidi_t *rmidi)
{
	assert(rmidi);
	return rmidi->ops->drop(rmidi);
}

int snd_rawmidi_drain(snd_rawmidi_t *rmidi)
{
	assert(rmidi);
	return rmidi->ops->drain(rmidi);
}

ssize_t snd_rawmidi_write(snd_rawmidi_t *rmidi, const void *buffer, size_t size)
{
	assert(rmidi);
	assert(rmidi->stream == SND_RAWMIDI_STREAM_OUTPUT);
	assert(buffer || size == 0);
	return rmidi->ops->write(rmidi, buffer, size);
}

ssize_t snd_rawmidi_read(snd_rawmidi_t *rmidi, void *buffer, size_t size)
{
	assert(rmidi);
	assert(rmidi->stream == SND_RAWMIDI_STREAM_INPUT);
	assert(buffer || size == 0);
	return rmidi->ops->read(rmidi, buffer, size);
}

int snd_rawmidi_params_current(snd_rawmidi_t *rmidi, snd_rawmidi_params_t *params)
{
	assert(rmidi);
	assert(params);
	params->buffer_size = rmidi->buffer_size;
	params->avail_min = rmidi->avail_min;
	params->no_active_sensing = rmidi->no_active_sensing;
	return 0;
}

int snd_rawmidi_params_default(snd_rawmidi_t *rmidi, snd_rawmidi_params_t *params)
{
	assert(rmidi);
	assert(params);
	params->buffer_size = page_size();
	params->avail_min = 1;
	params->no_active_sensing = 0;
	return 0;
}

int snd_rawmidi_open(snd_rawmidi_t **inputp, snd_rawmidi_t **outputp,
		     const char *name, int mode)
{
	const char *str;
	char buf[256];
	int err;
	snd_config_t *rawmidi_conf, *conf, *type_conf;
	snd_config_iterator_t i, next;
	snd_rawmidi_params_t params;
	const char *lib = NULL, *open = NULL;
	int (*open_func)(snd_rawmidi_t **inputp, snd_rawmidi_t **outputp,
			 const char *name, snd_config_t *conf, int mode);
	void *h;
	const char *name1;
	assert((inputp || outputp) && name);
	err = snd_config_update();
	if (err < 0)
		return err;
	err = snd_config_search_alias(snd_config, "rawmidi", name, &rawmidi_conf);
	name1 = name;
	if (err < 0 || snd_config_get_string(rawmidi_conf, &name1) >= 0) {
		int card, dev, subdev;
		err = sscanf(name1, "hw:%d,%d,%d", &card, &dev, &subdev);
		if (err == 3)
			return snd_rawmidi_hw_open(inputp, outputp, name, card, dev, subdev, mode);
		err = sscanf(name1, "hw:%d,%d", &card, &dev);
		if (err == 2)
			return snd_rawmidi_hw_open(inputp, outputp, name, card, dev, -1, mode);
		SNDERR("Unknown RAWMIDI %s", name1);
		return -ENOENT;
	}
	if (snd_config_get_type(rawmidi_conf) != SND_CONFIG_TYPE_COMPOUND) {
		SNDERR("Invalid type for RAWMIDI %s definition", name);
		return -EINVAL;
	}
	err = snd_config_search(rawmidi_conf, "type", &conf);
	if (err < 0) {
		SNDERR("type is not defined");
		return err;
	}
	err = snd_config_get_string(conf, &str);
	if (err < 0) {
		SNDERR("Invalid type for %s", snd_config_get_id(conf));
		return err;
	}
	err = snd_config_search_alias(snd_config, "rawmidi_type", str, &type_conf);
	if (err >= 0) {
		snd_config_for_each(i, next, type_conf) {
			snd_config_t *n = snd_config_iterator_entry(i);
			const char *id = snd_config_get_id(n);
			if (strcmp(id, "comment") == 0)
				continue;
			if (strcmp(id, "lib") == 0) {
				err = snd_config_get_string(n, &lib);
				if (err < 0) {
					SNDERR("Invalid type for %s", id);
					return -EINVAL;
				}
				continue;
			}
			if (strcmp(id, "open") == 0) {
				err = snd_config_get_string(n, &open);
				if (err < 0) {
					SNDERR("Invalid type for %s", id);
					return -EINVAL;
				}
				continue;
			}
			SNDERR("Unknown field %s", id);
			return -EINVAL;
		}
	}
	if (!open) {
		open = buf;
		snprintf(buf, sizeof(buf), "_snd_rawmidi_%s_open", str);
	}
	if (!lib)
		lib = "libasound.so";
	h = dlopen(lib, RTLD_NOW);
	if (!h) {
		SNDERR("Cannot open shared library %s", lib);
		return -ENOENT;
	}
	open_func = dlsym(h, open);
	if (!open_func) {
		SNDERR("symbol %s is not defined inside %s", open, lib);
		dlclose(h);
		return -ENXIO;
	}
	err = open_func(inputp, outputp, name, rawmidi_conf, mode);
	if (err < 0)
		return err;
	if (inputp) {
		snd_rawmidi_params_default(*inputp, &params);
		err = snd_rawmidi_params(*inputp, &params);
		assert(err >= 0);
	}
	if (outputp) {
		snd_rawmidi_params_default(*outputp, &params);
		err = snd_rawmidi_params(*outputp, &params);
		assert(err >= 0);
	}
	return 0;
}

