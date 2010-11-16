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


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <alsa/asoundlib.h>
#include <sys/shm.h>

#ifndef uint32_t
#define uint32_t unsigned int
#endif
#include <linux/msm_audio.h>

#include "utils.h"

struct shared_props_s{
	int is_initialized;
	long volume;
	unsigned int route;
	int route_id;
	long rec_flag;
};

static int shared_props_initialized=0;
struct shared_props_s *shared_props;

static int shared_props_init(void)
{
	if(shared_props_initialized)
		return 0;

	int fd=open("/tmp/alsa_android", O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
	close(fd);

	key_t key=ftok("/tmp/alsa_android", 'D');
	if(key==-1){
		SNDERR("Shared key generation failed");
		return errno;
	}

	int shm_id=shmget(key, sizeof(*shared_props), IPC_CREAT  | 0666);
	shared_props=shmat(shm_id, NULL, 0);
	if(shared_props==(void *)-1){
		SNDERR("Shared memory access failed with id=%d", shm_id);
		return errno;
	}

	shared_props_initialized=1;

	if(!shared_props->is_initialized){
		// Set the default values
		shared_props->volume=3;
		shared_props->route=1;
		shared_props->route_id=1;
		shared_props->is_initialized=1;
	}
	return 0;
}

int shared_props_get_volume(long *value)
{
	int ret=shared_props_init();
	if(ret)
		return ret;

	*value=shared_props->volume;
	return 0;
}

int shared_props_get_rec_flag(long *value)
{
	int ret=shared_props_init();
	if(ret)
		return ret;

	*value=shared_props->rec_flag;
	return 0;
}

int shared_props_get_route(unsigned int *value)
{
	int ret=shared_props_init();
	if(ret)
		return ret;

	*value=shared_props->route;
	return 0;
}

int shared_props_get_route_id(int *value)
{
	int ret=shared_props_init();
	if(ret)
		return ret;

	*value=shared_props->route_id;
	return 0;
}

int shared_props_set_volume(long value)
{
	int ret=shared_props_init();
	if(ret)
		return ret;

	shared_props->volume=value;
	return 0;
}

int shared_props_set_rec_flag(long value)
{
	int ret=shared_props_init();
	if(ret)
		return ret;

	shared_props->rec_flag=value;
	return 0;
}

int shared_props_set_route(unsigned int value)
{
	int ret=shared_props_init();
	if(ret)
		return ret;

	shared_props->route=value;
	return 0;
}

int shared_props_set_route_id(int value)
{
	int ret=shared_props_init();
	if(ret)
		return ret;

	shared_props->route_id=value;
	return 0;
}

int set_volume_rpc(int volume)
{
	int i;
	int fd = open("/dev/msm_snd", O_RDWR);
	if (fd < 0) {
		return errno;
	}

	struct msm_snd_volume_config args;
	args.method = SND_METHOD_VOICE;
	args.volume = volume;

	/* Set the volume for the following devices:
	 	0: HANDSET
		1: SPEAKER
		2: HEADSET
		3: BT
	*/
	for(i=0;i<=3;i++){
		args.device = i;
		if (ioctl(fd, SND_SET_VOLUME, &args) < 0) {
			printf("set_volume_rpc failed\n");
			close(fd);
			return errno;
		}
	}
	close(fd);

	return 0;
}
