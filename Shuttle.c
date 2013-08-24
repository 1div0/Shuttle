#define Name "Shuttle"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <signal.h>
#include <linux/input.h>

#include "mediactrl.h"
#include "htmsocket.h"
#include "OSC-client.h"

#define SIGNALS 32

typedef struct
{
  enum { INT, FLOAT, STRING } type ;
  union
  {
    int i ;
    float f ;
    char *s ;
  } datum ;
} typedArg ;

static int useTypeTags = 1 ;
static int end ;

#define MAX_ARGS 2000
#define SC_BUFFER_SIZE 32000
static char bufferForOSCbuf[SC_BUFFER_SIZE] ;

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

int WriteMessage(OSCbuf *buf, char *messageName, int numArgs, typedArg *args) {
    int j, returnVal;

    returnVal = 0;

#ifdef DEBUG
    printf("WriteMessage: %s ", messageName);

     for (j = 0; j < numArgs; j++) {
        switch (args[j].type) {
            case INT:
            printf("%d ", args[j].datum.i);
            break;

            case FLOAT:
            printf("%f ", args[j].datum.f);
            break;

            case STRING:
            printf("%s ", args[j].datum.s);
            break;

            default:
            fatal_error("Unrecognized arg type");
            exit(5);
        }
    }
    printf("\n");
#endif

    if (!useTypeTags) {
        returnVal = OSC_writeAddress(buf, messageName);
        if (returnVal) {
            complain("Problem writing address: %s\n", OSC_errorMessage);
        }
    } else {
        /* First figure out the type tags */
        char typeTags[MAX_ARGS+2];
        int i;

        typeTags[0] = ',';

        for (i = 0; i < numArgs; ++i) {
            switch (args[i].type) {
                case INT:
                typeTags[i+1] = 'i';
                break;

                case FLOAT:
                typeTags[i+1] = 'f';
                break;

                case STRING:
                typeTags[i+1] = 's';
                break;

                default:
                fatal_error("Unrecognized arg type");
                exit(5);
            }
        }
        typeTags[i+1] = '\0';
            
        returnVal = OSC_writeAddressAndTypes(buf, messageName, typeTags);
        if (returnVal) {
            complain("Problem writing address: %s\n", OSC_errorMessage);
        }
    }

     for (j = 0; j < numArgs; j++) {
        switch (args[j].type) {
            case INT:
            if ((returnVal = OSC_writeIntArg(buf, args[j].datum.i)) != 0) {
                return returnVal;
            }
            break;

            case FLOAT:
            if ((returnVal = OSC_writeFloatArg(buf, args[j].datum.f)) != 0) {
                return returnVal;
            }
            break;

            case STRING:
            if ((returnVal = OSC_writeStringArg(buf, args[j].datum.s)) != 0) {
                return returnVal;
            }
            break;

            default:
            fatal_error("Unrecognized arg type");
            exit(5);
        }
    }

    return returnVal;
}

void SendData(void *htmsocket, int size, char *data) {
    if (!SendHTMSocket(htmsocket, size, data)) {
        perror("Couldn't send out socket: ");
        CloseHTMSocket(htmsocket);
        exit(3);
    }
}

void SendBuffer(void *htmsocket, OSCbuf *buf) {
#ifdef DEBUG
    printf("Sending buffer...\n");
#endif
    if (OSC_isBufferEmpty(buf)) return;
    if (!OSC_isBufferDone(buf)) {
        fatal_error("SendBuffer() called but buffer not ready!");
        exit(5);
    }
    SendData(htmsocket, OSC_packetSize(buf), OSC_getPacket(buf));
}

static void usage ()
{
  fprintf (stderr, "Usage:\n") ;
  fprintf (stderr, "%s host [port]\n", Name) ;
}

int 
main (int argc, char **argv)
{
  int error = 0, i = 1 ;
  int port = 3819 ;
  char *host ;
  void *htmsocket ;
  struct media_ctrl mc ;
  struct media_ctrl_event me ;
  OSCbuf buf[1] ;
  bool valid = FALSE ;
  typedArg arg ;

  if (argc <= 1)
  {
    usage () ;
    return -1 ;
  }

  host = argv[i] ;
  if (argc > 2)
  {
    port = atoi (argv[i + 1]) ;
  }

  htmsocket = OpenHTMSocket (host, port) ;
  if (htmsocket == NULL)
  {
    perror ("Couldn't open socket: ") ;
    return 1 ;
  }

  install_signal_handler (SIGINT, (void (*)(void *)) sig_ctrl_c, &end, 1) ;

  media_ctrl_open (&mc) ;
  while (!end)
  {
    media_ctrl_read_event (&mc, &me) ;

    OSC_initBuffer (buf, SC_BUFFER_SIZE, bufferForOSCbuf) ;

    switch (me.type)
    {
      case MEDIA_CTRL_EVENT_KEY :
      {
        printf ("Key %04x %02x\n", me.code, me.value) ;

        if (me.value == 1)
        {
          switch (me.code)
          {
            case MEDIA_CTRL_F1 : WriteMessage (buf, "/ardour/rewind", 0, NULL) ; break ;
            case MEDIA_CTRL_F2 : WriteMessage (buf, "/ardour/transport_play", 0, NULL) ; break ;
            case MEDIA_CTRL_F3 : WriteMessage (buf, "/ardour/transport_stop", 0, NULL) ; break ;
            case MEDIA_CTRL_F4 : WriteMessage (buf, "/ardour/ffwd", 0, NULL) ; break ;
            case MEDIA_CTRL_B4 : WriteMessage (buf, "/ardour/goto_start", 0, NULL) ; break ;
            case MEDIA_CTRL_B2 : WriteMessage (buf, "/ardour/prev_marker", 0, NULL) ; break ;
            case MEDIA_CTRL_B1 : WriteMessage (buf, "/ardour/add_marker", 0, NULL) ; break ;
            case MEDIA_CTRL_B3 : WriteMessage (buf, "/ardour/next_marker", 0, NULL) ; break ;
            case MEDIA_CTRL_B5 : WriteMessage (buf, "/ardour/goto_end", 0, NULL) ; break ;
          }
          SendBuffer (htmsocket, buf) ;
        }
        break ;
      }

      case MEDIA_CTRL_EVENT_SHUTTLE :
      {
        static float table[8] = { 0.0, 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0 } ;
        float speed ;

        printf ("Shuttle %d\n", me.value) ;

        speed = table[me.value > 0 ? +me.value : -me.value ] ;

        arg.type = FLOAT ;
        arg.datum.f = me.value > 0 ? +speed : -speed ;
        WriteMessage (buf, "/ardour/set_transport_speed", 1, &arg) ;
        SendBuffer (htmsocket, buf) ;
        break ;
      }

      case MEDIA_CTRL_EVENT_JOG :
      {
        printf ("Jog %d\n", me.value) ;

        arg.type = STRING ;
        arg.datum.s = me.value > 0 ? "Editor/playhead-forward-to-grid" : "Editor/playhead-backward-to-grid" ;
        WriteMessage (buf, "/ardour/access_action", 1, &arg) ;
        SendBuffer (htmsocket, buf) ;
        break ;
      }
    }
  }
  media_ctrl_close (&mc) ;

  CloseHTMSocket (htmsocket) ;

  error = 0 ;

  install_signal_handler (SIGINT, NULL, NULL, 0) ;

  return error ;
}
