// Copyright (c) 2017, David H. Hovemeyer <david.hovemeyer@gmail.com>
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Really simple graphical CPU core monitor (visualize load on each CPU core)
// Linux-only, and requires libui (https://github.com/andlabs/libui)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <ui.h>

//////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////

#define INITIAL_BUF_SIZE 64

#define POLLS_PER_SEC 4
#define POLL_NSEC (1000000000 / POLLS_PER_SEC)

#define BAR_WIDTH 20
#define BAR_HEIGHT 80

#define BLACK 0x000000
#define DODGER_BLUE 0x1E90FF
#define MIDNIGHT_BLUE 0x003366

//////////////////////////////////////////////////////////////////////
// Data types
//////////////////////////////////////////////////////////////////////

typedef struct {
	uint64_t cpu, sys;
} State;

typedef struct {
	State last, now;
} Core;

//////////////////////////////////////////////////////////////////////
// Global variables
//////////////////////////////////////////////////////////////////////

static int s_num_cores;
static Core *s_cores;

static volatile bool s_anim_quit;
static pthread_t s_anim_thr;
static uiWindow *s_mainwin;
static uiArea *s_drawing_area;
static uiAreaHandler s_drawing_area_handler;

//////////////////////////////////////////////////////////////////////
// Functions
//////////////////////////////////////////////////////////////////////

// Read a single line of text from specified file handle
static char *readline(FILE *in) {
	// Grow the buffer (capacity doubles)
#define GROW() do { capacity *= 2; buf = realloc(buf, capacity); } while (0)

	char *buf = malloc(INITIAL_BUF_SIZE);
	size_t capacity = INITIAL_BUF_SIZE; // current size of buffer
	size_t pos = 0; // how many bytes of buffer have been used
	bool eof = false;

	for (;;) {
		int c = getc(in);
		if (c == EOF) {
			eof = true;
			break;
		} else if (c == '\n') {
			break;
		} else {
			if (pos >= capacity) { GROW(); }
			buf[pos] = (char) c;
			pos++;
		}
	}

	// Special case: return NULL if we reached EOF without
	// reading any data.
	if (eof && pos == 0) {
		free(buf);
		return NULL;
	}

	// terminate buffer
	if (pos >= capacity) { GROW(); }
	buf[pos] = '\0';

	return buf;

#undef GROW
}

static bool startswith(const char *s, const char *pfx) {
	return strstr(s, pfx) == s;
}

// Count how many cores there are
static int count_cores(void) {
	FILE *cpuinfo = fopen("/proc/cpuinfo", "r");
	int count = 0;
	for (;;) {
		char *s = readline(cpuinfo);
		if (!s) {
			break;
		}
		if (startswith(s, "processor\t")) { count++; }
		free(s);
	}
	fclose(cpuinfo);
	return count;
}

static void poll_cores(void) {
	FILE *stat = fopen("/proc/stat", "r");
	for (;;) {
		char *s = readline(stat);
		if (!s) {
			break;
		}
		if (startswith(s, "cpu") && !startswith(s, "cpu ")) {
			// Which core is this?
			int corenum = atoi(s + 3);
			if (corenum >= 0 && corenum < s_num_cores) {
				Core *c = &s_cores[corenum];

				// Parse times
				const char *rest = strstr(s, " ");
				uint64_t user, nice, system, idle, iowait, irq, softirq;
				sscanf(rest, "%" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64,
					&user, &nice, &system, &idle, &iowait, &irq, &softirq);

				// Update states
				c->last = c->now;
				c->now.cpu = user + nice;
				c->now.sys = system + iowait + irq + softirq;
			}
		}

		free(s);
	}
	fclose(stat);
}

// This is from the libui histogram example program.
static void setSolidBrush(uiDrawBrush *brush, uint32_t color, double alpha) {
	uint8_t component;

	brush->Type = uiDrawBrushTypeSolid;
	component = (uint8_t) ((color >> 16) & 0xFF);
	brush->R = ((double) component) / 255;
	component = (uint8_t) ((color >> 8) & 0xFF);
	brush->G = ((double) component) / 255;
	component = (uint8_t) (color & 0xFF);
	brush->B = ((double) component) / 255;
	brush->A = alpha;
}

static void handlerMouseEvent(uiAreaHandler *a, uiArea *area, uiAreaMouseEvent *e) {
}

static void handlerMouseCrossed(uiAreaHandler *ah, uiArea *a, int left) {
}

static void handlerDragBroken(uiAreaHandler *ah, uiArea *a) {
}

static int handlerKeyEvent(uiAreaHandler *ah, uiArea *a, uiAreaKeyEvent *e) {
	return 0;
}

static void handlerDraw(uiAreaHandler *a, uiArea *area, uiAreaDrawParams *p) {
	//printf("Drawing! %dx%d\n", (int)p->AreaWidth, (int)p->AreaHeight);

	uiDrawPath *path;
	uiDrawBrush brush;

	// Draw black background
	setSolidBrush(&brush, BLACK, 1.0);
	path = uiDrawNewPath(uiDrawFillModeWinding);
	uiDrawPathAddRectangle(path, 0, 0, p->AreaWidth, p->AreaHeight);
	uiDrawPathEnd(path);
	uiDrawFill(p->Context, path, &brush);
	uiDrawFreePath(path);

	// Draw a bar for each core
	for (int i = 0; i < s_num_cores; i++) {
		const Core *c = &s_cores[i];
		if (c->last.cpu > 0) {
			uint64_t cpudiff = c->now.cpu - c->last.cpu;
			uint64_t sysdiff = c->now.sys - c->last.sys;

			// show CPU utilization
			int x = i*BAR_WIDTH + 1;
			int y = BAR_HEIGHT - (int)(((cpudiff*POLLS_PER_SEC) / 100.0) * BAR_HEIGHT);
			setSolidBrush(&brush, DODGER_BLUE, 1.0);
			path = uiDrawNewPath(uiDrawFillModeWinding);
			uiDrawPathAddRectangle(path, x, y, BAR_WIDTH-2, BAR_HEIGHT - y);
			uiDrawPathEnd(path);
			uiDrawFill(p->Context, path, &brush);
			uiDrawFreePath(path);

			// show system time
			int sh = (int)(((sysdiff*POLLS_PER_SEC) / 100.0) * BAR_HEIGHT);
			setSolidBrush(&brush, MIDNIGHT_BLUE, 1.0);
			path = uiDrawNewPath(uiDrawFillModeWinding);
			uiDrawPathAddRectangle(path, x, y-sh, BAR_WIDTH-2, sh);
			uiDrawPathEnd(path);
			uiDrawFill(p->Context, path, &brush);
			uiDrawFreePath(path);
		}
	}
}

// Callback function responding to animation timer ticks.
static void on_tick(void *data) {
	// If we're quitting, don't update the core states or
	// attempt to redraw.
	if (s_anim_quit) {
		return;
	}

	//printf("Tick!\n");
	// Update core states
	poll_cores();

	// Force redraw
	uiAreaQueueRedrawAll(s_drawing_area);
}

// This thread generates timer tick events, posting them as calls
// to onTick() on the main UI thread.  It can be stopped by setting
// s_anim_quit to true.
static void *anim_timer_thread(void *arg) {
	while (!s_anim_quit) {
		struct timespec ts;
		ts.tv_sec = 0;
		ts.tv_nsec = 1000000000 / POLLS_PER_SEC;
		nanosleep(&ts, NULL);
		uiQueueMain(on_tick, NULL);
	}
	return NULL;
}

// Callback to start animation timer thread.
// This needs to be done from the UI thread, because the UI thread
// will be waiting for the animation thread to exit (using pthread_join).
static void start_animation_timer(void *data) {
	// Start the animation timer thread
	pthread_create(&s_anim_thr, NULL, anim_timer_thread, NULL);
}

static int on_closing(uiWindow *w, void *data) {
	// Tell animation timer thread to quit
	s_anim_quit = true;

	// Wait for the animation timer thread to exit
	pthread_join(s_anim_thr, NULL);

	uiControlDestroy(uiControl(s_mainwin));
	uiQuit();
	return 0;
}

int main(void) {
	// Determine number of cores, allocate data structure
	s_num_cores = count_cores();

	s_cores = malloc(s_num_cores * sizeof(Core));
	memset(s_cores, 0, s_num_cores * sizeof(Core));

	// Initialize libui
	uiInitOptions options;
	const char *err;

	// Initialize libui
	memset(&options, 0, sizeof (uiInitOptions));
	err = uiInit(&options);
	if (err != NULL) {
		fprintf(stderr, "error initializing libui: %s", err);
		uiFreeInitError(err);
		return 1;
	}

	// Start the animation thread
	uiQueueMain(start_animation_timer, NULL);

	s_mainwin = uiNewWindow("coremon", s_num_cores*BAR_WIDTH, BAR_HEIGHT, 1);
	uiWindowOnClosing(s_mainwin, on_closing, NULL);

	uiBox *hbox = uiNewHorizontalBox();
	uiBoxSetPadded(hbox, 1);
	uiWindowSetChild(s_mainwin, uiControl(hbox));

	s_drawing_area_handler.Draw = handlerDraw;
	s_drawing_area_handler.MouseEvent = handlerMouseEvent;
	s_drawing_area_handler.MouseCrossed = handlerMouseCrossed;
	s_drawing_area_handler.DragBroken = handlerDragBroken;
	s_drawing_area_handler.KeyEvent = handlerKeyEvent;

	s_drawing_area = uiNewArea(&s_drawing_area_handler);
	uiBoxAppend(hbox, uiControl(s_drawing_area), 1);

	uiControlShow(uiControl(s_mainwin));
	uiMain();

	return 0;
}
