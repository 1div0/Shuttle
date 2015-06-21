#define Name "Shuttle"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <lo/lo.h>
#ifdef __linux__
#include <linux/input.h>
#endif

#include "mediactrl.h"

#define SIGNALS 32

static int end ;

struct signal_handler
{
  void (*fn)(void *);
  void *data;
  int critical;
} ;

static int signal_mask[SIGNALS] ;
static struct signal_handler signal_handlers[SIGNALS] ;
static int signal_pipe[3] ;
static struct sigaction sa_zero ;

static inline int can_write(int fd)
{
  fd_set fds;
  struct timeval tv = {0, 0};

  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  return select(fd + 1, NULL, &fds, NULL, &tv);
}

void got_signal(int sig)
{
  int sv_errno = errno ;

  // fprintf (stderr, "signal number: %d\n", sig) ;

  if (sig >= SIGNALS || sig < 0)
  {
    fprintf (stderr, "ERROR: bad signal number: %d\n", sig) ;
    goto ret ;
  }
  if (!signal_handlers[sig].fn) goto ret ;
  if (signal_handlers[sig].critical)
  {
    signal_handlers[sig].fn(signal_handlers[sig].data) ;
    goto ret;
  }
  signal_mask[sig] = 1 ;
ret:
  if (can_write (signal_pipe[1]))
    write (signal_pipe[1], "\n", 1) ;
  errno = sv_errno ;
}

void install_signal_handler (int sig, void (*fn)(void *), void *data, int critical)
{
  struct sigaction sa = sa_zero ;

  if (sig >= SIGNALS || sig < 0)
  {
    fprintf (stderr, "bad signal number: %d\n", sig) ;
    return ;
  }
  if (!fn) sa.sa_handler = SIG_IGN ;
  else sa.sa_handler = got_signal ;
  sigfillset (&sa.sa_mask) ;
  sa.sa_flags = SA_RESTART ;
  if (!fn) sigaction (sig, &sa, NULL) ;
  signal_handlers[sig].fn = fn ;
  signal_handlers[sig].data = data ;
  signal_handlers[sig].critical = critical ;
  if (fn) sigaction (sig, &sa, NULL) ;
}

void sig_ctrl_c (int *end)
{
  *end = 1 ;
}

void complain(char *s, ...)
{
  va_list ap ;
  va_start (ap, s) ;
  vfprintf (stderr, s, ap) ;
  va_end (ap) ;
}

void fatal_error(char *s)
{
  fprintf (stderr, "%s\n", s) ;
  exit (4) ;
}

static void usage ()
{
  fprintf (stderr, "Usage:\n\n") ;
  fprintf (stderr, "%s [host] [port]\n", Name) ;
}

int 
main (int argc, char **argv)
{
  int error = 0, c = argc, i = 1, verbose = 0 ;
  char *arg ;
  char *host = "localhost" ;
  char *port = "3819" ;
  lo_address target ;
  struct media_ctrl mc ;
  struct media_ctrl_event me ;

  arg = argv[i] ;

  if (arg != NULL && arg[0] == '-')
  {
    switch (argv[i][1])
    {
      case 'h' :
        usage () ;
        return -1 ;

      case 'v' :
        verbose++ ;
        break ;

      default :
        usage () ;
    }
    i++ ;
    c-- ;
  }

  if (c > 1)
  {
    host = argv[i] ;
  }

  if (c > 2)
  {
    port = argv[i + 1] ;
  }

  target = lo_address_new (host, port) ;
  if (target == NULL)
  {
    fprintf (stderr, "Failed to open %s:%s\n", host, port) ;
    return 1 ;
  }

  install_signal_handler (SIGINT, (void (*)(void *)) sig_ctrl_c, &end, 1) ;

  media_ctrl_open (&mc) ;
  while (!end)
  {
    media_ctrl_read_event (&mc, &me) ;

    switch (me.type)
    {
      case MEDIA_CTRL_EVENT_KEY :
      {
        char *command = NULL ;
    	printf ("Key %04x %02x\n", me.code, me.value) ;

        if (me.value == 1)
        {
          switch (me.code)
          {
            case MEDIA_CTRL_F1 : command = "/ardour/rewind" ; break ;
            case MEDIA_CTRL_F2 : command = "/ardour/transport_play" ; break ;
            case MEDIA_CTRL_F3 : command = "/ardour/transport_stop" ; break ;
            case MEDIA_CTRL_F4 : command = "/ardour/ffwd" ; break ;
            case MEDIA_CTRL_B4 : command = "/ardour/goto_start" ; break ;
            case MEDIA_CTRL_B2 : command = "/ardour/prev_marker" ; break ;
            case MEDIA_CTRL_B1 : command = "/ardour/add_marker" ; break ;
            case MEDIA_CTRL_B3 : command = "/ardour/next_marker" ; break ;
            case MEDIA_CTRL_B5 : command = "/ardour/goto_end" ; break ;
          }
          if (command != NULL)
          {
        	lo_send (target, command, "") ;
          }
        }
        break ;
      }

      case MEDIA_CTRL_EVENT_SHUTTLE :
      {
        static float table[8] = { 0.0, 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 } ;
        float f, speed ;

        printf ("Shuttle %d\n", me.value) ;

        speed = table[me.value > 0 ? +me.value : -me.value ] ;
        f = me.value > 0 ? +speed : -speed ;

        lo_send (target, "/ardour/set_transport_speed", "f", f) ;
        break ;
      }

      case MEDIA_CTRL_EVENT_JOG :
      {
        char *s ;

        printf ("Jog %d\n", me.value) ;

        s = me.value > 0 ? "Editor/playhead-forward-to-grid" : "Editor/playhead-backward-to-grid" ;
        lo_send (target, "/ardour/access_action", "s", s) ;
        break ;
      }
    }
  }

  media_ctrl_close (&mc) ;

  lo_address_free (target) ;

  error = 0 ;

  install_signal_handler (SIGINT, NULL, NULL, 0) ;

  return error ;
}
