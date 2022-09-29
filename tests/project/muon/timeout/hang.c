#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static void
infinite_loop(void) {
	while (1) {
		sleep(1);
	}
}

static void
handler(int signo, siginfo_t *info, void *context)
{
	printf("got sigterm :)\n");
	infinite_loop();
}

int main() {
	struct sigaction act = { 0 };
	act.sa_sigaction = &handler;
	if (sigaction(SIGTERM, &act, NULL) == -1) {
		perror("sigaction");
		exit(EXIT_FAILURE);
	}

	infinite_loop();
	return 0;
}
