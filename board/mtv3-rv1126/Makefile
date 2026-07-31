# MTV3 camera stack — кросс-сборка под RV1126 (buildroot SDK)
# Использование:  make            (демон)
#                 make rknn       (RGA+RKNN пример, нужны librga/librknn_api)
#                 make deploy     (копия на SD, путь поправить)

SDK     ?= $(HOME)/proj/rv1126_sdk_ai
OUT     ?= $(SDK)/buildroot/output/rockchip_rv1126_rv1109_mtv3
CC      := $(OUT)/host/bin/arm-linux-gnueabihf-gcc
SYSROOT := $(OUT)/staging

# если rkisp2-config.h не попал в sysroot — локальная копия ТОЛЬКО этого
# заголовка (весь kernel/include/uapi в -I нельзя: его videodev2.h тянет
# linux/compiler.h, которого нет в юзерспейсе)
NEED_INC := $(shell test -f $(SYSROOT)/usr/include/linux/rkisp2-config.h || echo yes)
UAPI     := $(if $(NEED_INC),-Iinc)

CFLAGS  := -O2 -Wall $(UAPI)
LDLIBS  := -lm

all: mtv3_cam_daemon

inc/linux/rkisp2-config.h:
	mkdir -p inc/linux
	cp $(SDK)/kernel/include/uapi/linux/rkisp2-config.h $@

mtv3_cam_daemon: mtv3_cam_daemon.c $(if $(NEED_INC),inc/linux/rkisp2-config.h)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# отладочный MJPEG-сервер (кадры от mainCV --dump по RNDIS)
httpd: mtv3_httpd
mtv3_httpd: mtv3_httpd.c
	$(CC) -O2 -Wall -o $@ $<

# rknpu ставится свежей сборкой в buildroot/output (не в output/rockchip_*):
RKNN_SYS ?= $(SDK)/buildroot/output/host/arm-buildroot-linux-gnueabihf/sysroot

rknn: mtv3_rga_rknn
mtv3_rga_rknn: mtv3_rga_rknn.c
	$(CC) $(CFLAGS) -I$(RKNN_SYS)/usr/include/rknn \
		-o $@ $< -lrga $(RKNN_SYS)/usr/lib/librknn_api.so -lpthread -ldl $(LDLIBS)

SDCARD ?= /media/$(USER)/USD
deploy: all
	cp mtv3_cam_daemon init_mp.sh S60mtv3 $(SDCARD)/ && sync

clean:
	rm -f mtv3_cam_daemon mtv3_rga_rknn
