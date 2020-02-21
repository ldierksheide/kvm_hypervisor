#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

void timer_handler (int sig, siginfo_t *info, void *ucontext) 
{
	printf("timer! signum = %d\n", sig);

}
int main() 
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));

	struct itimerval timer;
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = &timer_handler;
	sigaction(SIGALRM, &sa, NULL);

	timer.it_value.tv_sec = 1;
	timer.it_value.tv_usec = 0;
	timer.it_interval.tv_sec = 1;
	timer.it_interval.tv_usec = 0;

	setitimer(ITIMER_REAL, &timer, NULL);
	while(1);
}
