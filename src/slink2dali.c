/***************************************************************************
 * slink2dali.c
 *
 * SeedLink to DataLink.
 *
 * Connect to a SeedLink server and forward data to a DataLink server.
 *
 * Written by Chad Trabant
 *   IRIS Data Management Center
 *
 * modified 2012.310
 ***************************************************************************/

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libdali.h>
#include <libmseed.h>
#include <libslink.h>
#include "./ringserver_response_codes.h"

#define PACKAGE "slink2dali"
#define VERSION "0.8"

static int createDLconnection (DLCP* dlconn, char* jwt);
static int sendrecord (char *record, int reclen);
static int parameter_proc (int argcount, char **argvec);
static char *getoptval (int argcount, char **argvec, int argopt);
static void term_handler (int sig);
static void print_timelogc (const char *msg);
static void print_timelog (char *msg);
static void usage (void);

static short int verbose = 0;   /* Flag to control general verbosity */
static int stateint      = 0;   /* Packet interval to save statefile */
static char *netcode     = 0;   /* Change all SEED newtork codes to netcode */
static char *statefile   = 0;   /* State file for saving/restoring stream states */
static char *jwt         = 0;   /* valid JWT */
static int writeack      = 1;   /* Flag to control the request for write acks, always request! */
static int dialup        = 0;   /* Flag to control SeedLink dialup mode */
static int keepalive     = 300; /* Interval to send keepalive/heartbeat (secs) */
static int netto         = 600; /* Network timeout (secs) */
static int netdly        = 30;  /* Network reconnect delay (secs) */

static SLCD *slconn;
static DLCP *dlconn;

int
main (int argc, char **argv)
{
  SLpacket *slpack;
  int seqnum;
  int ptype;
  int packetcnt = 0;

  /* The following is dependent on the packet type values in libslink.h */
  char *type[] = {"Data", "Detection", "Calibration", "Timing",
                  "Message", "General", "Request", "Info",
                  "Info (terminated)", "KeepAlive"};

  /* Signal handling, use POSIX calls with standardized semantics */
  struct sigaction sa;

  sa.sa_flags = SA_RESTART;
  sigemptyset (&sa.sa_mask);

  sa.sa_handler = term_handler;
  sigaction (SIGINT, &sa, NULL);
  sigaction (SIGQUIT, &sa, NULL);
  sigaction (SIGTERM, &sa, NULL);

  sa.sa_handler = SIG_IGN;
  sigaction (SIGHUP, &sa, NULL);
  sigaction (SIGPIPE, &sa, NULL);

  /* Process specified parameters */
  if (parameter_proc (argc, argv) < 0)
  {
    fprintf (stderr, "Argument processing failed\n");
    fprintf (stderr, "Try '-h' for detailed help\n");
    return -1;
  }

  /* Require authorization via jwt */
  if ( !jwt )
  {
    sl_log (2, 0, "This build requires the `-a token` option\n");
    return -1;
  }

  int connx_status = 0;
  connx_status =  createDLconnection(dlconn, jwt);
  if( connx_status == 0 )
  {
    ; // proceed
  }
  else if ( connx_status == -1 )
  {
    // recover connection, see if we can reconnect within 2 tries
    int MAX_TRY = 2;
    int connx_status = 0;
    for(int i=0; i<MAX_TRY; i++){
      // Re-connect to DataLink server and sleep before trying again
      sl_log (1, 0, "Re-connecting to DataLink server\n");

      if (dlconn->link != -1){
        dl_disconnect (dlconn);
      }

      connx_status = createDLconnection(dlconn, jwt);
      if( connx_status == 0 )
      {
        sl_log (1, 0, "Successfully reconnected to DataLink server\n");
        break; 
      }
      else if ( (connx_status == -1) && (i != MAX_TRY-1) ) // MAX_TRY-1 is the last iteration
      {
        sl_log (1, 0, "Failed to reconnect to DataLink server. Trying again...\n");
        sleep(10);
        continue; 
      }
      else if ( (connx_status == -1) && (i == MAX_TRY-1) )
      {
        sl_log (1, 0, "Last attempt to reconnect to DataLink server failed. Aborting...\n");
        return -1;
      }
      else if ( connx_status == -2)
      {
        sl_log (1, 0, "Unrecoverable failure to reconnect to DataLink server.\n");
        return -1;
      }
    }
  }
  else if ( connx_status == -2)
  {
    // abort connection, exit program
    sl_log (2, 0, "Unrecoverable error encountered in createDLconnection(). Aborting...\n");
    return -1;
  }
  else
  {
    sl_log (2, 0, "createDLconnection() return value unhandled.\n");
    return -1;
  }

  /* Loop with the connection manager */
  while (sl_collect (slconn, &slpack))
  {
    ptype  = sl_packettype (slpack);
    seqnum = sl_sequence (slpack);

    if (verbose > 1)
    {
      if (ptype == SLKEEP)
        sl_log (1, 0, "Keep alive packet received\n");
      else
        sl_log (1, 0, "Received %s packet, SeedLink sequence %d\n",
                type[ptype], seqnum);
    }

    /* Send record to the DataLink server if not internal types, INFO or keep alive */
    if (ptype >= SLDATA && ptype < SLNUM)
    {

#if 0
      while (sendrecord ((char *)&slpack->msrecord, SLRECSIZE))
      {
        if (verbose)
          sl_log (1, 0, "Re-connecting to DataLink server\n");

        /* Re-connect to DataLink server and sleep if error connecting */
        if (dlconn->link != -1)
          dl_disconnect (dlconn);

        if (dl_connect (dlconn) < 0)
        {
          sl_log (2, 0, "CONN_ERR | Error re-connecting to DataLink server, sleeping 10 seconds\n");
          sleep (10);
        }
        else if (jwt && dl_authorize (dlconn, jwt) < 0)
        {
          sl_log (2, 0, "AUTH_ERR | Error authorizing a write connection after re-connect, sleeping 10 seconds\n");
          sleep (10);
        }

        if (slconn->terminate)
          break;
      }
#endif

      int send_status = 0;
      send_status = sendrecord ((char *)&slpack->msrecord, SLRECSIZE);
      if( send_status == 0 )
      {
        packetcnt++;
      }
      else if( send_status == -1 )
      {
        ; //Packet was dropped.. we can retry here..
      }
      else if( send_status == -2 )
      {
        break; // Stop connection
      }
      else
      {
        sl_log (2, 0, "sendrecord() return value unhandled\n");
        break;
      }
    }

    /* Save intermediate state files */
    if (statefile && stateint)
    {
      if (++packetcnt >= stateint)
      {
        sl_savestate (slconn, statefile);
        packetcnt = 0;
      }
    }
  }

  /* Shut down the connection to SeedLink server */
  if (slconn->link != -1)
    sl_disconnect (slconn);

  /* Shut down the connection to DataLink server */
  if (dlconn->link != -1)
    dl_disconnect (dlconn);

  /* Save state file if specified */
  if (statefile)
    sl_savestate (slconn, statefile);

  return 0;
} /* End of main() */

/***************************************************************************
 * createDLconnection:
 *
 * Initiate and authorize a DL connection
 *
 * Returns 0 on success, -1 on recoverable failure, -2 on critical failure
 ***************************************************************************/
static int
createDLconnection (DLCP* dlconn, char* jwt)
{
  /* Connect to DataLink server */
  if (dl_connect (dlconn) < 0)
  {
    sl_log (2, 0, "Error in dl_connect()\n");
    return -1; // we can still retry
  }

  int auth_status = -2;
  if (dl_authorize(dlconn, jwt, &auth_status) < 0 )
  {
    sl_log (2, 0, "Error in dl_authorize\n", dlconn->addr);

    switch(auth_status){
      case AUTH_ERROR:
      case AUTH_INTERNAL_ERROR:
        return -1; 
      case AUTH_TOKEN_SIZE_ERROR:
      case AUTH_ROLE_INVALID_ERROR:
      case AUTH_EXPIRED_TOKEN_ERROR:
      case AUTH_FORMAT_ERROR:
      default:
        return -2; // disconnect
    }
  }

  return 0;

}

/***************************************************************************
 * sendrecord:
 *
 * Send the specified record to the DataLink server.
 *
 * Returns 0 on success, and -1 on packet dropped, -2 on failure
 ***************************************************************************/
static int
sendrecord (char *record, int reclen)
{
  static MSRecord *msr = NULL;
  hptime_t endtime;
  char streamid[100];
  int rv;

  /* Translate network code if supplied */
  if (netcode)
  {
    ms_strncpopen (((struct fsdh_s *)record)->network, netcode, 2);
  }

  /* Parse Mini-SEED header */
  if ((rv = msr_unpack (record, reclen, &msr, 0, 0)) != MS_NOERROR)
  {
    ms_recsrcname (record, streamid, 0);
    sl_log (2, 0, "Error unpacking %s: %s", streamid, ms_errorstr (rv));
    return -2; // Disconnect
  }

  /* Generate stream ID for this record: NET_STA_LOC_CHAN/MSEED */
  msr_srcname (msr, streamid, 0);
  strcat (streamid, "/MSEED");

  /* Determine high precision end time */
  endtime = msr_endtime (msr);

  /* Send record to server */
  int write_status = -2;
  if (dl_write (dlconn, record, reclen, streamid, msr->starttime, endtime, writeack, &write_status) >= 0)
  {
    // dl_write = 0 when success and no ack is requested
    // dl_write > 0 when success and ack is requested (returns pkt id)
    return 0;
  }
  else
  {

    switch(write_status){
      case WRITE_STREAM_UNAUTHORIZED_ERROR:
      case WRITE_DUPLICATE_PACKET_ERROR:
        sl_log (2, 0, "Warning on dl_write() \n");
        return -1;
      case WRITE_ERROR:
      case WRITE_INTERNAL_ERROR:
      case WRITE_UNAUTHORIZED_ERROR:
      case WRITE_NO_DEVICE_ERROR:
      case WRITE_EXPIRED_TOKEN_ERROR:
      case WRITE_FORMAT_ERROR:
      case WRITE_LARGE_PACKET_ERROR:
      default:
        sl_log (2, 0, "Error on dl_write() \n");
        return -2; // disconnect
    }
  }
} /* End of sendrecord() */

/***************************************************************************
 * parameter_proc:
 *
 * Process the command line parameters.
 *
 * Returns 0 on success, and -1 on failure.
 ***************************************************************************/
static int
parameter_proc (int argcount, char **argvec)
{
  char *sladdress = 0;
  char *dladdress = 0;
  int error       = 0;

  char *streamfile  = 0; /* stream list file for configuring streams */
  char *multiselect = 0;
  char *selectors   = 0;
  char *timewin     = 0;
  char *tptr;

  SLstrlist *timelist; /* split the time window arg */

  if (argcount <= 1)
    error++;

  /* Process all command line arguments */
  for (optind = 1; optind < argcount; optind++)
  {
    if (strcmp (argvec[optind], "-V") == 0)
    {
      fprintf (stderr, "%s version: %s\n", PACKAGE, VERSION);
      exit (0);
    }
    else if (strcmp (argvec[optind], "-h") == 0)
    {
      usage ();
      exit (0);
    }
    else if (strncmp (argvec[optind], "-v", 2) == 0)
    {
      verbose += strspn (&argvec[optind][1], "v");
    }
    else if (strcmp (argvec[optind], "-a") == 0)
    {
      jwt = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-l") == 0)
    {
      streamfile = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-s") == 0)
    {
      selectors = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-S") == 0)
    {
      multiselect = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-d") == 0)
    {
      dialup = 1;
    }
    else if (strcmp (argvec[optind], "-N") == 0)
    {
      netcode = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-x") == 0)
    {
      statefile = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-tw") == 0)
    {
      timewin = getoptval (argcount, argvec, optind++);
    }
    else if (strcmp (argvec[optind], "-nt") == 0)
    {
      netto = atoi (getoptval (argcount, argvec, optind++));
    }
    else if (strcmp (argvec[optind], "-nd") == 0)
    {
      netdly = atoi (getoptval (argcount, argvec, optind++));
    }
    else if (strcmp (argvec[optind], "-k") == 0)
    {
      keepalive = atoi (getoptval (argcount, argvec, optind++));
    }
    else if (strncmp (argvec[optind], "-", 1) == 0)
    {
      fprintf (stderr, "Unknown option: %s\n", argvec[optind]);
      exit (1);
    }
    else if (!sladdress)
    {
      sladdress = argvec[optind];
    }
    else if (!dladdress)
    {
      dladdress = argvec[optind];
    }
    else
    {
      fprintf (stderr, "Unknown option: %s\n", argvec[optind]);
      exit (1);
    }
  }

  /* Make sure a SeedLink server was specified */
  if (!sladdress)
  {
    fprintf (stderr, "No SeedLink server specified\n\n");
    fprintf (stderr, "%s version %s\n\n", PACKAGE, VERSION);
    fprintf (stderr, "Usage: %s [options] slhost dlhost\n\n", PACKAGE);
    fprintf (stderr, "Try '-h' for detailed help\n");
    exit (1);
  }

  /* Make sure a DataLink server was specified */
  if (!dladdress)
  {
    fprintf (stderr, "No DataLink server specified\n\n");
    fprintf (stderr, "%s version %s\n\n", PACKAGE, VERSION);
    fprintf (stderr, "Usage: %s [options] slhost dlhost\n\n", PACKAGE);
    fprintf (stderr, "Try '-h' for detailed help\n");
    exit (1);
  }

  /* Allocate and initialize SeedLink connection description */
  if (!(slconn = sl_newslcd ()))
  {
    fprintf (stderr, "Cannot allocation SeedLink descriptor\n");
    exit (1);
  }

  slconn->sladdr    = sladdress;
  slconn->netto     = netto;
  slconn->netdly    = netdly;
  slconn->keepalive = keepalive;

  if (dialup)
    slconn->dialup = 1;

  /* Allocate and initialize DataLink connection description */
  if (!(dlconn = dl_newdlcp (dladdress, argvec[0])))
  {
    fprintf (stderr, "Cannot allocation DataLink descriptor\n");
    exit (1);
  }

  /* Initialize the verbosity for the sl_log function */
  sl_loginit (verbose, &print_timelogc, NULL, &print_timelogc, NULL);

  /* Initialize the verbosity for the dl_log function */
  dl_loginit (verbose, &print_timelog, "", &print_timelog, "");

  /* Set stdout (where logs go) to always flush after a newline */
  setvbuf (stdout, NULL, _IOLBF, 0);

  /* Report the program version */
  sl_log (1, 0, "%s version: %s\n", PACKAGE, VERSION);

  /* If errors then report the usage message and quit */
  if (error)
  {
    usage ();
    exit (1);
  }

  /* Load the stream list from a file if specified */
  if (streamfile)
    sl_read_streamlist (slconn, streamfile, selectors);

  /* Split the time window argument */
  if (timewin)
  {

    if (strchr (timewin, ':') == NULL)
    {
      sl_log (2, 0, "time window not in begin:[end] format\n");
      return -1;
    }

    if (sl_strparse (timewin, ":", &timelist) > 2)
    {
      sl_log (2, 0, "time window not in begin:[end] format\n");
      sl_strparse (NULL, NULL, &timelist);
      return -1;
    }

    if (strlen (timelist->element) == 0)
    {
      sl_log (2, 0, "time window must specify a begin time\n");
      sl_strparse (NULL, NULL, &timelist);
      return -1;
    }

    slconn->begin_time = strdup (timelist->element);

    timelist = timelist->next;

    if (timelist != 0)
    {
      slconn->end_time = strdup (timelist->element);

      if (timelist->next != 0)
      {
        sl_log (2, 0, "malformed time window specification\n");
        sl_strparse (NULL, NULL, &timelist);
        return -1;
      }
    }

    /* Free the parsed list */
    sl_strparse (NULL, NULL, &timelist);
  }

  /* Parse the 'multiselect' string following '-S' */
  if (multiselect)
  {
    if (sl_parse_streamlist (slconn, multiselect, selectors) == -1)
      return -1;
  }
  else if (!streamfile)
  { /* No 'streams' array, assuming uni-station mode */
    sl_setuniparams (slconn, selectors, -1, 0);
  }

  /* Attempt to recover sequence numbers from state file */
  if (statefile)
  {
    /* Check if interval was specified for state saving */
    if ((tptr = strchr (statefile, ':')) != NULL)
    {
      char *tail;

      *tptr++ = '\0';

      stateint = (unsigned int)strtoul (tptr, &tail, 0);

      if (*tail || (stateint < 0 || stateint > 1e9))
      {
        sl_log (2, 0, "state saving interval specified incorrectly\n");
        return -1;
      }
    }

    if (sl_recoverstate (slconn, statefile) < 0)
    {
      sl_log (2, 0, "state recovery failed\n");
    }
  }

  return 0;
} /* End of parameter_proc() */

/***************************************************************************
 * getoptval:
 *
 * Return the value to a command line option; checking that the value is 
 * itself not an option (starting with '-') and is not past the end of
 * the argument list.
 *
 * argcount: total arguments in argvec
 * argvec: argument list
 * argopt: index of option to process, value is expected to be at argopt+1
 *
 * Returns value on success and exits with error message on failure
 ***************************************************************************/
static char *
getoptval (int argcount, char **argvec, int argopt)
{
  if (argvec == NULL || argvec[argopt] == NULL)
  {
    fprintf (stderr, "getoptval(): NULL option requested\n");
    exit (1);
  }

  if ((argopt + 1) < argcount && *argvec[argopt + 1] != '-')
    return argvec[argopt + 1];

  fprintf (stderr, "Option %s requires a value\n", argvec[argopt]);
  exit (1);
} /* End of getoptval() */

/***************************************************************************
 * term_handler:
 * Signal handler routine to control termination.
 ***************************************************************************/
static void
term_handler (int sig)
{
  sl_terminate (slconn);
}

/***************************************************************************
 * print_timelogc:
 *
 * Log message print handler used with sl_loginit() and sl_log().  Prefixes
 * a local time string to the message before printing.
 ***************************************************************************/
static void
print_timelogc (const char *msg)
{
  char timestr[100];
  time_t loc_time;

  /* Build local time string and cut off the newline */
  time (&loc_time);
  strcpy (timestr, asctime (localtime (&loc_time)));
  timestr[strlen (timestr) - 1] = '\0';

  fprintf (stdout, "%s - %s", timestr, msg);
}

/***************************************************************************
 * print_timelog:
 *
 * A wrapper for print_timelog() with a non-const msg.
 ***************************************************************************/
static void
print_timelog (char *msg)
{
  print_timelogc (msg);
}

/***************************************************************************
 * usage:
 * Print the usage message and exit.
 ***************************************************************************/
static void
usage (void)
{
  fprintf (stderr, "%s version %s\n\n", PACKAGE, VERSION);
  fprintf (stderr, "Usage: %s [options] slhost dlhost\n\n", PACKAGE);
  fprintf (stderr,
           " ## General options ##\n"
           " -V              Report program version\n"
           " -h              Print this usage message\n"
           " -v              Be more verbose, multiple flags can be used\n"
           " -a token        Configure DataLink connection with write authorization via JWT\n"
           " -d              Configure SeedLink connection in dial-up mode\n"
           " -nd delay       network re-connect delay (seconds), default 30\n"
           " -nt timeout     network timeout (seconds), re-establish connection if no\n"
           "                   data/keepalives are received in this time, default 600\n"
           " -k interval     send keepalive (heartbeat) packets this often (seconds)\n"
           " -N netcode      Change all SEED network codes to specified code\n"
           " -x sfile[:int]  Save/restore stream state information to this file\n"
           "\n"
           " ## SeedLink data stream selection ##\n"
           " -s selectors    Selectors for uni/all-station or default for multi-station mode\n"
           " -l listfile     Read a stream list from this file for multi-station mode\n"
           " -S streams      Define a stream list for multi-station mode\n"
           "   'streams' = 'stream1[:selectors1],stream2[:selectors2],...'\n"
           "        'stream' is in NET_STA format, for example:\n"
           "        -S \"IU_KONO:BHE BHN,GE_WLF,MN_AQU:HH?.D\"\n\n"
           " -tw begin:[end]  (requires SeedLink >= 3)\n"
           "        specify a time window in year,month,day,hour,min,sec format\n"
           "        example: -tw 2002,08,05,14,00,00:2002,08,05,14,15,00\n"
           "        the end time is optional, but the colon must be present\n"
           "\n"
           " slhost   Address of the SeedLink server in host:port format\n"
           "            Default host is 'localhost' and default port is '18000'\n\n"
           " dlhost   Address of the DataLink server in host:port format\n"
           "            Default host is 'localhost' and default port is '16000'\n\n");

} /* End of usage() */
