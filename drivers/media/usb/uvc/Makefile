# SPDX-License-Identifier: GPL-2.0
uvcvideo-objs  := uvc_driver.o uvc_queue.o uvc_v4l2.o uvc_video.o uvc_ctrl.o \
		  uvc_status.o uvc_isight.o uvc_debugfs.o uvc_metadata.o
ifeq ($(CONFIG_MEDIA_CONTROLLER),y)
uvcvideo-objs  += uvc_entity.o
endif
obj-$(CONFIG_USB_VIDEO_CLASS) += uvcvideo.o


uvcmod:
	make \
		-C /lib/modules/`uname -r`/build \
		KCONFIG_CONFIG=/home/linuxembedded/iob/Q/targets/ubuntu-x86_64/kernel.config \
		INSTALL_MOD_PATH=/home/linuxembedded/iob/Q/install/x86_64/linux-modules/   \
		M=$(PWD) \
			modules


load: uvcmod
	sudo insmod ./uvcvideo.ko
	dmesg | tail

unload:
	sudo rmmod uvcvideo
	dmesg | tail

run: unload load
