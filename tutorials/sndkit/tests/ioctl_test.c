/*
  Purpose: This program has been used to verify that some of the ioctl calls work
 * Copyright (C) 4Front Technologies, 2009. Released under GPLv2/CDDL.
 *
 * Description:
 * This program can be used to debug some of the most common OSS audio ioctl calls (use the -m option to select):
 *
 * 	-m0 : SNDCTL_DSP_GETOSPTR      - The output buffer pointer
 * 	-m1 : SNDCTL_DSP_GETODELAY     - The output delay
 * 	-m2 : SNDCTL_DSP_GETOSPACE     - Space in the output buffer
 *
 * CAUTION!
 * 	
 * 	This program is not an automated test that tells if the device/driver is working correctly or not.
 * 	It simply displays the value returned by the ioctl call and gives some visual indication of the 
 * 	result. You will see a '*' walking left or right on some kind of bar display.
 *
 * 	Tho use this program you should have very detailed knowledge about the internals of OSS. Otherwise
 * 	the results will not make any sense to you.
 *
 * NOTE!
 *
 * 	Output of this program is supposed to be redirected to a disk file that is to be examined after the
 * 	test is finished. Output to a terminal will delay the program too much which gives wrong results.
 *
 * How it works:
 *
 * There are different tests in this program. All they write audio data to the audio device (no sound will be produced).
 * After each write call the program will display the information specific to that test. The times displayed are derived
 * from the number of bytes written to the device so far. It is important to understand that they are not real time. As
 * long as the device buffer is not completely filled the time difference between two subsequent writes will be zero
 * (unless the -D option is used).
 *
 * Device setup:
 *
 * Use the following command line options to select the setup parameters for the device:
 *
 * -s NNN    Selects the sampling rate (in Hz or kHz)
 * -c NNN    Selects the number of channels (1, 2, ..., N)
 * -b NNN    Selects the number of bits (8, 16 or 32)
 *
 * -f NNN    Selects the fragment size (fs = 2**NNN)
 * -n NNN    Selects the number of fragments (2 to N)
 * -d NNN    Selects the output device to use (/dev/dsp by default)
 *
 * -r        Disables automatic format conversions performed by OSS
 * -B        Open the physical device directly (bypassing virtual mixer (vmix))
 *
 * -w NNN    Selects the number of bytes written during each loop. By default this is
 *           equal to the fragment size.
 * -D NNN    Delay NNN milliseconds after each write (before displaying the test output). Using too long
 *           delay times will cause buffer underruns. Use delays that are shorter than the fragment time
 *           reported by the program.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <soundcard.h>
#ifdef _AIX
#include <sys/select.h>
#endif

typedef void (*measure_f_t) (int fd);

measure_f_t measure_f = NULL;
char *name = "Unknown";

char *dspdev = "/dev/dsp";
int mode = 0;
int fd = -1;
int speed = 48000;
int bits = 16;
int fmt = AFMT_S16_LE;
int channels = 2;
unsigned char silence = 0;
int fragsize = 0;		/* Use default */
int fragcount = 0x7fff;		/* Unlimited */
int write_size = 0;
int write_byte = 0;
int raw_mode = 0;
int loop_delay = 0;

int data_rate, buffer_size;

static void
player (int fd)
{
  char *buffer;

  if ((buffer = malloc (write_size)) == NULL)
    {
      fprintf (stderr, "Out of memory\n");
      exit (EXIT_FAILURE);
    }

  memset (buffer, silence, write_size);

  while (1)
    {
      if (write (fd, buffer, write_size) != write_size)
	{
	  perror ("write");
	  exit (EXIT_FAILURE);
	}

      if (loop_delay > 0)
	usleep (loop_delay * 1000);

      measure_f (fd);

      write_byte += write_size;
    }
}

static void
print_spacing (int i)
{
  if ((i % 10) == 0)
    {
      printf ("%d", (i / 10) % 10);
      return;
    }

  printf (".");
}

static void
error_check (int fd)
{
  audio_errinfo err;

  if (ioctl (fd, SNDCTL_DSP_GETERROR, &err) == -1)
    return;

  if (err.play_underruns > 0)
    printf (" %d underruns\n", err.play_underruns);
}

static void
measure_getoptr (int fd)
{
  count_info ci;
  int i, n;

  if (ioctl (fd, SNDCTL_DSP_GETOPTR, &ci) == -1)
    {
      perror ("SNDCTL_DSP_GETOPTR");
      exit (EXIT_FAILURE);
    }

  n = (100 * ci.ptr + buffer_size / 2) / buffer_size;

  printf ("w=%8d, t=%8d ms, p=%6d : ", write_byte,
	  (1000 * write_byte + data_rate / 2) / data_rate, ci.ptr);

  for (i = 0; i < n; i++)
    print_spacing (i);
  printf ("*");

  if (n < 100)
    {
      for (i = n + 1; i < 100; i++)
	print_spacing (i);
      printf ("%%");
    }

  error_check (fd);
  printf ("\n");
  fflush (stdout);
}

static void
measure_getospace (int fd)
{
  audio_buf_info bi;
  int i, n;

  if (ioctl (fd, SNDCTL_DSP_GETOSPACE, &bi) == -1)
    {
      perror ("SNDCTL_DSP_GETOSPACE");
      exit (EXIT_FAILURE);
    }

  n = (100 * bi.bytes + buffer_size / 2) / buffer_size;

  printf ("w=%8d, t=%8d ms, p=%6d : ", write_byte,
	  (1000 * write_byte + data_rate / 2) / data_rate, bi.bytes);

  for (i = 0; i < n; i++)
    print_spacing (i);
  printf ("*");

  if (n < 100)
    {
      for (i = n + 1; i < 100; i++)
	print_spacing (i);
      printf ("%%");
    }

  error_check (fd);
  printf ("\n");
  fflush (stdout);
}

static void
measure_getodelay (int fd)
{
  int d, i, n;

  if (ioctl (fd, SNDCTL_DSP_GETODELAY, &d) == -1)
    {
      perror ("SNDCTL_DSP_GETODELAY");
      exit (EXIT_FAILURE);
    }

  n = (100 * d + buffer_size / 2) / buffer_size;

  printf ("w=%8d, t=%8d ms, d=%6d : ", write_byte,
	  (1000 * write_byte + data_rate / 2) / data_rate, d);

  for (i = 0; i < n; i++)
    print_spacing (i);
  printf ("*");

  if (n < 100)
    {
      for (i = n + 1; i < 100; i++)
	print_spacing (i);
      printf ("%%");
    }

  error_check (fd);
  printf ("\n");
  fflush (stdout);
}

int
main (int argc, char *argv[])
{
  int i;
  int tmp;
  audio_buf_info bi;

  int open_mode = O_WRONLY;

  measure_f = measure_getospace;

  while ((i = getopt (argc, argv, "rBd:m:s:c:b:f:n:w:D:")) != EOF)
    switch (i)
      {
      case 'd':
	dspdev = optarg;
	break;

      case 'm':
	mode = atoi (optarg);
	break;

      case 's':
	speed = atoi (optarg);
	if (speed < 200)
	  speed *= 1000;
	break;

      case 'c':
	channels = atoi (optarg);
	break;

      case 'b':
	bits = atoi (optarg);
	break;

      case 'f':
	fragsize = atoi (optarg);
	break;

      case 'n':
	fragcount = atoi (optarg);
	break;

      case 'w':
	write_size = atoi (optarg);
	break;

      case 'D':
	loop_delay = atoi (optarg);
	break;

      case 'r':
	raw_mode = 1;
	break;

      case 'B':
	open_mode |= O_EXCL;
	break;
      }

  switch (bits)
    {
    case 8:
      bits = AFMT_U8;
      silence = 0x80;
      break;
    case 16:
      bits = AFMT_S16_NE;
      break;
    case 32:
      bits = AFMT_S32_LE;
      break;
    default:
      fprintf (stderr, "Bad numer of bits %d\n", bits);
      exit (EXIT_FAILURE);
    }

  switch (mode)
    {
    case 0:
      measure_f = measure_getoptr;
      name = "Getoptr";
      break;
    case 1:
      measure_f = measure_getodelay;
      name = "Getodelay";
      break;
    case 2:
      measure_f = measure_getospace;
      name = "Getospace";
      break;

    default:
      fprintf (stderr, "Bad mode -m %d\n", mode);
      exit (EXIT_FAILURE);
    }

  if ((fd = open (dspdev, open_mode, 0)) == -1)
    {
      perror (dspdev);
      exit (EXIT_FAILURE);
    }

  if (raw_mode)
    {
      tmp = 0;
      ioctl (fd, SNDCTL_DSP_COOKEDMODE, &tmp);	/* Ignore errors */
    }

  if (fragsize != 0)
    {
      fragsize = (fragsize & 0xffff) | ((fragcount & 0x7fff) << 16);
      ioctl (fd, SNDCTL_DSP_SETFRAGMENT, &fragsize);	/* Ignore errors */
    }

  tmp = fmt;
  if (ioctl (fd, SNDCTL_DSP_SETFMT, &tmp) == -1)
    {
      perror ("SNDCTL_DSP_SETFMT");
      exit (EXIT_FAILURE);
    }

  if (tmp != fmt)
    {
      fprintf (stderr,
	       "Failed to select the requested sample format (%x, %x)\n", fmt,
	       tmp);
      exit (EXIT_FAILURE);
    }

  tmp = channels;
  if (ioctl (fd, SNDCTL_DSP_CHANNELS, &tmp) == -1)
    {
      perror ("SNDCTL_DSP_CHANNELS");
      exit (EXIT_FAILURE);
    }

  if (tmp != channels)
    {
      fprintf (stderr, "Failed to select the requested #channels (%d, %d)\n",
	       channels, tmp);
      exit (EXIT_FAILURE);
    }

  tmp = speed;
  if (ioctl (fd, SNDCTL_DSP_SPEED, &tmp) == -1)
    {
      perror ("SNDCTL_DSP_SPEED");
      exit (EXIT_FAILURE);
    }

  if (tmp != speed)
    {
      fprintf (stderr, "Failed to select the requested rate (%d, %d)\n",
	       speed, tmp);
      exit (EXIT_FAILURE);
    }

  if (ioctl (fd, SNDCTL_DSP_GETOSPACE, &bi) == -1)
    {
      perror ("SNDCTL_DSP_GETOSPACE");
      exit (EXIT_FAILURE);
    }

  buffer_size = bi.fragsize * bi.fragstotal;

  data_rate = speed * channels * (bits / 8);

  printf ("fragsize %d, nfrags %d, total buffer %d (bytes)\n", bi.fragsize,
	  bi.fragstotal, buffer_size);

  if (write_size == 0)
    write_size = bi.fragsize;

  printf ("Sample rate rate %d Hz, channels %d, bits %d\n", speed, channels,
	  bits);
  printf ("Data rate %d bytes / second\n", data_rate);
  printf ("Fragment time %d ms\n",
	  (1000 * bi.fragsize + data_rate / 2) / data_rate);
  printf ("Buffer time %d ms\n",
	  (1000 * buffer_size + data_rate / 2) / data_rate);
  printf ("Write size %d bytes, write time %d ms\n", write_size,
	  (1000 * write_size + data_rate / 2) / data_rate);

  printf ("\n");
  printf ("*** Starting test %d (%s)\n", mode, name);
  printf ("\n");

  player (fd);
  exit (0);
}
