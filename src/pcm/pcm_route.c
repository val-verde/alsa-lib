/*
 *  PCM - Linear conversion
 *  Copyright (c) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 *
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
  
#include <byteswap.h>
#include <math.h>
#include "pcm_local.h"
#include "pcm_plugin.h"

/* The best possible hack to support missing optimization in gcc 2.7.2.3 */
#if ROUTE_PLUGIN_RESOLUTION & (ROUTE_PLUGIN_RESOLUTION - 1) != 0
#define div(a) a /= ROUTE_PLUGIN_RESOLUTION
#elif ROUTE_PLUGIN_RESOLUTION == 16
#define div(a) a >>= 4
#else
#error "Add some code here"
#endif

typedef struct {
	int channel;
	int as_int;
#if ROUTE_PLUGIN_FLOAT
	float as_float;
#endif
} snd_pcm_route_ttable_src_t;

typedef struct snd_pcm_route_ttable_dst snd_pcm_route_ttable_dst_t;

typedef struct {
	enum {UINT32=0, UINT64=1, FLOAT=2} sum_idx;
	int get_idx;
	int put_idx;
	int conv_idx;
	int src_size;
	snd_pcm_format_t dst_sfmt;
	unsigned int ndsts;
	snd_pcm_route_ttable_dst_t *dsts;
} snd_pcm_route_params_t;


typedef void (*route_f)(const snd_pcm_channel_area_t *dst_area,
			snd_pcm_uframes_t dst_offset,
			const snd_pcm_channel_area_t *src_areas,
			snd_pcm_uframes_t src_offset,
			snd_pcm_uframes_t frames,
			const snd_pcm_route_ttable_dst_t *ttable,
			const snd_pcm_route_params_t *params);

struct snd_pcm_route_ttable_dst {
	int att;	/* Attenuated */
	unsigned int nsrcs;
	snd_pcm_route_ttable_src_t* srcs;
	route_f func;
};

typedef union {
	u_int32_t as_uint32;
	u_int64_t as_uint64;
#if ROUTE_PLUGIN_FLOAT
	float as_float;
#endif
} sum_t;

typedef struct {
	/* This field need to be the first */
	snd_pcm_plugin_t plug;
	snd_pcm_format_t sformat;
	int schannels;
	snd_pcm_route_params_t params;
} snd_pcm_route_t;


void snd_pcm_route_convert1_zero(const snd_pcm_channel_area_t *dst_area,
				 snd_pcm_uframes_t dst_offset,
				 const snd_pcm_channel_area_t *src_areas ATTRIBUTE_UNUSED,
				 snd_pcm_uframes_t src_offset ATTRIBUTE_UNUSED,
				 snd_pcm_uframes_t frames,
				 const snd_pcm_route_ttable_dst_t* ttable ATTRIBUTE_UNUSED,
				 const snd_pcm_route_params_t *params)
{
	snd_pcm_area_silence(dst_area, dst_offset, frames, params->dst_sfmt);
}

void snd_pcm_route_convert1_one(const snd_pcm_channel_area_t *dst_area,
				snd_pcm_uframes_t dst_offset,
				const snd_pcm_channel_area_t *src_areas,
				snd_pcm_uframes_t src_offset,
				snd_pcm_uframes_t frames,
				const snd_pcm_route_ttable_dst_t* ttable,
				const snd_pcm_route_params_t *params)
{
#define CONV_LABELS
#include "plugin_ops.h"
#undef CONV_LABELS
	void *conv;
	const snd_pcm_channel_area_t *src_area = 0;
	unsigned int srcidx;
	const char *src;
	char *dst;
	int src_step, dst_step;
	for (srcidx = 0; srcidx < ttable->nsrcs; ++srcidx) {
		src_area = &src_areas[ttable->srcs[srcidx].channel];
		if (src_area->addr != NULL)
			break;
	}
	if (srcidx == ttable->nsrcs) {
		snd_pcm_route_convert1_zero(dst_area, dst_offset,
					    src_areas, src_offset,
					    frames, ttable, params);
		return;
	}
	
	conv = conv_labels[params->conv_idx];
	src = snd_pcm_channel_area_addr(src_area, src_offset);
	dst = snd_pcm_channel_area_addr(dst_area, dst_offset);
	src_step = snd_pcm_channel_area_step(src_area);
	dst_step = snd_pcm_channel_area_step(dst_area);
	while (frames-- > 0) {
		goto *conv;
#define CONV_END after
#include "plugin_ops.h"
#undef CONV_END
	after:
		src += src_step;
		dst += dst_step;
	}
}

void snd_pcm_route_convert1_many(const snd_pcm_channel_area_t *dst_area,
				 snd_pcm_uframes_t dst_offset,
				 const snd_pcm_channel_area_t *src_areas,
				 snd_pcm_uframes_t src_offset,
				 snd_pcm_uframes_t frames,
				 const snd_pcm_route_ttable_dst_t* ttable,
				 const snd_pcm_route_params_t *params)
{
#define GETU_LABELS
#define PUT32_LABELS
#include "plugin_ops.h"
#undef GETU_LABELS
#undef PUT32_LABELS
	static void *zero_labels[3] = {
		&&zero_int32, &&zero_int64,
#if ROUTE_PLUGIN_FLOAT
		&&zero_float
#endif
	};
	/* sum_type att */
	static void *add_labels[3 * 2] = {
		&&add_int32_noatt, &&add_int32_att,
		&&add_int64_noatt, &&add_int64_att,
#if ROUTE_PLUGIN_FLOAT
		&&add_float_noatt, &&add_float_att
#endif
	};
	/* sum_type att shift */
	static void *norm_labels[3 * 2 * 4] = {
		0,
		&&norm_int32_8_noatt,
		&&norm_int32_16_noatt,
		&&norm_int32_24_noatt,
		0,
		&&norm_int32_8_att,
		&&norm_int32_16_att,
		&&norm_int32_24_att,
		&&norm_int64_0_noatt,
		&&norm_int64_8_noatt,
		&&norm_int64_16_noatt,
		&&norm_int64_24_noatt,
		&&norm_int64_0_att,
		&&norm_int64_8_att,
		&&norm_int64_16_att,
		&&norm_int64_24_att,
#if ROUTE_PLUGIN_FLOAT
		&&norm_float_0,
		&&norm_float_8,
		&&norm_float_16,
		&&norm_float_24,
		&&norm_float_0,
		&&norm_float_8,
		&&norm_float_16,
		&&norm_float_24,
#endif
	};
	void *zero, *get, *add, *norm, *put32;
	int nsrcs = ttable->nsrcs;
	char *dst;
	int dst_step;
	const char *srcs[nsrcs];
	int src_steps[nsrcs];
	snd_pcm_route_ttable_src_t src_tt[nsrcs];
	u_int32_t sample = 0;
	int srcidx, srcidx1 = 0;
	for (srcidx = 0; srcidx < nsrcs; ++srcidx) {
		const snd_pcm_channel_area_t *src_area = &src_areas[ttable->srcs[srcidx].channel];
		srcs[srcidx1] = snd_pcm_channel_area_addr(src_area, src_offset);
		src_steps[srcidx1] = snd_pcm_channel_area_step(src_area);
		src_tt[srcidx1] = ttable->srcs[srcidx];
		srcidx1++;
	}
	nsrcs = srcidx1;
	if (nsrcs == 0) {
		snd_pcm_route_convert1_zero(dst_area, dst_offset,
					    src_areas, src_offset,
					    frames, ttable, params);
		return;
	} else if (nsrcs == 1 && src_tt[0].as_int == ROUTE_PLUGIN_RESOLUTION) {
		snd_pcm_route_convert1_one(dst_area, dst_offset,
					   src_areas, src_offset,
					   frames, ttable, params);
		return;
	}

	zero = zero_labels[params->sum_idx];
	get = getu_labels[params->get_idx];
	add = add_labels[params->sum_idx * 2 + ttable->att];
	norm = norm_labels[params->sum_idx * 8 + ttable->att * 4 + 4 - params->src_size];
	put32 = put32_labels[params->put_idx];
	dst = snd_pcm_channel_area_addr(dst_area, dst_offset);
	dst_step = snd_pcm_channel_area_step(dst_area);

	while (frames-- > 0) {
		snd_pcm_route_ttable_src_t *ttp = src_tt;
		sum_t sum;

		/* Zero sum */
		goto *zero;
	zero_int32:
		sum.as_uint32 = 0;
		goto zero_end;
	zero_int64: 
		sum.as_uint64 = 0;
		goto zero_end;
#if ROUTE_PLUGIN_FLOAT
	zero_float:
		sum.as_float = 0.0;
		goto zero_end;
#endif
	zero_end:
		for (srcidx = 0; srcidx < nsrcs; ++srcidx) {
			const char *src = srcs[srcidx];
			
			/* Get sample */
			goto *get;
#define GETU_END after_get
#include "plugin_ops.h"
#undef GETU_END
		after_get:

			/* Sum */
			goto *add;
		add_int32_att:
			sum.as_uint32 += sample * ttp->as_int;
			goto after_sum;
		add_int32_noatt:
			if (ttp->as_int)
				sum.as_uint32 += sample;
			goto after_sum;
		add_int64_att:
			sum.as_uint64 += (u_int64_t) sample * ttp->as_int;
			goto after_sum;
		add_int64_noatt:
			if (ttp->as_int)
				sum.as_uint64 += sample;
			goto after_sum;
#if ROUTE_PLUGIN_FLOAT
		add_float_att:
			sum.as_float += sample * ttp->as_float;
			goto after_sum;
		add_float_noatt:
			if (ttp->as_int)
				sum.as_float += sample;
			goto after_sum;
#endif
		after_sum:
			srcs[srcidx] += src_steps[srcidx];
			ttp++;
		}
		
		/* Normalization */
		goto *norm;
	norm_int32_8_att:
		sum.as_uint64 = sum.as_uint32;
	norm_int64_8_att:
		sum.as_uint64 <<= 8;
	norm_int64_0_att:
		div(sum.as_uint64);
		goto norm_int;

	norm_int32_16_att:
		sum.as_uint64 = sum.as_uint32;
	norm_int64_16_att:
		sum.as_uint64 <<= 16;
		div(sum.as_uint64);
		goto norm_int;

	norm_int32_24_att:
		sum.as_uint64 = sum.as_uint32;
	norm_int64_24_att:
		sum.as_uint64 <<= 24;
		div(sum.as_uint64);
		goto norm_int;

	norm_int32_8_noatt:
		sum.as_uint64 = sum.as_uint32;
	norm_int64_8_noatt:
		sum.as_uint64 <<= 8;
		goto norm_int;

	norm_int32_16_noatt:
		sum.as_uint64 = sum.as_uint32;
	norm_int64_16_noatt:
		sum.as_uint64 <<= 16;
		goto norm_int;

	norm_int32_24_noatt:
		sum.as_uint64 = sum.as_uint32;
	norm_int64_24_noatt:
		sum.as_uint64 <<= 24;
		goto norm_int;

	norm_int64_0_noatt:
	norm_int:
		if (sum.as_uint64 > (u_int32_t)0xffffffff)
			sample = (u_int32_t)0xffffffff;
		else
			sample = sum.as_uint64;
		goto after_norm;

#if ROUTE_PLUGIN_FLOAT
	norm_float_8:
		sum.as_float *= 1 << 8;
		goto norm_float;
	norm_float_16:
		sum.as_float *= 1 << 16;
		goto norm_float;
	norm_float_24:
		sum.as_float *= 1 << 24;
		goto norm_float;
	norm_float_0:
	norm_float:
		sum.as_float = floor(sum.as_float + 0.5);
		if (sum.as_float > (u_int32_t)0xffffffff)
			sample = (u_int32_t)0xffffffff;
		else
			sample = sum.as_float;
		goto after_norm;
#endif
	after_norm:
		
		/* Put sample */
		goto *put32;
#define PUT32_END after_put32
#include "plugin_ops.h"
#undef PUT32_END
	after_put32:
		
		dst += dst_step;
	}
}

void snd_pcm_route_convert(const snd_pcm_channel_area_t *dst_areas,
			   snd_pcm_uframes_t dst_offset,
			   const snd_pcm_channel_area_t *src_areas,
			   snd_pcm_uframes_t src_offset,
			   snd_pcm_uframes_t dst_channels,
			   snd_pcm_uframes_t frames,
			   snd_pcm_route_params_t *params)
{
	unsigned int dst_channel;
	snd_pcm_route_ttable_dst_t *dstp;
	const snd_pcm_channel_area_t *dst_area;

	dstp = params->dsts;
	dst_area = dst_areas;
	for (dst_channel = 0; dst_channel < dst_channels; ++dst_channel) {
		if (dst_channel >= params->ndsts)
			snd_pcm_route_convert1_zero(dst_area, dst_offset,
						    src_areas, src_offset,
						    frames, dstp, params);
		else
			dstp->func(dst_area, dst_offset,
				   src_areas, src_offset,
				   frames, dstp, params);
		dstp++;
		dst_area++;
	}
}

static int snd_pcm_route_close(snd_pcm_t *pcm)
{
	snd_pcm_route_t *route = pcm->private_data;
	snd_pcm_route_params_t *params = &route->params;
	int err = 0;
	unsigned int dst_channel;
	if (route->plug.close_slave)
		err = snd_pcm_close(route->plug.slave);
	if (params->dsts) {
		for (dst_channel = 0; dst_channel < params->ndsts; ++dst_channel) {
			if (params->dsts[dst_channel].srcs != NULL)
				free(params->dsts[dst_channel].srcs);
		}
		free(params->dsts);
	}
	free(route);
	return 0;
}

static int snd_pcm_route_hw_refine_cprepare(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params)
{
	int err;
	snd_pcm_access_mask_t access_mask = { SND_PCM_ACCBIT_PLUGIN };
	snd_pcm_format_mask_t format_mask = { SND_PCM_FMTBIT_LINEAR };
	err = _snd_pcm_hw_param_set_mask(params, SND_PCM_HW_PARAM_ACCESS,
					 &access_mask);
	if (err < 0)
		return err;
	err = _snd_pcm_hw_param_set_mask(params, SND_PCM_HW_PARAM_FORMAT,
					 &format_mask);
	if (err < 0)
		return err;
	err = _snd_pcm_hw_params_set_subformat(params, SND_PCM_SUBFORMAT_STD);
	if (err < 0)
		return err;
	err = _snd_pcm_hw_param_set_min(params, SND_PCM_HW_PARAM_CHANNELS, 1, 0);
	if (err < 0)
		return err;
	params->info &= ~(SND_PCM_INFO_MMAP | SND_PCM_INFO_MMAP_VALID);
	return 0;
}

static int snd_pcm_route_hw_refine_sprepare(snd_pcm_t *pcm, snd_pcm_hw_params_t *sparams)
{
	snd_pcm_route_t *route = pcm->private_data;
	snd_pcm_access_mask_t saccess_mask = { SND_PCM_ACCBIT_MMAP };
	_snd_pcm_hw_params_any(sparams);
	_snd_pcm_hw_param_set_mask(sparams, SND_PCM_HW_PARAM_ACCESS,
				   &saccess_mask);
	if (route->sformat != SND_PCM_FORMAT_UNKNOWN) {
		_snd_pcm_hw_params_set_format(sparams, route->sformat);
		_snd_pcm_hw_params_set_subformat(sparams, SND_PCM_SUBFORMAT_STD);
	}
	if (route->schannels >= 0) {
		_snd_pcm_hw_param_set(sparams, SND_PCM_HW_PARAM_CHANNELS,
				      route->schannels, 0);
	}
	return 0;
}

static int snd_pcm_route_hw_refine_schange(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
					    snd_pcm_hw_params_t *sparams)
{
	snd_pcm_route_t *route = pcm->private_data;
	int err;
	unsigned int links = (SND_PCM_HW_PARBIT_RATE |
			      SND_PCM_HW_PARBIT_PERIODS |
			      SND_PCM_HW_PARBIT_PERIOD_SIZE |
			      SND_PCM_HW_PARBIT_PERIOD_TIME |
			      SND_PCM_HW_PARBIT_BUFFER_SIZE |
			      SND_PCM_HW_PARBIT_BUFFER_TIME |
			      SND_PCM_HW_PARBIT_TICK_TIME);
	if (route->sformat == SND_PCM_FORMAT_UNKNOWN)
		links |= (SND_PCM_HW_PARBIT_FORMAT | 
			  SND_PCM_HW_PARBIT_SUBFORMAT |
			  SND_PCM_HW_PARBIT_SAMPLE_BITS);
	if (route->schannels < 0)
		links |= SND_PCM_HW_PARBIT_CHANNELS;
	err = _snd_pcm_hw_params_refine(sparams, links, params);
	if (err < 0)
		return err;
	return 0;
}
	
static int snd_pcm_route_hw_refine_cchange(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
					    snd_pcm_hw_params_t *sparams)
{
	snd_pcm_route_t *route = pcm->private_data;
	int err;
	unsigned int links = (SND_PCM_HW_PARBIT_RATE |
			      SND_PCM_HW_PARBIT_PERIODS |
			      SND_PCM_HW_PARBIT_PERIOD_SIZE |
			      SND_PCM_HW_PARBIT_PERIOD_TIME |
			      SND_PCM_HW_PARBIT_BUFFER_SIZE |
			      SND_PCM_HW_PARBIT_BUFFER_TIME |
			      SND_PCM_HW_PARBIT_TICK_TIME);
	if (route->sformat == SND_PCM_FORMAT_UNKNOWN)
		links |= (SND_PCM_HW_PARBIT_FORMAT | 
			  SND_PCM_HW_PARBIT_SUBFORMAT |
			  SND_PCM_HW_PARBIT_SAMPLE_BITS);
	if (route->schannels < 0)
		links |= SND_PCM_HW_PARBIT_CHANNELS;
	err = _snd_pcm_hw_params_refine(params, links, sparams);
	if (err < 0)
		return err;
	return 0;
}

static int snd_pcm_route_hw_refine(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	return snd_pcm_hw_refine_slave(pcm, params,
				       snd_pcm_route_hw_refine_cprepare,
				       snd_pcm_route_hw_refine_cchange,
				       snd_pcm_route_hw_refine_sprepare,
				       snd_pcm_route_hw_refine_schange,
				       snd_pcm_plugin_hw_refine_slave);
}

static int snd_pcm_route_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t * params)
{
	snd_pcm_route_t *route = pcm->private_data;
	snd_pcm_t *slave = route->plug.slave;
	snd_pcm_format_t src_format, dst_format;
	int err = snd_pcm_hw_params_slave(pcm, params,
					  snd_pcm_route_hw_refine_cchange,
					  snd_pcm_route_hw_refine_sprepare,
					  snd_pcm_route_hw_refine_schange,
					  snd_pcm_plugin_hw_params_slave);
	if (err < 0)
		return err;

	if (pcm->stream == SND_PCM_STREAM_PLAYBACK) {
		src_format = snd_pcm_hw_params_get_format(params);
		dst_format = slave->format;
	} else {
		src_format = slave->format;
		dst_format = snd_pcm_hw_params_get_format(params);
	}
	route->params.get_idx = snd_pcm_linear_get_index(src_format, SND_PCM_FORMAT_U16);
	route->params.put_idx = snd_pcm_linear_put_index(SND_PCM_FORMAT_U32, dst_format);
	route->params.conv_idx = snd_pcm_linear_convert_index(src_format, dst_format);
	route->params.src_size = snd_pcm_format_width(src_format) / 8;
	route->params.dst_sfmt = dst_format;
#if ROUTE_PLUGIN_FLOAT
	route->params.sum_idx = FLOAT;
#else
	if (src_size == 4)
		route->params.sum_idx = UINT64;
	else
		route->params.sum_idx = UINT32;
#endif
	return 0;
}

static snd_pcm_uframes_t
snd_pcm_route_write_areas(snd_pcm_t *pcm,
			  const snd_pcm_channel_area_t *areas,
			  snd_pcm_uframes_t offset,
			  snd_pcm_uframes_t size,
			  const snd_pcm_channel_area_t *slave_areas,
			  snd_pcm_uframes_t slave_offset,
			  snd_pcm_uframes_t *slave_sizep)
{
	snd_pcm_route_t *route = pcm->private_data;
	snd_pcm_t *slave = route->plug.slave;
	if (size > *slave_sizep)
		size = *slave_sizep;
	snd_pcm_route_convert(slave_areas, slave_offset,
			      areas, offset, 
			      slave->channels, size, &route->params);
	*slave_sizep = size;
	return size;
}

static snd_pcm_uframes_t
snd_pcm_route_read_areas(snd_pcm_t *pcm,
			 const snd_pcm_channel_area_t *areas,
			 snd_pcm_uframes_t offset,
			 snd_pcm_uframes_t size,
			 const snd_pcm_channel_area_t *slave_areas,
			 snd_pcm_uframes_t slave_offset,
			 snd_pcm_uframes_t *slave_sizep)
{
	snd_pcm_route_t *route = pcm->private_data;
	if (size > *slave_sizep)
		size = *slave_sizep;
	snd_pcm_route_convert(areas, offset, 
			      slave_areas, slave_offset,
			      pcm->channels, size, &route->params);
	*slave_sizep = size;
	return size;
}

static void snd_pcm_route_dump(snd_pcm_t *pcm, snd_output_t *out)
{
	snd_pcm_route_t *route = pcm->private_data;
	unsigned int dst;
	if (route->sformat == SND_PCM_FORMAT_UNKNOWN)
		snd_output_printf(out, "Route conversion PCM\n");
	else
		snd_output_printf(out, "Route conversion PCM (sformat=%s)\n", 
			snd_pcm_format_name(route->sformat));
	snd_output_puts(out, "Transformation table:\n");
	for (dst = 0; dst < route->params.ndsts; dst++) {
		snd_pcm_route_ttable_dst_t *d = &route->params.dsts[dst];
		unsigned int src;
		if (d->nsrcs == 0)
			continue;
		snd_output_printf(out, "%d <- ", dst);
		src = 0;
		while (1) {
			snd_pcm_route_ttable_src_t *s = &d->srcs[src];
			if (d->att)
				snd_output_printf(out, "%d*%g", s->channel, s->as_float);
			else
				snd_output_printf(out, "%d", s->channel);
			src++;
			if (src == d->nsrcs)
				break;
			snd_output_puts(out, " + ");
		}
		snd_output_putc(out, '\n');
	}
	if (pcm->setup) {
		snd_output_printf(out, "Its setup is:\n");
		snd_pcm_dump_setup(pcm, out);
	}
	snd_output_printf(out, "Slave: ");
	snd_pcm_dump(route->plug.slave, out);
}

snd_pcm_ops_t snd_pcm_route_ops = {
	close: snd_pcm_route_close,
	info: snd_pcm_plugin_info,
	hw_refine: snd_pcm_route_hw_refine,
	hw_params: snd_pcm_route_hw_params,
	hw_free: snd_pcm_plugin_hw_free,
	sw_params: snd_pcm_plugin_sw_params,
	channel_info: snd_pcm_plugin_channel_info,
	dump: snd_pcm_route_dump,
	nonblock: snd_pcm_plugin_nonblock,
	async: snd_pcm_plugin_async,
	mmap: snd_pcm_plugin_mmap,
	munmap: snd_pcm_plugin_munmap,
};

int route_load_ttable(snd_pcm_route_params_t *params, snd_pcm_stream_t stream,
		      unsigned int tt_ssize,
		      snd_pcm_route_ttable_entry_t *ttable,
		      unsigned int tt_cused, unsigned int tt_sused)
{
	unsigned int src_channel, dst_channel;
	snd_pcm_route_ttable_dst_t *dptr;
	unsigned int sused, dused, smul, dmul;
	if (stream == SND_PCM_STREAM_PLAYBACK) {
		sused = tt_cused;
		dused = tt_sused;
		smul = tt_ssize;
		dmul = 1;
	} else {
		sused = tt_sused;
		dused = tt_cused;
		smul = 1;
		dmul = tt_ssize;
	}
	params->ndsts = dused;
	dptr = calloc(dused, sizeof(*params->dsts));
	if (!dptr)
		return -ENOMEM;
	params->dsts = dptr;
	for (dst_channel = 0; dst_channel < dused; ++dst_channel) {
		snd_pcm_route_ttable_entry_t t = 0;
		int att = 0;
		int nsrcs = 0;
		snd_pcm_route_ttable_src_t srcs[sused];
		for (src_channel = 0; src_channel < sused; ++src_channel) {
			snd_pcm_route_ttable_entry_t v;
			v = ttable[src_channel * smul + dst_channel * dmul];
			assert(v >= 0 && v <= FULL);
			if (v != 0) {
				srcs[nsrcs].channel = src_channel;
#if ROUTE_PLUGIN_FLOAT
				/* Also in user space for non attenuated */
				srcs[nsrcs].as_int = (v == FULL ? ROUTE_PLUGIN_RESOLUTION : 0);
				srcs[nsrcs].as_float = v;
#else
				srcs[nsrcs].as_int = v;
#endif
				if (v != FULL)
					att = 1;
				t += v;
				nsrcs++;
			}
		}
#if 0
		assert(t <= FULL);
#endif
		dptr->att = att;
		dptr->nsrcs = nsrcs;
		if (nsrcs == 0)
			dptr->func = snd_pcm_route_convert1_zero;
		else if (nsrcs == 1 && !att)
			dptr->func = snd_pcm_route_convert1_one;
		else
			dptr->func = snd_pcm_route_convert1_many;
		if (nsrcs > 0) {
			dptr->srcs = calloc(nsrcs, sizeof(*srcs));
			if (!dptr->srcs)
				return -ENOMEM;
			memcpy(dptr->srcs, srcs, sizeof(*srcs) * nsrcs);
		} else
			dptr->srcs = 0;
		dptr++;
	}
	return 0;
}


int snd_pcm_route_open(snd_pcm_t **pcmp, const char *name,
		       snd_pcm_format_t sformat, unsigned int schannels,
		       snd_pcm_route_ttable_entry_t *ttable,
		       unsigned int tt_ssize,
		       unsigned int tt_cused, unsigned int tt_sused,
		       snd_pcm_t *slave, int close_slave)
{
	snd_pcm_t *pcm;
	snd_pcm_route_t *route;
	int err;
	assert(pcmp && slave && ttable);
	if (sformat != SND_PCM_FORMAT_UNKNOWN && 
	    snd_pcm_format_linear(sformat) != 1)
		return -EINVAL;
	route = calloc(1, sizeof(snd_pcm_route_t));
	if (!route) {
		return -ENOMEM;
	}
	route->sformat = sformat;
	route->schannels = schannels;
	route->plug.read = snd_pcm_route_read_areas;
	route->plug.write = snd_pcm_route_write_areas;
	route->plug.slave = slave;
	route->plug.close_slave = close_slave;

	pcm = calloc(1, sizeof(snd_pcm_t));
	if (!pcm) {
		free(route);
		return -ENOMEM;
	}
	if (name)
		pcm->name = strdup(name);
	pcm->type = SND_PCM_TYPE_ROUTE;
	pcm->stream = slave->stream;
	pcm->mode = slave->mode;
	pcm->ops = &snd_pcm_route_ops;
	pcm->op_arg = pcm;
	pcm->fast_ops = &snd_pcm_plugin_fast_ops;
	pcm->fast_op_arg = pcm;
	pcm->private_data = route;
	pcm->poll_fd = slave->poll_fd;
	pcm->hw_ptr = &route->plug.hw_ptr;
	pcm->appl_ptr = &route->plug.appl_ptr;
	err = route_load_ttable(&route->params, pcm->stream, tt_ssize, ttable, tt_cused, tt_sused);
	if (err < 0) {
		snd_pcm_close(pcm);
		return err;
	}
	*pcmp = pcm;

	return 0;
}

int snd_pcm_route_load_ttable(snd_config_t *tt, snd_pcm_route_ttable_entry_t *ttable,
			      unsigned int tt_csize, unsigned int tt_ssize,
			      unsigned int *tt_cused, unsigned int *tt_sused,
			      int schannels)
{
	int cused = -1;
	int sused = -1;
	snd_config_iterator_t i, inext;
	unsigned int k;
	for (k = 0; k < tt_csize * tt_ssize; ++k)
		ttable[k] = 0.0;
	snd_config_for_each(i, inext, tt) {
		snd_config_t *in = snd_config_iterator_entry(i);
		snd_config_iterator_t j, jnext;
		char *p;
		long cchannel;
		errno = 0;
		cchannel = strtol(snd_config_get_id(in), &p, 10);
		if (errno || *p || 
		    cchannel < 0 || (unsigned int) cchannel > tt_csize) {
			SNDERR("Invalid client channel: %s", snd_config_get_id(in));
			return -EINVAL;
		}
		if (snd_config_get_type(in) != SND_CONFIG_TYPE_COMPOUND)
			return -EINVAL;
		snd_config_for_each(j, jnext, in) {
			snd_config_t *jn = snd_config_iterator_entry(j);
			double value;
			long schannel;
			int err;
			const char *id = snd_config_get_id(jn);
			errno = 0;
			schannel = strtol(id, &p, 10);
			if (errno || *p || 
			    schannel < 0 || (unsigned int) schannel > tt_ssize || 
			    (schannels > 0 && schannel >= schannels)) {
				SNDERR("Invalid slave channel: %s", id);
				return -EINVAL;
			}
			err = snd_config_get_real(jn, &value);
			if (err < 0) {
				long v;
				err = snd_config_get_integer(jn, &v);
				if (err < 0) {
					SNDERR("Invalid type for %s", id);
					return -EINVAL;
				}
				value = v;
			}
			ttable[cchannel * tt_ssize + schannel] = value;
			if (schannel > sused)
				sused = schannel;
		}
		if (cchannel > cused)
			cused = cchannel;
	}
	*tt_sused = sused + 1;
	*tt_cused = cused + 1;
	return 0;
}

#define MAX_CHANNELS 32

int _snd_pcm_route_open(snd_pcm_t **pcmp, const char *name,
			snd_config_t *conf, 
			snd_pcm_stream_t stream, int mode)
{
	snd_config_iterator_t i, next;
	const char *sname = NULL;
	int err;
	snd_pcm_t *spcm;
	snd_config_t *slave = NULL;
	snd_pcm_format_t sformat = SND_PCM_FORMAT_UNKNOWN;
	int schannels = -1;
	snd_config_t *tt = NULL;
	snd_pcm_route_ttable_entry_t ttable[MAX_CHANNELS*MAX_CHANNELS];
	unsigned int cused, sused;
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id = snd_config_get_id(n);
		if (strcmp(id, "comment") == 0)
			continue;
		if (strcmp(id, "type") == 0)
			continue;
		if (strcmp(id, "slave") == 0) {
			slave = n;
			continue;
		}
		if (strcmp(id, "ttable") == 0) {
			if (snd_config_get_type(n) != SND_CONFIG_TYPE_COMPOUND) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			tt = n;
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}
	if (!slave) {
		SNDERR("slave is not defined");
		return -EINVAL;
	}
	if (!tt) {
		SNDERR("ttable is not defined");
		return -EINVAL;
	}
	err = snd_pcm_slave_conf(slave, &sname, 2,
				 SND_PCM_HW_PARAM_FORMAT, 0, &sformat,
				 SND_PCM_HW_PARAM_CHANNELS, 0, &schannels);
	if (err < 0)
		return err;
	if (sformat != SND_PCM_FORMAT_UNKNOWN &&
	    snd_pcm_format_linear(sformat) != 1) {
		SNDERR("slave format is not linear");
		return -EINVAL;
	}

	err = snd_pcm_route_load_ttable(tt, ttable, MAX_CHANNELS, MAX_CHANNELS,
					&cused, &sused, schannels);
	if (err < 0)
		return err;

	/* This is needed cause snd_config_update may destroy config */
	sname = strdup(sname);
	if (!sname)
		return  -ENOMEM;
	err = snd_pcm_open(&spcm, sname, stream, mode);
	free((void *) sname);
	if (err < 0)
		return err;
	err = snd_pcm_route_open(pcmp, name, sformat, schannels,
				 ttable, MAX_CHANNELS,
				 cused, sused,
				 spcm, 1);
	if (err < 0)
		snd_pcm_close(spcm);
	return err;
}
				

