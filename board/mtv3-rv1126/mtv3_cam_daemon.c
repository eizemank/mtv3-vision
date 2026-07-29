/*
 * mtv3_cam_daemon.c — боевой демон MTV3: rkisp mainpath без rkaiq/ISPP.
 *   захват NV12 (MPLANE MMAP) + AE (гистограмма Y -> exposure/gain сенсора)
 *   + AWB gray-world (выходные U/V -> AWB_GAIN через rkisp-input-params)
 *   + BLS/GAMMA_OUT при старте.
 *
 * Объединяет и заменяет mtv3_mp_ae.c и mtv3_isp_params.c (те остаются как
 * отладочные утилиты). Граф должен быть поднят init_mp.sh.
 *
 * Сборка: $CC -O2 -Wall -o mtv3_cam_daemon mtv3_cam_daemon.c -lm
 * Запуск: mtv3_cam_daemon [-w 2112] [-h 1568] [-t 110] [-f 30] [--hz 50]
 *                         [-b 256] [--awb 1] [-n 0] [-o /tmp/dump.nv12] [--shm]
 *
 * --shm: публикация кадров в /dev/shm/mtv3cam (ринг NV12 на 3 слота) для
 * приложения (SeeSharpPy ShmSource). Для CV задавайте уменьшенный -w/-h
 * (напр. 1056x784) — скейлит resizer mainpath, бесплатно.
 *
 * Контуры:
 *   AE  — каждый кадр замер, коррекция раз в SETTLE кадров (латентность сенсора);
 *   AWB — раз в AWB_PERIOD кадров, цель mean(U)=mean(V)=128 по зонам с валидным Y.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

/* rkisp2-config.h написан в kernel-стиле: шим типов для юзерспейса */
#include <linux/types.h>
typedef __u8  u8;  typedef __u16 u16; typedef __u32 u32; typedef __u64 u64;
typedef __s8  s8;  typedef __s16 s16; typedef __s32 s32; typedef __s64 s64;
#ifndef BIT
#define BIT(x)     (1UL << (x))
#endif
#ifndef BIT_ULL
#define BIT_ULL(x) (1ULL << (x))
#endif
#include <linux/rkisp2-config.h>

/* старые sysroot-заголовки пребилт-тулчейна не знают META_OUTPUT (ядро 4.19+ его имеет) */
#ifndef V4L2_BUF_TYPE_META_OUTPUT
#define V4L2_BUF_TYPE_META_OUTPUT 14
#endif

#define NBUF 4
#define SETTLE 3
#define AWB_PERIOD 15
#define GAIN_MIN 16
#define GAIN_MAX 248
#define EXP_MIN 4
#define AWB_MIN 128            /* 0.5x */
#define AWB_MAX 2048           /* 8.0x — тёплые источники требуют >4x по синему */
#define AWB_G_MIN 180          /* нижний предел зелёного (fallback на тёплом свете) */

static int xioctl(int fd, unsigned long req, void *arg)
{
	int r;
	do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
	return r;
}

static int find_node(const char *substr, const char *prefix, char *out, size_t olen)
{
	DIR *d = opendir("/sys/class/video4linux");
	struct dirent *e;
	char path[256], name[128];

	if (!d)
		return -1;
	while ((e = readdir(d))) {
		FILE *f;
		if (strncmp(e->d_name, prefix, strlen(prefix)))
			continue;
		snprintf(path, sizeof(path), "/sys/class/video4linux/%s/name", e->d_name);
		f = fopen(path, "r");
		if (!f)
			continue;
		if (fgets(name, sizeof(name), f) && strstr(name, substr)) {
			fclose(f);
			snprintf(out, olen, "/dev/%s", e->d_name);
			closedir(d);
			return 0;
		}
		fclose(f);
	}
	closedir(d);
	return -1;
}

static int s_ctrl(int fd, uint32_t id, int32_t val)
{
	struct v4l2_control c = { .id = id, .value = val };
	return xioctl(fd, VIDIOC_S_CTRL, &c);
}

static int g_ctrl(int fd, uint32_t id, int32_t *val)
{
	struct v4l2_control c = { .id = id };
	if (xioctl(fd, VIDIOC_G_CTRL, &c) < 0)
		return -1;
	*val = c.value;
	return 0;
}

static int query_range(int fd, uint32_t id, int32_t *mn, int32_t *mx)
{
	struct v4l2_queryctrl q = { .id = id };
	if (xioctl(fd, VIDIOC_QUERYCTRL, &q) < 0)
		return -1;
	*mn = q.minimum;
	*mx = q.maximum;
	return 0;
}

/* ================= ISP params (BLS/GAMMA/AWB_GAIN) ================= */

struct params_ctx {
	int fd;
	void *map;
	size_t maplen;
	struct v4l2_buffer buf;
	int bls;
	uint16_t gr, gg, gb;    /* AWB-гейны, 1x=256 */
	uint8_t sat;            /* CPROC насыщенность, 0x80=1.0x */
	int ccm_en;
	int ccm_update;         /* пометка: слать CCM в cfg_update следующим push */
	int16_t ccm_m[9];       /* CCM, Q7: 1.0 = 128 (полная матрица) */
	int16_t ccm_o[3];       /* офсеты, пиксельный домен */
	int started;
};

static void params_fill(struct params_ctx *p, int first)
{
	struct isp2x_isp_params_cfg *cfg = p->map;
	int i;

	memset(cfg, 0, sizeof(*cfg));
	cfg->module_ens = ISP2X_MODULE_BLS | ISP2X_MODULE_GOC |
			  ISP2X_MODULE_AWB_GAIN | ISP2X_MODULE_CPROC;
	if (p->ccm_en)
		cfg->module_ens |= ISP2X_MODULE_CCM;
	cfg->module_en_update = first ? cfg->module_ens : 0;
	cfg->module_cfg_update = first ? cfg->module_ens :
		(ISP2X_MODULE_AWB_GAIN |
		 (p->ccm_update ? ISP2X_MODULE_CCM : 0));
	p->ccm_update = 0;
	cfg->frame_id = 0;

	cfg->others.bls_cfg.fixed_val.r  = p->bls;
	cfg->others.bls_cfg.fixed_val.gr = p->bls;
	cfg->others.bls_cfg.fixed_val.gb = p->bls;
	cfg->others.bls_cfg.fixed_val.b  = p->bls;

	cfg->others.gammaout_cfg.equ_segm = 1;   /* VERIFY: см. isp_params_v2x.c */
	cfg->others.gammaout_cfg.offset = 0;
	for (i = 0; i < ISP2X_GAMMA_OUT_MAX_SAMPLES; i++) {
		double x = (double)i / (ISP2X_GAMMA_OUT_MAX_SAMPLES - 1);
		cfg->others.gammaout_cfg.gamma_y[i] =
			(uint16_t)(4095.0 * pow(x, 1.0 / 2.2) + 0.5);
	}

	cfg->others.awb_gain_cfg.gain_red     = p->gr;
	cfg->others.awb_gain_cfg.gain_green_r = p->gg;
	cfg->others.awb_gain_cfg.gain_green_b = p->gg;
	cfg->others.awb_gain_cfg.gain_blue    = p->gb;

	/* CPROC: насыщенность/контраст (0x80 = 1.0x), сдвиги нулевые.
	 * VERIFY: семантика *_range (1 = full range); если появится клип/серость —
	 * попробовать 0 (см. isp_params_v2x.c, cproc_config) */
	cfg->others.cproc_cfg.contrast    = 0x80;
	cfg->others.cproc_cfg.sat         = p->sat;
	cfg->others.cproc_cfg.brightness  = 0;
	cfg->others.cproc_cfg.hue         = 0;
	cfg->others.cproc_cfg.y_in_range  = 1;
	cfg->others.cproc_cfg.y_out_range = 1;
	cfg->others.cproc_cfg.c_out_range = 1;

	/* CCM: матрица 3x3 (Q7) + офсеты; альфа-кривая = 1.0 (полное применение). */
	if (p->ccm_en) {
		struct isp2x_ccm_cfg *c = &cfg->others.ccm_cfg;
		/* по rkaiq (Isp20Params.cpp:1302): диагональ пишется как
		 * (coeff-1)*128 — identity железо добавляет само */
		c->coeff0_r = p->ccm_m[0] - 128; c->coeff1_r = p->ccm_m[1];      c->coeff2_r = p->ccm_m[2];
		c->coeff0_g = p->ccm_m[3];       c->coeff1_g = p->ccm_m[4] - 128; c->coeff2_g = p->ccm_m[5];
		c->coeff0_b = p->ccm_m[6];       c->coeff1_b = p->ccm_m[7];      c->coeff2_b = p->ccm_m[8] - 128;
		c->offset_r = p->ccm_o[0]; c->offset_g = p->ccm_o[1]; c->offset_b = p->ccm_o[2];
		/* из донорского IQ (lumaCCM): RGB2Y Q7 (сумма 128), альфа-рампа
		 * 0..1024 — CCM плавно гасится в тенях (меньше цветного шума) */
		c->coeff0_y = 38; c->coeff1_y = 75; c->coeff2_y = 15;
		for (i = 0; i < ISP2X_CCM_CURVE_NUM; i++)
			c->alp_y[i] = i * 64;
		c->bound_bit = 8;   /* low_bound_pos_bit из донора */
	}
}

static int params_push(struct params_ctx *p, int first)
{
	params_fill(p, first);
	p->buf.bytesused = sizeof(struct isp2x_isp_params_cfg);
	if (xioctl(p->fd, VIDIOC_QBUF, &p->buf) < 0) {
		perror("params QBUF");
		return -1;
	}
	if (!p->started) {
		enum v4l2_buf_type t = V4L2_BUF_TYPE_META_OUTPUT;
		if (xioctl(p->fd, VIDIOC_STREAMON, &t) < 0) {
			perror("params STREAMON");
			return -1;
		}
		p->started = 1;
	}
	if (xioctl(p->fd, VIDIOC_DQBUF, &p->buf) < 0) {
		perror("params DQBUF");
		return -1;
	}
	return 0;
}

static int params_init(struct params_ctx *p)
{
	char dev[64];

	if (find_node("rkisp-input-params", "video", dev, sizeof(dev))) {
		fprintf(stderr, "rkisp-input-params not found\n");
		return -1;
	}
	p->fd = open(dev, O_RDWR);
	if (p->fd < 0) {
		perror("params open");
		return -1;
	}

	struct v4l2_requestbuffers req = {
		.count = 1,
		.type = V4L2_BUF_TYPE_META_OUTPUT,
		.memory = V4L2_MEMORY_MMAP,
	};
	if (xioctl(p->fd, VIDIOC_REQBUFS, &req) < 0) {
		perror("params REQBUFS");
		return -1;
	}
	memset(&p->buf, 0, sizeof(p->buf));
	p->buf.type = V4L2_BUF_TYPE_META_OUTPUT;
	p->buf.memory = V4L2_MEMORY_MMAP;
	p->buf.index = 0;
	if (xioctl(p->fd, VIDIOC_QUERYBUF, &p->buf) < 0) {
		perror("params QUERYBUF");
		return -1;
	}
	p->maplen = p->buf.length;
	p->map = mmap(NULL, p->buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
		      p->fd, p->buf.m.offset);
	if (p->map == MAP_FAILED) {
		perror("params mmap");
		return -1;
	}
	printf("params: %s buf=%u struct=%zu\n", dev, p->buf.length,
	       sizeof(struct isp2x_isp_params_cfg));
	return params_push(p, 1);
}

/* ============== SHM-ринг для приложения (see_sharp.ShmSource) ============== */

#define SHM_PATH  "/dev/shm/mtv3cam"
#define SHM_MAGIC 0x4D335348u          /* в файле little-endian: "HS3M" */
#define SHM_NSLOT 3

struct shm_hdr {                       /* 64 байта, данные сразу за ним */
	uint32_t magic, version;
	uint32_t w, h, stride, fmt;    /* fmt: 0 = NV12 */
	uint32_t nslot, slot_size;
	volatile uint32_t seq;         /* завершённые кадры; последний в (seq-1)%nslot */
	uint32_t ts_ms;                /* CLOCK_MONOTONIC мс последнего кадра */
	uint32_t reserved[6];
};

static struct shm_hdr *shm_hdr;
static uint8_t *shm_data;

static int shm_init(int w, int h, int stride)
{
	uint32_t slot = (uint32_t)stride * h * 3 / 2;
	size_t len = sizeof(struct shm_hdr) + (size_t)slot * SHM_NSLOT;
	int fd = open(SHM_PATH, O_RDWR | O_CREAT, 0666);

	if (fd < 0 || ftruncate(fd, len) < 0) {
		perror("shm open/truncate");
		if (fd >= 0)
			close(fd);
		return -1;
	}
	shm_hdr = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (shm_hdr == MAP_FAILED) {
		perror("shm mmap");
		shm_hdr = NULL;
		return -1;
	}
	memset(shm_hdr, 0, sizeof(*shm_hdr));
	shm_data = (uint8_t *)(shm_hdr + 1);
	shm_hdr->version = 1;
	shm_hdr->w = w;
	shm_hdr->h = h;
	shm_hdr->stride = stride;
	shm_hdr->fmt = 0;
	shm_hdr->nslot = SHM_NSLOT;
	shm_hdr->slot_size = slot;
	__sync_synchronize();
	shm_hdr->magic = SHM_MAGIC;    /* последним — признак готовности хедера */
	printf("shm: %s %dx%d stride=%d slots=%d\n", SHM_PATH, w, h, stride, SHM_NSLOT);
	return 0;
}

static void shm_write(const uint8_t *frame)
{
	struct timespec ts;

	memcpy(shm_data + (size_t)(shm_hdr->seq % SHM_NSLOT) * shm_hdr->slot_size,
	       frame, shm_hdr->slot_size);
	clock_gettime(CLOCK_MONOTONIC, &ts);
	shm_hdr->ts_ms = (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
	__sync_synchronize();          /* данные видимы до инкремента seq */
	shm_hdr->seq++;
}

/* ============================ main ============================ */

int main(int argc, char **argv)
{
	int W = 2112, H = 1568, target = 110, fps = 30, hz = 50;
	int nframes = 0, bls = 256, awb_en = 1, sat_pct = 130, ccm_sel = 0;
	int shm_en = 0;
	const char *dump = NULL;
	char vdev[64], sdev[64];
	int i, vfd, sfd;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-w")) W = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-h")) H = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-t")) target = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-f")) fps = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--hz")) hz = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-b")) bls = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--awb")) awb_en = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-s")) sat_pct = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--ccm")) {
			++i;
			ccm_sel = strcmp(argv[i], "auto") ? atoi(argv[i]) : -1;
		}
		else if (!strcmp(argv[i], "--shm")) shm_en = 1;
		else if (!strcmp(argv[i], "-n")) nframes = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-o")) dump = argv[++i];
	}

	if (find_node("rkisp_mainpath", "video", vdev, sizeof(vdev)) ||
	    find_node("ov13850", "v4l-subdev", sdev, sizeof(sdev))) {
		fprintf(stderr, "nodes not found\n");
		return 1;
	}
	vfd = open(vdev, O_RDWR);
	sfd = open(sdev, O_RDWR);
	if (vfd < 0 || sfd < 0) {
		perror("open");
		return 1;
	}
	printf("mainpath=%s sensor=%s\n", vdev, sdev);

	/* ISP params: BLS + gamma + начальный AWB 1x */
	int sat_code = 128 * sat_pct / 100;
	if (sat_code < 128) sat_code = 128;
	if (sat_code > 255) sat_code = 255;
	struct params_ctx pc = {
		.bls = bls, .gr = 256, .gg = 256, .gb = 256,
		.sat = (uint8_t)sat_code,
	};
	/* CCM-набор (все *_100 из донорского IQ ov13B10) + эталонные WB-гейны
	 * иллюминантов (wbGain из aCcmCof того же xml; r/b, 1x = 1.0). */
	static const struct {
		const char *name;
		float wbr, wbb;
		float m[12];
	} CCM_TBL[6] = {
		{ "off", 0, 0, { 1,0,0, 0,1,0, 0,0,1, 0,0,0 } },
		{ "D65", 2.1881f, 1.3199f,
		  { 1.6418f,-0.6213f,-0.0205f, -0.0999f, 1.4531f,-0.3531f,
		    -0.0476f,-0.5556f, 1.6031f, 0, 0, 0 } },
		{ "A", 1.1605f, 2.1145f,
		  { 1.4581f,-0.3178f,-0.1403f, -0.2060f, 1.3548f,-0.1488f,
		    -0.2420f,-1.1590f, 2.4010f, 0, 0, 0 } },
		{ "D50", 1.7561f, 1.4360f,
		  { 1.5771f,-0.5283f,-0.0488f, -0.1402f, 1.4610f,-0.3208f,
		    -0.0728f,-0.6265f, 1.6993f, 0, 0, 0 } },
		{ "CWF", 1.7640f, 1.9330f,
		  { 1.9018f,-0.9072f, 0.0054f, -0.2272f, 1.3212f,-0.0940f,
		    -0.0698f,-0.7535f, 1.8233f, 0, 0, 0 } },
		{ "TL84", 1.6270f, 1.8665f,
		  { 1.6531f,-0.6380f,-0.0151f, -0.1903f, 1.3728f,-0.1824f,
		    -0.0969f,-0.7617f, 1.8586f, 0, 0, 0 } },
	};
	int ccm_auto = (ccm_sel == -1);
	if (ccm_auto)
		ccm_sel = 1;                  /* старт автоселекта с D65 */
	if (ccm_sel < 0 || ccm_sel > 5)
		ccm_sel = 0;
	pc.ccm_en = (ccm_sel != 0);
	for (i = 0; i < 9; i++)
		pc.ccm_m[i] = (int16_t)lrintf(CCM_TBL[ccm_sel].m[i] * 128.0f);
	for (i = 0; i < 3; i++)
		pc.ccm_o[i] = (int16_t)CCM_TBL[ccm_sel].m[9 + i];
	int ccm_cur = ccm_sel, ccm_cand = ccm_sel, ccm_votes = 0;
	if (params_init(&pc))
		fprintf(stderr, "params init failed — работаем на дефолтах ISP\n");

	/* диапазоны сенсора */
	int32_t exp_min = EXP_MIN, exp_max = H + 100, g_min = GAIN_MIN,
		g_max = GAIN_MAX, vblank = 0, qmin, qmax;
	if (!query_range(sfd, V4L2_CID_EXPOSURE, &qmin, &qmax)) {
		exp_min = qmin > EXP_MIN ? qmin : EXP_MIN;
		exp_max = qmax;
	}
	if (!query_range(sfd, V4L2_CID_ANALOGUE_GAIN, &qmin, &qmax)) {
		g_min = qmin;
		g_max = qmax;
	}
	g_ctrl(sfd, V4L2_CID_VBLANK, &vblank);
	int vts = H + (vblank > 0 ? vblank : 40);
	double line_us = 1e6 / (double)fps / (double)vts;
	int flick = hz ? (int)((hz == 50 ? 10000.0 : 8333.0) / line_us + 0.5) : 0;
	printf("exp[%d..%d] gain[%d..%d] vts=%d line=%.2fus flick=%d\n",
	       exp_min, exp_max, g_min, g_max, vts, line_us, flick);

	/* формат и буферы MP */
	struct v4l2_format fmt = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE };
	fmt.fmt.pix_mp.width = W;
	fmt.fmt.pix_mp.height = H;
	fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
	fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
	fmt.fmt.pix_mp.num_planes = 1;
	if (xioctl(vfd, VIDIOC_S_FMT, &fmt) < 0) {
		perror("S_FMT");
		return 1;
	}
	W = fmt.fmt.pix_mp.width;
	H = fmt.fmt.pix_mp.height;
	int stride = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
	printf("negotiated %dx%d stride=%d\n", W, H, stride);

	if (shm_en && shm_init(W, H, stride))
		shm_en = 0;

	struct v4l2_requestbuffers req = {
		.count = NBUF,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.memory = V4L2_MEMORY_MMAP,
	};
	if (xioctl(vfd, VIDIOC_REQBUFS, &req) < 0) {
		perror("REQBUFS");
		return 1;
	}
	void *map[NBUF];
	size_t maplen[NBUF];
	for (i = 0; i < (int)req.count; i++) {
		struct v4l2_plane pl[1];
		struct v4l2_buffer b;
		memset(&b, 0, sizeof(b));
		memset(pl, 0, sizeof(pl));
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		b.m.planes = pl;
		b.length = 1;
		if (xioctl(vfd, VIDIOC_QUERYBUF, &b) < 0) {
			perror("QUERYBUF");
			return 1;
		}
		maplen[i] = pl[0].length;
		map[i] = mmap(NULL, pl[0].length, PROT_READ | PROT_WRITE,
			      MAP_SHARED, vfd, pl[0].m.mem_offset);
		if (map[i] == MAP_FAILED) {
			perror("mmap");
			return 1;
		}
		if (xioctl(vfd, VIDIOC_QBUF, &b) < 0) {
			perror("QBUF");
			return 1;
		}
	}
	enum v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if (xioctl(vfd, VIDIOC_STREAMON, &t) < 0) {
		perror("STREAMON");
		return 1;
	}

	int32_t cur_exp = exp_max / 2, cur_gain = g_min * 2;
	s_ctrl(sfd, V4L2_CID_ANALOGUE_GAIN, cur_gain);
	s_ctrl(sfd, V4L2_CID_EXPOSURE, cur_exp);

	FILE *df = dump ? fopen(dump, "w") : NULL;
	int settle = SETTLE, frame = 0;
	uint32_t hist[256];

	while (nframes == 0 || frame < nframes) {
		struct v4l2_plane pl[1];
		struct v4l2_buffer b;
		memset(&b, 0, sizeof(b));
		memset(pl, 0, sizeof(pl));
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		b.memory = V4L2_MEMORY_MMAP;
		b.m.planes = pl;
		b.length = 1;
		if (xioctl(vfd, VIDIOC_DQBUF, &b) < 0) {
			perror("DQBUF");
			break;
		}
		frame++;

		const uint8_t *y = map[b.index];
		const uint8_t *uv = y + (size_t)stride * H;

		/* ---- AE: гистограмма Y ---- */
		uint32_t n = 0;
		memset(hist, 0, sizeof(hist));
		for (int r = 0; r < H; r += 8) {
			const uint8_t *row = y + (size_t)r * stride;
			for (int c = 0; c < W; c += 8) {
				hist[row[c]]++;
				n++;
			}
		}
		uint32_t acc = 0;
		uint64_t ysum = 0;
		int med = 0, p99 = 255;
		for (i = 0; i < 256; i++) {
			ysum += (uint64_t)i * hist[i];
			acc += hist[i];
			if (med == 0 && acc * 2 >= n) med = i;
			if (p99 == 255 && acc * 100 >= n * 99) p99 = i;
		}
		int mean = (int)(ysum / n);

		if (df && frame % 30 == 0)
			fwrite(y, 1, (size_t)stride * H * 3 / 2, df);

		if (shm_en)
			shm_write(y);

		if (settle > 0) {
			settle--;
		} else {
			/* low-key сцена: медиана вырождается в 0-1, переходим на mean */
			int metric = med, tgt = target;
			if (med < 10) {
				metric = mean;
				tgt = target / 2;
			}
			double ratio;
			if (p99 > 250)         ratio = 0.70;
			else if (metric < 1)   ratio = 1.50;
			else                   ratio = (double)tgt / (double)metric;
			ratio = pow(ratio, 0.6);   /* демпфирование против limit-cycle */
			if (ratio > 1.5)  ratio = 1.5;
			if (ratio < 0.66) ratio = 0.66;

			if (ratio > 1.09 || ratio < 0.91) {
				double ev = (double)cur_exp * (double)cur_gain * ratio;
				int32_t ne = (int32_t)(ev / (double)(g_min * 2));
				if (flick && ne > flick)
					ne = (ne / flick) * flick;
				if (ne > exp_max - 8) ne = exp_max - 8;
				if (ne < exp_min) ne = exp_min;
				int32_t ng = (int32_t)(ev / (double)ne);
				if (ng < g_min) ng = g_min;
				if (ng > g_max) ng = g_max;
				if (ne != cur_exp || ng != cur_gain) {
					s_ctrl(sfd, V4L2_CID_ANALOGUE_GAIN, ng);
					s_ctrl(sfd, V4L2_CID_EXPOSURE, ne);
					printf("AE  f%05d med=%d p99=%d -> exp=%d gain=%d\n",
					       frame, med, p99, ne, ng);
					cur_exp = ne;
					cur_gain = ng;
					settle = SETTLE;
				}
			}
		}

		/* ---- AWB gray-world: раз в AWB_PERIOD кадров ---- */
		if (awb_en && pc.started && frame % 5 == 0 && med > 10) {
			/* robust gray-world: насыщенные зоны (цветные объекты)
			 * не голосуют; при вырожденной выборке (весь кадр со
			 * сдвигом — старт на тёплом свете) fallback на общее среднее */
			uint64_t su = 0, sv = 0, sua = 0, sva = 0;
			uint32_t cnt = 0, cnta = 0;
			for (int r = 0; r < H / 2; r += 8) {
				const uint8_t *uvrow = uv + (size_t)r * stride;
				const uint8_t *yrow = y + (size_t)(2 * r) * stride;
				for (int c = 0; c < W; c += 16) {
					uint8_t yy = yrow[c];
					if (yy < 30 || yy > 220)
						continue;      /* тени/клип не голосуют */
					int du = (int)uvrow[c] - 128;
					int dv = (int)uvrow[c + 1] - 128;
					sua += uvrow[c];
					sva += uvrow[c + 1];
					cnta++;
					if (du * du + dv * dv > 40 * 40)
						continue;      /* цветные объекты не голосуют */
					su += uvrow[c];        /* U (Cb) */
					sv += uvrow[c + 1];    /* V (Cr) */
					cnt++;
				}
			}
			if (cnt < cnta / 8) {          /* фильтр съел всё — общий сдвиг */
				su = sua; sv = sva; cnt = cnta;
			}
			if (cnt > 500) {
				double mu = (double)su / cnt, mv = (double)sv / cnt;
				double eu = (mu - 128.0) / 128.0;  /* >0: избыток синего */
				double ev2 = (mv - 128.0) / 128.0; /* >0: избыток красного */
				double emax = fabs(eu) > fabs(ev2) ? fabs(eu) : fabs(ev2);
				int go = emax > 0.04 || frame % AWB_PERIOD == 0;
				if (go && emax > 0.01) {
					double kk = emax > 0.04 ? 1.2 : 0.7;  /* быстрая фаза / точная фаза */
					double nr = pc.gr * (1.0 - kk * ev2);
					double nb = pc.gb * (1.0 - kk * eu);
					if (nr < AWB_MIN) nr = AWB_MIN;
					if (nr > AWB_MAX) nr = AWB_MAX;
					if (nb < AWB_MIN) nb = AWB_MIN;
					if (nb > AWB_MAX) nb = AWB_MAX;
					pc.gr = (uint16_t)nr;
					pc.gb = (uint16_t)nb;
					/* очень тёплый свет: b на упоре, синевы не хватает
					 * (eu<0) — балансируем опусканием зелёного;
					 * возвращаем к 256, когда свет позволяет */
					if (nb >= AWB_MAX - 1 && eu < -0.03) {
						double ngg = pc.gg * (1.0 + 0.5 * eu);
						if (ngg < AWB_G_MIN) ngg = AWB_G_MIN;
						pc.gg = (uint16_t)ngg;
					} else if (pc.gg < 256 && eu > -0.01) {
						double ngg = pc.gg * 1.05;
						if (ngg > 256) ngg = 256;
						pc.gg = (uint16_t)ngg;
					}
					if (!params_push(&pc, 0))
						printf("AWB f%05d U=%.1f V=%.1f -> r=%d g=%d b=%d\n",
						       frame, mu, mv, pc.gr, pc.gg, pc.gb);
				}
			}
		}

		/* ---- CCM-автоселект: иллюминант с ближайшим wbGain ---- */
		if (ccm_auto && pc.started && frame % 30 == 0 && med > 10) {
			double r = pc.gr / 256.0, bb = pc.gb / 256.0;
			int best = ccm_cur;
			double bd = 1e9;
			for (int s = 1; s <= 5; s++) {
				if (CCM_TBL[s].wbr <= 0)
					continue;
				double dr = log(r / CCM_TBL[s].wbr);
				double db = log(bb / CCM_TBL[s].wbb);
				double d = dr * dr + db * db;
				if (d < bd) { bd = d; best = s; }
			}
			if (best == ccm_cur) {
				ccm_votes = 0;
			} else if (best != ccm_cand) {
				ccm_cand = best;
				ccm_votes = 1;
			} else if (++ccm_votes >= 3) {      /* гистерезис ~3 c */
				for (i = 0; i < 9; i++)
					pc.ccm_m[i] = (int16_t)lrintf(CCM_TBL[best].m[i] * 128.0f);
				pc.ccm_update = 1;
				if (!params_push(&pc, 0))
					printf("CCM f%05d -> %s (r=%.2f b=%.2f)\n",
					       frame, CCM_TBL[best].name, r, bb);
				ccm_cur = best;
				ccm_votes = 0;
			}
		}

		if (xioctl(vfd, VIDIOC_QBUF, &b) < 0) {
			perror("QBUF");
			break;
		}
	}

	xioctl(vfd, VIDIOC_STREAMOFF, &t);
	for (i = 0; i < (int)req.count; i++)
		munmap(map[i], maplen[i]);
	if (df)
		fclose(df);
	close(vfd);
	close(sfd);
	if (pc.fd > 0)
		close(pc.fd);
	return 0;
}
