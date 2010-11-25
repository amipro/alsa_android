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
#include <alsa/control_external.h>
#include <pthread.h>

#ifndef uint32_t
#define uint32_t unsigned int
#endif
#include <linux/msm_audio.h>

#include "utils.h"

typedef struct snd_ctl_android {
	snd_ctl_ext_t ext;
	int end_point_count;
	struct msm_snd_endpoint *end_point_list;
	pthread_t monitor_thread;
	int push_fd;
} snd_ctl_android_t;

enum{
	CTL_ANDROID_VOLUME=1,
	CTL_ANDROID_ROUTE=2,
	CTL_ANDROID_REC=3};
#define CTL_ANDROID_COUNT 2

static int do_route_audio_rpc(uint32_t device, int ear_mute, int mic_mute)
{
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

static int android_elem_list(snd_ctl_ext_t *ext, unsigned int offset, snd_ctl_elem_id_t *id)
{
	snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_MIXER);
	switch(offset){
		case 0:
			snd_ctl_elem_id_set_name(id, "PCM Playback Volume");
			break;
		case 1:
			snd_ctl_elem_id_set_name(id, "Playback Route");
			break;
		case 2:
			snd_ctl_elem_id_set_name(id, "Record Capture Switch");
			break;
	}			
	
	return 0;
}

static int android_elem_count(snd_ctl_ext_t *ext)
{
	return CTL_ANDROID_COUNT;
}

static snd_ctl_ext_key_t android_find_elem(snd_ctl_ext_t *ext, const snd_ctl_elem_id_t *id)
{
	unsigned int numid;

	numid = snd_ctl_elem_id_get_numid(id);
	if(numid>3)
		return SND_CTL_EXT_KEY_NOT_FOUND;
	
	return numid;
}

static int android_get_attribute(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key,
			     int *type, unsigned int *acc, unsigned int *count)
{
	switch(key){
		case CTL_ANDROID_VOLUME:
			// Master volume
			*type = SND_CTL_ELEM_TYPE_INTEGER;
			*count = 1;
			break;
		case CTL_ANDROID_ROUTE:
			// Audio Route
			*type = SND_CTL_ELEM_TYPE_ENUMERATED;
			*count = 1;
			break;
		case CTL_ANDROID_REC:
			// Record enable
			*type = SND_CTL_ELEM_TYPE_BOOLEAN;
			*count = 1;
			break;
	}
	*acc = SND_CTL_EXT_ACCESS_READWRITE;

	return 0;
}

static int android_get_integer_info(snd_ctl_ext_t *ext ATTRIBUTE_UNUSED,
				snd_ctl_ext_key_t key ATTRIBUTE_UNUSED,
				long *imin, long *imax, long *istep)
{
	*istep = 0;
	*imin = 0;
	*imax = 5;
	return 0;
}

static int android_write_integer(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key, long *value)
{
	int ret=-1;

	switch(key){
		case CTL_ANDROID_VOLUME:
			ret=shared_props_set_volume(*value);
			break;
		case CTL_ANDROID_REC:
			ret=shared_props_set_rec_flag(*value);
			break;
	}
	
	if(ret)
		return ret;
	
	return set_volume_rpc(*value);
}

static int android_read_integer(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key, long *value)
{
	int ret=-1;
	
	switch(key){
		case CTL_ANDROID_VOLUME:
			ret=shared_props_get_volume(value);
			break;
		case CTL_ANDROID_REC:
			ret=shared_props_get_rec_flag(value);
			break;
	}

	return ret;
}

static int android_get_enumerated_info(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key ATTRIBUTE_UNUSED, unsigned int *items)
{
	snd_ctl_android_t *android = ext->private_data;

	//printf("android_get_enumerated_info %d\n",android->end_point_count);
	*items = android->end_point_count;
	
	return 0;
}

static int android_get_enumerated_name(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key ATTRIBUTE_UNUSED, unsigned int item, char *name, size_t name_max_len)
{
	snd_ctl_android_t *android = ext->private_data;

	if (item >= android->end_point_count)
		return -EINVAL;

	//printf("android_get_enumerated_name %d %s\n",item, android->end_point_list[item].name);
	strncpy(name, android->end_point_list[item].name, name_max_len - 1);
	name[name_max_len - 1] = 0;
	
	return 0;
}

static int android_write_enumerated(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key ATTRIBUTE_UNUSED,	unsigned int *items)
{
	snd_ctl_android_t *android = ext->private_data;

	int id=android->end_point_list[*items].id;

	int ret=shared_props_set_route(*items);
	if(ret)
		return ret;
	shared_props_set_route_id(id);
	
	return do_route_audio_rpc(id,SND_MUTE_MUTED,SND_MUTE_MUTED);
}

static int android_read_enumerated(snd_ctl_ext_t *ext, snd_ctl_ext_key_t key ATTRIBUTE_UNUSED, unsigned int *items)
{
	return shared_props_get_route(items);
}

static void android_close(snd_ctl_ext_t *ext)
{
	snd_ctl_android_t *android = ext->private_data;

	pthread_cancel(android->monitor_thread);
	close(android->ext.poll_fd);
	close(android->push_fd);
	if(android && android->end_point_list)
		free(android->end_point_list);
	if(android)
		free(android);
}

// Values changes are pushed from the monitor thread
static int android_read_event(snd_ctl_ext_t *ext,
                          snd_ctl_elem_id_t *id,
                          unsigned int *event_mask)
{
	int i;
	int ret=read(ext->poll_fd, &i, sizeof(int));
	if(ret==sizeof(int)){
		android_elem_list(ext, i, id);
		*event_mask = SND_CTL_EVENT_MASK_VALUE;
	}

	return ret;
}

// Monitor changes in the values. It is running in a seperate thread
void *android_monitor(void *arg)
{
	snd_ctl_android_t *android=(snd_ctl_android_t *)arg;
	long old_volume=0;
	unsigned int old_route=0;
	long volume=0;
	unsigned int route=0;
	int control;

	while(1){
		sleep(2);
		if(!shared_props_get_volume(&volume)){
			if(volume!=old_volume){
				old_volume=volume;
				control=0;
				write(android->push_fd, &control, sizeof(control));
			}
		}
		if(!shared_props_get_route(&route)){
			if(route!=old_route){
				old_route=route;
				control=1;
				write(android->push_fd, &control, sizeof(control));
			}
		}
	}
}

static snd_ctl_ext_callback_t android_ext_callback = {
	.close = android_close,
	.elem_count = android_elem_count,
	.elem_list = android_elem_list,
	.find_elem = android_find_elem,
	.get_attribute = android_get_attribute,
	.get_integer_info = android_get_integer_info,
	.get_enumerated_info = android_get_enumerated_info,
	.get_enumerated_name = android_get_enumerated_name,
	.read_integer = android_read_integer,
	.read_enumerated = android_read_enumerated,
	.write_integer = android_write_integer,
	.write_enumerated = android_write_enumerated,
	.read_event = android_read_event,
};


SND_CTL_PLUGIN_DEFINE_FUNC(alsa_android)
{
	snd_config_iterator_t it, next;
	int err, fd=-1, i;
	snd_ctl_android_t *android=0;
	int pipes[2];

	snd_config_for_each(it, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(it);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
			continue;
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}

	if(pipe(pipes))		// Used by the monitor thread to push changes
		return errno;
	
	fcntl(pipes[0], F_SETFL, O_NONBLOCK);
	fcntl(pipes[1], F_SETFL, O_NONBLOCK);

	android = calloc(1, sizeof(*android));
	
	android->ext.version = SND_CTL_EXT_VERSION;
	android->ext.card_idx = 0; /* FIXME */
	strcpy(android->ext.driver, "Alsa - Android PCM Plugin");
	strcpy(android->ext.name, "Alsa - Android PCM Plugin");
	android->ext.poll_fd = pipes[0];
	android->push_fd = pipes[1];
	android->ext.callback = &android_ext_callback;
	android->ext.private_data = android;

	fd = open ("/dev/msm_snd", O_RDWR);
	if(fd==-1){
		SNDERR("Error opening file /dev/msm_snd\n");
		err=errno;
		goto error;
	}
		
	if(ioctl (fd, SND_GET_NUM_ENDPOINTS,&android->end_point_count)){
		SNDERR("ioctl SND_GET_NUM_ENDPOINTS failed\n");
		err=errno;
		goto error;
	}

	android->end_point_list=calloc(android->end_point_count,sizeof(android->end_point_list[0]));
	
	for (i = 0; i < android->end_point_count; i++)
	{
		android->end_point_list[i].id = i;
		if(ioctl (fd, SND_GET_ENDPOINT, &android->end_point_list[i])){
			SNDERR("ioctl SND_GET_ENDPOINT failed\n");
			err=errno;
			goto error;
		}
	}
	close(fd);

	err = snd_ctl_ext_create(&android->ext, name, mode);
	if (err < 0)
		goto error;

	pthread_create(&android->monitor_thread, NULL, android_monitor, android);
	
	*handlep = android->ext.handle;
	return 0;

error:
	close(pipes[0]);
	close(pipes[1]);
	if(fd>-1)
		close(fd);
	if(android && android->end_point_list)
		free(android->end_point_list);
	if(android)
		free(android);
	return err;
}

SND_CTL_PLUGIN_SYMBOL(alsa_android);
