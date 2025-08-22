#include <pthread.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#ifdef X11
#include <X11/Xlib.h>
#endif

struct arg {
  void *function;
  void* args;
};

void *volume(void *args); // args: format string (%d)
void *datetime(void* args); // args: format string


#define MAXLEN 128 // max len per module
#define MAX_STATUS_LEN 1024
#define DATE_INTERVAL_SECS 60
#define WAIT_FOR_STATE // if defined the program will wait until all process update at least one time

static const struct arg args[] = {
  {volume, "Vol: %d%% |"},
  {datetime, " %a %d %b %H:%M"}
};


#define LEN(array) (sizeof(array) / sizeof(array[0]))
#define FORMAT_ARGS(args) (*(struct ThreadArg *)args)

static void update_status_text();

struct ThreadArg {
  int index;
  void* args;
};

typedef struct {
    void (*init)(void);
    void (*deinit)(void);
    void (*write_status)(const char *status);
} StatusBackend;

#ifdef X11
Display *display = NULL;

void x11_init(void) {
  display = XOpenDisplay(NULL);
  if (!display) {
    fprintf(stderr, "XOpenDisplay failed\n");
    exit(1);
  }
}

void x11_deinit(void) {
  if (display) {
    XCloseDisplay(display);
    display = NULL;
  }
}

void x11_write(const char *status) {
  if (!display) return;
  Window root = DefaultRootWindow(display);
  XStoreName(display, root, status);
  XFlush(display);
}

static StatusBackend backend = { x11_init, x11_deinit, x11_write };
#endif

#ifndef X11
void wayland_init(void) {}
void wayland_deinit(void) {}
void wayland_write(const char *status) {
  write(1, status, strlen(status));
  write(1, "\n", 1);
}

static StatusBackend backend = { wayland_init, wayland_deinit, wayland_write };
#endif

char statusText[MAXLEN] = "";
static pthread_mutex_t statusMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t statusCond = PTHREAD_COND_INITIALIZER;
static int updated = 0;

static char argsTexts[LEN(args)][MAXLEN] = {{""}};


static void sink_cb(pa_context *c, const pa_sink_info *i, int eol, void *args) {
  struct ThreadArg targs = FORMAT_ARGS(args);
  if (eol > 0)
    return;
  if (i) {
    pa_volume_t vol = pa_cvolume_avg(&i->volume);
    int percent = (int)((vol * 100) / PA_VOLUME_NORM) + 1;

    sprintf(argsTexts[targs.index], targs.args, percent);
    update_status_text();
  }
}

static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *args) {
  pa_operation *o = pa_context_get_sink_info_by_index(c, idx, sink_cb, args);
  if (o)
    pa_operation_unref(o);
}

static void context_state_cb(pa_context *c, void *args) {
  if (pa_context_get_state(c) == PA_CONTEXT_READY) {
    pa_operation *o = pa_context_get_sink_info_list(c, sink_cb, args);
    if (o)
      pa_operation_unref(o);
    pa_context_set_subscribe_callback(c, subscribe_cb, args);
    o = pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
    if (o)
      pa_operation_unref(o);
  }
}

static void update_status_text() {
    char buffer[MAX_STATUS_LEN];
    buffer[0] = '\0';

    pthread_mutex_lock(&statusMutex);

    for (int i = 0; i < LEN(args); i++) {
#ifdef WAIT_FOR_STATE
        if (strcmp(argsTexts[i], "") == 0) {
            updated = 1;
            pthread_cond_signal(&statusCond);
            pthread_mutex_unlock(&statusMutex);
            return;
        }
#endif
        strncat(buffer, argsTexts[i], sizeof(buffer) - strlen(buffer) - 1);
    }

    backend.write_status(buffer);

    updated = 1;
    pthread_cond_signal(&statusCond);
    pthread_mutex_unlock(&statusMutex);
}


void *volume(void *args) {
  pa_mainloop *ml = pa_mainloop_new();
  pa_context *ctx = pa_context_new(pa_mainloop_get_api(ml), "VolumeWatcher");

  pa_context_set_state_callback(ctx, context_state_cb, args);
  pa_context_connect(ctx, NULL, 0, NULL);

  pa_mainloop_run(ml, NULL);

  pa_context_disconnect(ctx);
  pa_context_unref(ctx);
  pa_mainloop_free(ml);

  free(args);

  return 0;
}

void *datetime(void *args) {
  struct ThreadArg targs = FORMAT_ARGS(args);
  static time_t rawtime;

  while (1) {
    time( &rawtime );
    struct tm *info = localtime(&rawtime);

    strftime(argsTexts[targs.index], MAXLEN, targs.args, info);

    update_status_text();
    sleep(DATE_INTERVAL_SECS);
  }

  free(args);
  return 0;
}

int main() {
  backend.init();

  for (int i = 0; i < LEN(args); i++) {
    pthread_t argThread;
    struct ThreadArg *targs = malloc(sizeof(struct ThreadArg));
    *targs = (struct ThreadArg){.index = i, .args = args[i].args};
    pthread_create(&argThread, NULL, args[i].function, targs);
  }

  for (;;) {
    pthread_mutex_lock(&statusMutex);
    while (!updated) {
      pthread_cond_wait(&statusCond, &statusMutex);
    }
    updated = 0;

    char local[64];
    strncpy(local, statusText, sizeof(local));
    local[sizeof(local)-1] = '\0';

    pthread_mutex_unlock(&statusMutex);

    printf("%s", local);
  }

  backend.deinit();

  return 0;
}
