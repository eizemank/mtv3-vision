/*
 * mtv3_httpd.c — микросервер отладочного видео для MTV3.
 * Отдаёт файл (по умолчанию /tmp/last.jpg, его пишет mainCV --dump) как
 * MJPEG-поток multipart/x-mixed-replace: в браузере живое видео без рефреша.
 *
 * Сборка:  make httpd        Запуск:  ./mtv3_httpd [порт=8080] [файл]
 * Просмотр: http://<ip-борта>:8080/
 *
 * Однопоточный, один клиент за раз (отладочный инструмент, не продукт).
 */

#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define PERIOD_US 200000        /* 5 кадров/с — по темпу mainCV --dump 5..10 */

static int send_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	while (len) {
		ssize_t n = write(fd, p, len);
		if (n <= 0)
			return -1;
		p += n;
		len -= n;
	}
	return 0;
}

static int stream(int cfd, const char *path)
{
	static const char hdr[] =
		"HTTP/1.0 200 OK\r\n"
		"Cache-Control: no-cache\r\n"
		"Connection: close\r\n"
		"Content-Type: multipart/x-mixed-replace; boundary=mtv3\r\n\r\n";
	char part[128], buf[512 * 1024];
	char req[1024];

	read(cfd, req, sizeof(req));            /* запрос не разбираем */
	if (send_all(cfd, hdr, sizeof(hdr) - 1))
		return -1;

	for (;;) {
		int f = open(path, O_RDONLY);
		if (f >= 0) {
			ssize_t n = read(f, buf, sizeof(buf));
			close(f);
			if (n > 0) {
				int k = snprintf(part, sizeof(part),
					"--mtv3\r\nContent-Type: image/jpeg\r\n"
					"Content-Length: %zd\r\n\r\n", n);
				if (send_all(cfd, part, k) ||
				    send_all(cfd, buf, n) ||
				    send_all(cfd, "\r\n", 2))
					return -1;      /* клиент отвалился */
			}
		}
		usleep(PERIOD_US);
	}
}

int main(int argc, char **argv)
{
	int port = argc > 1 ? atoi(argv[1]) : 8080;
	const char *path = argc > 2 ? argv[2] : "/tmp/last.jpg";

	signal(SIGPIPE, SIG_IGN);

	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	int one = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in a = { .sin_family = AF_INET,
				 .sin_port = htons(port),
				 .sin_addr.s_addr = INADDR_ANY };
	if (bind(sfd, (struct sockaddr *)&a, sizeof(a)) || listen(sfd, 1)) {
		perror("bind/listen");
		return 1;
	}
	printf("mtv3_httpd: port %d, file %s\n", port, path);

	for (;;) {
		int cfd = accept(sfd, NULL, NULL);
		if (cfd < 0)
			continue;
		stream(cfd, path);              /* до отвала клиента */
		close(cfd);
	}
}
