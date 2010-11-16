/*
 * alsa-android - Alsa virtual driver that uses the MSM android sound driver
 * 
 * Copyright (C) Ahmed Abdel-Hamid 2010 <ahmedam@mail.usa.com>
 * 
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <sys/ioctl.h>
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

#ifndef uint32_t
#define uint32_t unsigned int
#endif
#include <linux/msm_audio.h>

#include "utils.h"

#define ARRAY_SIZE(ary)	(sizeof(ary)/sizeof(ary[0]))

typedef struct snd_pcm_alsa_android {
	snd_pcm_ioplug_t io;
	int format;
	int sample_rate;
	int bytes_per_frame;
	int buffer_size;
	int started;
	unsigned int old_route;
	snd_pcm_sframes_t hw_pointer;
} snd_pcm_alsa_android_t;

static int alsa_android_close(snd_pcm_ioplug_t * io);

static int do_route_audio_rpc (uint32_t device, int ear_mute, int mic_mute)
{
	if (device == -1UL)
		return 0;

	int fd;

	fd = open ("/dev/msm_snd", O_RDWR);
	if (fd < 0)
	{
		perror ("Can not open snd device");
		return -1;
	}

	struct msm_snd_device_config args;
	args.device = device;
	args.ear_mute = ear_mute ? SND_MUTE_MUTED : SND_MUTE_UNMUTED;
	args.mic_mute = mic_mute ? SND_MUTE_MUTED : SND_MUTE_UNMUTED;

	if (ioctl (fd, SND_SET_DEVICE, &args) < 0)
	{
		perror ("snd_set_device error.");
		close (fd);
		return -1;
	}

	close (fd);
	return 0;
}

static int alsa_android_prepare1(snd_pcm_ioplug_t * io)
{
	snd_pcm_alsa_android_t *alsa_android = io->private_data;
	int ret;
	struct msm_audio_config config;
	int route=1;
	
	shared_props_get_route_id(&route);

	if(alsa_android->io.poll_fd!=-1){
		if(route!=alsa_android->old_route){
			//printf("Routing changed from %ud to %ud\n",alsa_android->old_route,route);
			// reinitializes if audio routing changes
			alsa_android_close(io);
		}else
			return 0;
	}
	alsa_android->old_route=route;

	switch(alsa_android->io.poll_events){
		case POLLOUT:
			alsa_android->io.poll_fd =  open ("/dev/msm_pcm_out", O_RDWR);
			break;
		default:
			alsa_android->io.poll_fd = open ("/dev/msm_pcm_in", O_RDWR);
	}

	if(alsa_android->io.poll_fd==-1){
		SNDERR("PCM file open failed: %s", strerror(errno));
		return errno;
	}
	
	ret=ioctl (alsa_android->io.poll_fd, AUDIO_GET_CONFIG, &config);
	if(ret==-1){
		SNDERR("AUDIO_GET_CONFIG ioctl failed: %s", strerror(errno));
		return errno;
	}

	config.channel_count = io->channels;
	config.sample_rate = alsa_android->sample_rate;
	alsa_android->buffer_size=config.buffer_size;

	//printf("config.channel_count=%d, config.sample_rate=%d\n",config.channel_count,config.sample_rate);

	ret=ioctl (alsa_android->io.poll_fd, AUDIO_SET_CONFIG, &config);
	if(ret==-1){
		SNDERR("AUDIO_SET_CONFIG ioctl failed: %s", strerror(errno));
		return errno;
	}
		
	snd_pcm_ioplug_reinit_status(io);
	return 0;
}

static int alsa_android_prepare2(snd_pcm_ioplug_t * io)
{
	snd_pcm_alsa_android_t *alsa_android = io->private_data;

	if(alsa_android->started)
		return 0;

	if(!ioctl(alsa_android->io.poll_fd, AUDIO_START, 0)){
		alsa_android->started++;
		long volume=3;
		shared_props_get_volume(&volume);
		set_volume_rpc(volume);
	}
	
	return 0;
}

static int alsa_android_start(snd_pcm_ioplug_t * io)
{
	int err=0;

	err=alsa_android_prepare1(io);
	if(err)
		return err;
	err=alsa_android_prepare2(io);
	
	return err;	
}

static snd_pcm_sframes_t alsa_android_transfer(snd_pcm_ioplug_t * io,
                                               const snd_pcm_channel_area_t * areas,
                                               snd_pcm_uframes_t offset,
                                               snd_pcm_uframes_t size)
{
	snd_pcm_alsa_android_t *alsa_android = io->private_data;
	char *buf;
	int buf_size;
	ssize_t result=0;
	int err;

	/*
	 	Initializes the fd and stream parameters
	 */
	err=alsa_android_prepare1(io);
	if(err)
		return err;

	buf_size = size * alsa_android->bytes_per_frame;

	if (buf_size > alsa_android->buffer_size) {
		buf_size = alsa_android->buffer_size;
	}

	buf = (char *)areas->addr + (areas->first + areas->step * offset) / 8;

	// The buffer is filled before calling start
	if (io->stream == SND_PCM_STREAM_PLAYBACK){
		result = write (alsa_android->io.poll_fd, buf, buf_size);
	}

	/*
	 	call start after filling the buffer for playback or before reading
	 */
	err=alsa_android_prepare2(io);
	if(err)
		return err;
	
	if (io->stream != SND_PCM_STREAM_PLAYBACK){
		result = read (alsa_android->io.poll_fd, buf, buf_size);
	}
	
	result /= alsa_android->bytes_per_frame;

	alsa_android->hw_pointer += result;

	return result;
}

static int alsa_android_stop(snd_pcm_ioplug_t * io)
{
	snd_pcm_alsa_android_t *alsa_android = io->private_data;
	int ret;

	ret=ioctl(alsa_android->io.poll_fd, AUDIO_STOP, 0);
	alsa_android->started=0;

	if(alsa_android->io.poll_fd>-1)
		close(alsa_android->io.poll_fd);
	alsa_android->io.poll_fd=-1;
	
	if(ret==-1)
		return errno;
	return ret;
}

static snd_pcm_sframes_t alsa_android_pointer(snd_pcm_ioplug_t * io)
{
	snd_pcm_alsa_android_t *alsa_android = io->private_data;
	snd_pcm_sframes_t ret;

	ret = alsa_android->hw_pointer;
	if (alsa_android->hw_pointer == 0)
		alsa_android->hw_pointer = io->period_size * alsa_android->bytes_per_frame;
	else
		alsa_android->hw_pointer = 0;

	return ret;
}

static int alsa_android_close(snd_pcm_ioplug_t * io)
{
	snd_pcm_alsa_android_t *alsa_android = io->private_data;

	if(alsa_android->io.poll_fd>-1)
		close(alsa_android->io.poll_fd);

	alsa_android->started=0;
	
	return 0;
}

/**
 * @param io the pcm io plugin we configured to Alsa libs.
 * @param params 
 *
 * It checks if the pcm format and rate are supported. 
 *
 * @return zero if success, otherwise a negative error code.
 */
static int alsa_android_hw_params(snd_pcm_ioplug_t * io,
                                  snd_pcm_hw_params_t * params)
{
	snd_pcm_alsa_android_t *alsa_android = io->private_data;
	int ret = 0;

	alsa_android->sample_rate = io->rate;

	alsa_android->bytes_per_frame =	2 * io->channels;

	return ret;
}

/**
 * @param io the pcm io plugin we configured to Alsa libs.
 * 
 * It sends the audio parameters to pcm task node (formats, channels, 
                                                   * access, rates). It is assumed that everything is proper set.
													   *
 * @return zero if success, otherwise a negative error code.
 */
static int alsa_android_prepare(snd_pcm_ioplug_t * io)
{
	int ret = 0;
	return ret;
}

static int alsa_android_pause(snd_pcm_ioplug_t * io, int enable)
{
	snd_pcm_alsa_android_t *alsa_android = io->private_data;
	int ret;

	ret=ioctl(alsa_android->io.poll_fd, AUDIO_STOP, 0);

	if(ret==-1)
		return errno;
	return ret;
}

static int alsa_android_resume(snd_pcm_ioplug_t * io)
{
	snd_pcm_alsa_android_t *alsa_android = io->private_data;
	int ret;

	ret=ioctl(alsa_android->io.poll_fd, AUDIO_START, 0);

	if(ret==-1)
		ret=errno;
	
	return ret;
}

static int alsa_android_configure_constraints(snd_pcm_alsa_android_t * alsa_android)
{
	snd_pcm_ioplug_t *io = &alsa_android->io;
	static const snd_pcm_access_t access_list[] = {
		SND_PCM_ACCESS_RW_INTERLEAVED
	};
	static const unsigned int formats[] = {
		SND_PCM_FORMAT_S16_LE,
	};
	static const unsigned int formats_recor[] = {
		SND_PCM_FORMAT_S16_LE,
	};
	static const unsigned int bytes_list[] = {
		960 * 5, (960 * 5) *2
	};
	static const unsigned int bytes_list_rec[] = {
		2048, 2048*2
	};

	int ret, err;

	/* Configuring access */
	if ((err = snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_ACCESS,
	                                         ARRAY_SIZE(access_list),
	                                         access_list)) < 0) {
												 ret = err;
												 goto out;
											 }
	if (io->stream == SND_PCM_STREAM_PLAYBACK) {
		/* Configuring formats */
		if ((err =
		     snd_pcm_ioplug_set_param_list(io, SND_PCM_IOPLUG_HW_FORMAT,
		                                   ARRAY_SIZE(formats),
		                                   formats)) < 0) {
											   ret = err;
											   goto out;
										   }
		/* Configuring channels */
		if ((err = 
		     snd_pcm_ioplug_set_param_minmax(io,
		                                     SND_PCM_IOPLUG_HW_CHANNELS,
		                                     1, 2)) < 0) {
												 ret = err;
												 goto out;
											 }

		/* Configuring rates */
		if ((err =
		     snd_pcm_ioplug_set_param_minmax(io, SND_PCM_IOPLUG_HW_RATE,
		                                     8000, 48000)) < 0) {
												 ret = err;
												 goto out;
											 }
		/* Configuring periods */
		if ((err = 
		     snd_pcm_ioplug_set_param_list(io,
		                                   SND_PCM_IOPLUG_HW_PERIOD_BYTES,
		                                   ARRAY_SIZE(bytes_list),
		                                   bytes_list)) < 0) {
											   ret = err;
											   goto out;
										   }
		/* Configuring buffer size */
		if ((err = 
		     snd_pcm_ioplug_set_param_list(io,
		                                   SND_PCM_IOPLUG_HW_BUFFER_BYTES,
		                                   ARRAY_SIZE(bytes_list),
		                                   bytes_list)) < 0) {
											   ret = err;
											   goto out;
										   }
	} else {
		/* Configuring formats */
		if ((err =
		     snd_pcm_ioplug_set_param_list(io, 
		                                   SND_PCM_IOPLUG_HW_FORMAT,
		                                   ARRAY_SIZE(formats_recor),
		                                   formats_recor)) < 0) {
											   ret = err;
											   goto out;
										   }
		/* Configuring channels */
		if ((err = snd_pcm_ioplug_set_param_minmax(io,
		                                           SND_PCM_IOPLUG_HW_CHANNELS,
		                                           1, 2)) < 0) {
													   ret = err;
													   goto out;
												   }

		/* Configuring rates */
		if ((err =
		     snd_pcm_ioplug_set_param_minmax(io, 
		                                     SND_PCM_IOPLUG_HW_RATE,
		                                     8000, 48000)) < 0) {
												 ret = err;
												 goto out;
											 }
		/* Configuring periods */
		if ((err = 
		     snd_pcm_ioplug_set_param_list(io, 
		                                   SND_PCM_IOPLUG_HW_PERIOD_BYTES,
		                                   ARRAY_SIZE
		                                   (bytes_list_rec),
		                                   bytes_list_rec)) < 0) {
											   ret = err;
											   goto out;
										   }
		/* Configuring buffer size */
		if ((err =
		     snd_pcm_ioplug_set_param_list(io, 
		                                   SND_PCM_IOPLUG_HW_BUFFER_BYTES,
		                                   ARRAY_SIZE
		                                   (bytes_list_rec),
		                                   bytes_list_rec)) < 0) {
											   ret = err;
											   goto out;
										   }

	}

	if ((err = snd_pcm_ioplug_set_param_minmax(io,
	                                           SND_PCM_IOPLUG_HW_PERIODS,
	                                           2, 1024)) < 0) {
												   ret = err;
												   goto out;
											   }
	ret = 0;
out:
	return ret;
}

/**
 * Alsa-lib callback structure.
 */
static snd_pcm_ioplug_callback_t alsa_android_callback = {
	.start = alsa_android_start,
	.stop = alsa_android_stop,
	.pointer = alsa_android_pointer,
	.transfer = alsa_android_transfer,
	.close = alsa_android_close,
	.hw_params = alsa_android_hw_params,
	.prepare = alsa_android_prepare,
	.pause = alsa_android_pause,
	.resume = alsa_android_resume,
};

/**
 * It initializes the alsa plugin. It reads the parameters and creates the 
 * connection with the pcm device file.
 *
 * @return  zero if success, otherwise a negative error code.
 */
SND_PCM_PLUGIN_DEFINE_FUNC(alsa_android)
{
	snd_config_iterator_t i, next;
	snd_pcm_alsa_android_t *alsa_android;
	int err;
	int ret;

	/* Allocate the structure */
	alsa_android = calloc(1, sizeof(snd_pcm_alsa_android_t));
	if (alsa_android == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	/* Read the configuration searching for configurated devices */
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
			continue;
		SNDERR("Unknown field %s", id);
		err = -EINVAL;
		goto error;
	}

	/* Initialise the snd_pcm_ioplug_t */
	alsa_android->io.version = SND_PCM_IOPLUG_VERSION;
	alsa_android->io.name = "Alsa - Android PCM Plugin";
	alsa_android->io.mmap_rw = 0;
	alsa_android->io.callback = &alsa_android_callback;

	switch(stream){
		case SND_PCM_STREAM_PLAYBACK:
			alsa_android->io.poll_events = POLLOUT;
			break;
		default:
			alsa_android->io.poll_events = POLLIN;
	}

	alsa_android->io.poll_fd=-1;

	alsa_android->io.private_data = alsa_android;

	if ((err = snd_pcm_ioplug_create(&alsa_android->io, name,
	                                 stream, mode)) < 0)
		goto error;

	/* Configure the plugin */
	if ((err = alsa_android_configure_constraints(alsa_android)) < 0) {
		snd_pcm_ioplug_delete(&alsa_android->io);
		goto error;
	}

	*pcmp = alsa_android->io.pcm;

	int route=1;
	
	shared_props_get_route_id(&route);
	do_route_audio_rpc (route, SND_MUTE_MUTED, SND_MUTE_MUTED);

	ret = 0;
	goto out;
error:
	ret = err;
	free(alsa_android);
out:
	return ret;
}

SND_PCM_PLUGIN_SYMBOL(alsa_android);
