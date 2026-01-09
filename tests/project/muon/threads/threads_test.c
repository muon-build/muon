/*
 * SPDX-FileCopyrightText: VaiTon <eyadlorenzo@gmail.com>
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>

static DWORD WINAPI
thread_func(LPVOID arg)
{
	int *value = (int *)arg;
	*value = 42;
	return 0;
}

int
test_threads(void)
{
	HANDLE thread;
	int result = 0;
	DWORD thread_id;

	thread = CreateThread(NULL, 0, thread_func, &result, 0, &thread_id);
	if (thread == NULL) {
		fprintf(stderr, "Failed to create thread\n");
		return 1;
	}

	if (WaitForSingleObject(thread, INFINITE) != WAIT_OBJECT_0) {
		fprintf(stderr, "Failed to wait for thread\n");
		CloseHandle(thread);
		return 1;
	}

	CloseHandle(thread);

	if (result != 42) {
		fprintf(stderr, "Thread did not execute correctly\n");
		return 1;
	}

	return 0;
}

#elif defined(__unix__) || defined(__APPLE__)

#include <pthread.h>

static void *
thread_func(void *arg)
{
	int *value = (int *)arg;
	*value = 42;
	return 0;
}

int
test_threads(void)
{
	pthread_t thread;
	int result = 0;

	if (pthread_create(&thread, NULL, thread_func, &result) != 0) {
		fprintf(stderr, "Failed to create thread\n");
		return 1;
	}

	if (pthread_join(thread, NULL) != 0) {
		fprintf(stderr, "Failed to join thread\n");
		return 1;
	}

	if (result != 42) {
		fprintf(stderr, "Thread did not execute correctly\n");
		return 1;
	}

	return 0;
}
#else
#error "Thread test not implemented for this platform"
#endif

int
main(void)
{
	return test_threads();
}
