#!/bin/sh
# MTV3: инициализация графа "MP без ISPP" (идемпотентно). Запускать до демона.
# Размер подставляется по фактическому режиму сенсора.

W=${1:-2112}
H=${2:-1568}
M=/dev/media0

# aiq-приложений быть не должно
killall rkisp_demo rkmedia_vi_get_frame_test 2>/dev/null

# мост OFF, mainpath ON (двигается только на остановленном пайплайне)
media-ctl -d $M -l '"rkisp-isp-subdev":2->"rkisp-bridge-ispp":0[0]' || exit 1
media-ctl -d $M -l '"rkisp-isp-subdev":2->"rkisp_mainpath":0[1]'   || exit 1

# форматы тракта
media-ctl -d $M -V "\"m00_b_ov13850 1-0010\":0 [fmt:SBGGR10_1X10/${W}x${H}]"
media-ctl -d $M -V "\"rkisp-isp-subdev\":0 [fmt:SBGGR10_1X10/${W}x${H}]"
media-ctl -d $M -V "\"rkisp-isp-subdev\":2 [fmt:YUYV8_2X8/${W}x${H}]"

# readback-контроль: фактический формат MP обязан совпасть с запрошенным
V=$(grep -l rkisp_mainpath /sys/class/video4linux/video*/name | head -1)
V=/dev/${V#/sys/class/video4linux/}; V=${V%/name}
v4l2-ctl -d $V -v width=$W,height=$H,pixelformat=NV12
v4l2-ctl -d $V -V | grep -E "Width|Bytes"
echo "MP ready: $V ${W}x${H}"
