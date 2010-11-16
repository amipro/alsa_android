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


int shared_props_get_volume(long *value);
int shared_props_get_rec_flag(long *value);
int shared_props_get_route(unsigned int *value);
int shared_props_get_route_id(int *value);
int shared_props_set_volume(long value);
int shared_props_set_rec_flag(long value);
int shared_props_set_route(unsigned int value);
int shared_props_set_route_id(int value);

int set_volume_rpc(int volume);
