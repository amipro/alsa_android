## Process this file with automake to produce Makefile.in

asound_module_pcm_alsa_android_LTLIBRARIES = libasound_module_pcm_alsa_android.la
asound_module_ctl_alsa_android_LTLIBRARIES = libasound_module_ctl_alsa_android.la

asound_module_pcm_alsa_androiddir = /usr/lib/alsa-lib
asound_module_ctl_alsa_androiddir = /usr/lib/alsa-lib

AM_CFLAGS = -Wall -O2 $(ALSA_ANDROID_CFLAGS)
AM_LDFLAGS = -module -avoid-version -export-dynamic -no-undefined -lasound -lpthread -lrt

libasound_module_pcm_alsa_android_la_SOURCES = alsa-android.c utils.c utils.h
libasound_module_ctl_alsa_android_la_SOURCES = ctl-android.c utils.c utils.h
