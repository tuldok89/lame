#include "util.h"
#include "get_audio.h"
#include "globalflags.h"
#ifdef HAVEGTK
#include "gtkanal.h"
#include <gtk/gtk.h>
#endif

static FILE *musicin=NULL;  /* input file pointer */
static unsigned long num_samples;
static int samp_freq;
static int input_bitrate;
static int num_channels;

int read_samples_pcm( short sample_buffer[2304],int frame_size, int samples_to_read);
int read_samples_mp3(FILE *musicin,short int mpg123pcm[2][1152],int num_chan);

/* read mp3 file until mpglib returns one frame of PCM data */
#ifdef AMIGA_MPEGA
int lame_decode_initfile(const char *fullname,int *stereo,int *samp,int *bitrate, unsigned long *nsamp);
#else
int lame_decode_initfile(FILE *fd,int *stereo,int *samp,int *bitrate, unsigned long *nsamp);
#endif
int lame_decode_fromfile(FILE *fd,short int mpg123pcm[2][1152]);



void lame_init_infile(void)
{
  /* open the input file */
  OpenSndFile(gf.inPath,gf.samplerate,gf.num_channels);  
  /* if GetSndSampleRate is non zero, use it to overwrite the default */
  if (GetSndSampleRate()) gf.samplerate=GetSndSampleRate();
  if (GetSndChannels()) gf.num_channels=GetSndChannels();
  gf.num_samples = GetSndSamples();
}
void lame_close_infile(void)
{
  CloseSndFile();
}




/************************************************************************
*
* lame_readframe()
*
* PURPOSE:  reads a frame of audio data from a file to the buffer,
*   aligns the data for future processing, and separates the
*   left and right channels
*
*
************************************************************************/
int lame_readframe(short int Buffer[2][1152])
{
  int iread;

  /* note: if input is gf.stereo and output is mono, get_audio() 
   * will  .5*(L+R) in channel 0,  and nothing in channel 1. */
  /* DOWNSAMPLE, ETC */
  if (gf.resample_ratio!=1) {
    /* input frequency =  output freq*resample_ratio; */
    iread=get_audio_resample(Buffer,gf.resample_ratio,gf.stereo);
  }else{
    iread = get_audio(Buffer,gf.stereo);
  }

  /* check to see if we overestimated/underestimated totalframes */
  if (iread==0)  gf.totalframes = Min(gf.totalframes,gf.frameNum+2);
  if (gf.frameNum > (gf.totalframes-1)) gf.totalframes = gf.frameNum;
  return iread;
}





/************************************************************************
*
* get_audio()
*
* PURPOSE:  reads a frame of audio data from a file to the buffer,
*   aligns the data for future processing, and separates the
*   left and right channels
*
*
************************************************************************/
int get_audio(short buffer[2][1152],int stereo)
{

  int		j;
  short	insamp[2304];
  int samples_read;
  int framesize,samples_to_read;
  static unsigned long num_samples_read;
  int remaining;
  int autoconvert=0;
  int num_channels = gf.num_channels;

  if (gf.frameNum==0) {
    num_samples_read=0;
    num_samples= GetSndSamples();
  }
  if (num_channels==2 && gf.stereo==1) autoconvert=1;

  remaining=num_samples-num_samples_read;
  framesize = gf.mode_gr*576;
  samples_to_read = (remaining > framesize ? framesize : remaining);
  if (samples_to_read == -1 ) /* we don't know how much to read */
    samples_to_read = framesize;
  if (samples_to_read<0) samples_to_read=0;


  if (gf.input_format==sf_mp3) {
    samples_read=read_samples_mp3(musicin,buffer,num_channels);
  }else{
    samples_read = read_samples_pcm(insamp,num_channels*framesize,num_channels*samples_to_read);
    samples_read /=num_channels;

    for(j=0;j<framesize;j++) {
      buffer[0][j] = insamp[num_channels*j];
      if (num_channels==2) buffer[1][j] = insamp[2*j+1];
      else buffer[1][j]=0;
      if (autoconvert) {
	buffer[0][j] = (buffer[0][j] + buffer[1][j])/2;
	buffer[1][j] = 0;
      }
    }
  }

  /* dont count things in this case to avoid overflows */
  if (num_samples!=MAX_U_32_NUM) num_samples_read += samples_read;
  return(samples_read);

}
  

/************************************************************************
*
* get_audio_resample()
*
* PURPOSE:  reads a frame of audio data from a file to the buffer,
*   aligns the data for future processing, separates the
*   left and right channels and resamples to desired sampling rate.  
*
*
************************************************************************/
int get_audio_resample(short int Buffer[2][1152],FLOAT resample_ratio,int stereo)
{

  typedef short int sample_buffer[2][1152];
  static sample_buffer bufa[2];
  static sample_buffer *r0,*r1,*swap;
  static int iread=1;
  static FLOAT itime=0;

  FLOAT time0,time1,time2,x1,x2;
  int rcode,out_framesize,ch,j,k;
  static int in_framesize;

  out_framesize = gf.mode_gr*576;
  rcode=out_framesize;  
  /* if during the last call to resample we got an iread=0, set rcode=0 */
  if (iread==0) rcode=0;


  if (gf.frameNum==0) {
    r0=&bufa[0];
    r1=&bufa[1];
    in_framesize=out_framesize;
    iread=get_audio(*r0,stereo);
    if (iread>0) in_framesize=iread;
    iread=get_audio(*r1,stereo);
  }

  /* time of j'th element in r0] = itime + j/ifreq; */
  /* time of j'th element in r1] = itime + (j+in_framesize)/ifreq; */
  /* time of k'th element in Buffer   =  j/ofreq */

  for (k=0;k<out_framesize;k++) {
    time0 = k*resample_ratio;       /* time of k'th output sample */
    j = floor( time0 -itime  );  
    time1 = itime + j;              /* time of j'th input sample */
    time2 = time1 +1;               /* time of j+1th input sample */

    if (1+j >= 2*in_framesize) {
      fprintf(stderr,"strange error in linear_resample()");
      exit(1);
    }
    for (ch=0;ch<stereo;ch++) {
      x1 = (j<in_framesize) ? (*r0)[ch][j] : (*r1)[ch][j-in_framesize];
      x2 = ((1+j)<in_framesize) ? (*r0)[ch][1+j] : (*r1)[ch][1+j-in_framesize];
      Buffer[ch][k] = (x1*(time2-time0) + x2*(time0-time1));

    }

    if ((1+j)>=in_framesize) {
      swap=r0; r0=r1; r1=swap;
      iread = get_audio(*r1,stereo);
      itime += in_framesize;
    }
    /*    printf("k=%i numch=%i  buffer = %i %i \n",k,stereo,Buffer[0][k],Buffer[1][k]);*/
  }
  itime -= out_framesize*resample_ratio;
  return rcode;
}




















  
int GetSndBitrate()
{
	return input_bitrate;
}






int read_samples_mp3(FILE *musicin,short int mpg123pcm[2][1152],int stereo)
{
  int j,out=0;
#ifdef HAVEGTK
  static int framesize=0;
  int ch;
#endif
  int autoconvert=0;
  if (gf.num_channels==2  && gf.stereo==1) autoconvert=1;

  out=lame_decode_fromfile(musicin,mpg123pcm);
  /* out = -1:  error, probably EOF */
  /* out = 0:   not possible with lame_decode_fromfile() */
  /* out = number of output samples */

  if (out==-1) {
    for ( j = 0; j < 1152; j++ ) {
      mpg123pcm[0][j] = 0;
      mpg123pcm[1][j] = 0;
    }
  }
  if (autoconvert) {
    for ( j = 0; j < out; j++ ) {
      mpg123pcm[0][j] = ((int)mpg123pcm[0][j] + (int)mpg123pcm[1][j])/2;
      mpg123pcm[1][j] = 0;
    }
  }


#ifdef HAVEGTK
  if (gf.gtkflag) {
    framesize=1152;
    if (out==576) framesize=576;
    
    /* add a delay of framesize-DECDELAY, which will make the total delay
     * exactly one frame, so we can sync MP3 output with WAV input */
    
    for ( ch = 0; ch < stereo; ch++ ) {
      for ( j = 0; j < framesize-DECDELAY; j++ )
	pinfo->pcmdata2[ch][j] = pinfo->pcmdata2[ch][j+framesize];
      for ( j = 0; j < framesize; j++ ) 
	pinfo->pcmdata2[ch][j+framesize-DECDELAY] = mpg123pcm[ch][j];
    }
  
  pinfo->frameNum123 = gf.frameNum-1;
  pinfo->frameNum = gf.frameNum;
  }
#endif
  if (out==-1) return 0;
  else return out;
}






#ifdef LIBSNDFILE 
/*
** Copyright (C) 1999 Albert Faber
**  
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#include <stdio.h>

/* External references */

static SNDFILE*	gs_pSndFileIn=NULL;
static SF_INFO	gs_wfInfo;


unsigned long GetSndSamples(void)
{
       return gs_wfInfo.samples;
}
int GetSndSampleRate(void)
{
	return gs_wfInfo.samplerate;
}

int GetSndChannels(void)
{
	return gs_wfInfo.channels;
}

void CloseSndFile(void)
{
  if (gf.input_format==sf_mp3) {
#ifndef AMIGA_MPEGA
    if (fclose(musicin) != 0){
      fprintf(stderr, "Could not close audio input file\n");
      exit(2);
    }
#endif
  }else{
	if (gs_pSndFileIn)
	{
		if (sf_close(gs_pSndFileIn) !=0)
		{
			fprintf(stderr, "Could not close sound file \n");
			exit(2);
		}
	}
  }
}



FILE * OpenSndFile(const char* lpszFileName, int default_samp,
int default_channels)
{
  input_bitrate=0;
  if (gf.input_format==sf_mp3) {
#ifndef AMIGA_MPEGA
    if ((musicin = fopen(lpszFileName, "rb")) == NULL) {
      fprintf(stderr, "Could not find \"%s\".\n", lpszFileName);
      exit(1);
    }
#endif
#ifdef AMIGA_MPEGA
    if (-1==lame_decode_initfile(lpszFileName,&num_channels,&samp_freq,&input_bitrate,&num_samples)) {
#else
    if (-1==lame_decode_initfile(musicin,&num_channels,&samp_freq,&input_bitrate,&num_samples)) {
#endif
      fprintf(stderr,"Error reading headers in mp3 input file %s.\n", lpszFileName);
      exit(1);
    }
    gs_wfInfo.samples=num_samples;
    gs_wfInfo.channels=num_channels;
    gs_wfInfo.samplerate=samp_freq;

  } else {

    /* Try to open the sound file */
    /* set some defaults incase input is raw PCM */
    gs_wfInfo.seekable=(gf.input_format!=sf_raw);  /* if user specified -r, set to not seekable */
    gs_wfInfo.samplerate=default_samp;
    gs_wfInfo.pcmbitwidth=16;
    gs_wfInfo.channels=default_channels;
    if (DetermineByteOrder()==order_littleEndian)
      gs_wfInfo.format=SF_FORMAT_RAW_LE;
    else
      gs_wfInfo.format=SF_FORMAT_RAW_BE;

    gs_pSndFileIn=sf_open_read(lpszFileName,&gs_wfInfo);

        /* Check result */
	if (gs_pSndFileIn==NULL)
	{
	        sf_perror(gs_pSndFileIn);
		fprintf(stderr, "Could not open sound file \"%s\".\n", lpszFileName);
		exit(1);
	}

    if ((gs_wfInfo.format==SF_FORMAT_RAW_LE) || 
	(gs_wfInfo.format==SF_FORMAT_RAW_BE)) 
      gf.input_format=sf_raw;

#ifdef _DEBUG_SND_FILE
	printf("\n\nSF_INFO structure\n");
	printf("samplerate        :%d\n",gs_wfInfo.samplerate);
	printf("samples           :%d\n",gs_wfInfo.samples);
	printf("channels          :%d\n",gs_wfInfo.channels);
	printf("pcmbitwidth       :%d\n",gs_wfInfo.pcmbitwidth);
	printf("format            :");

	/* new formats from sbellon@sbellon.de  1/2000 */
        if ((gs_wfInfo.format&SF_FORMAT_TYPEMASK)==SF_FORMAT_WAV)
	  printf("Microsoft WAV format (big endian). ");
	if ((gs_wfInfo.format&SF_FORMAT_TYPEMASK)==SF_FORMAT_AIFF)
	  printf("Apple/SGI AIFF format (little endian). ");
	if ((gs_wfInfo.format&SF_FORMAT_TYPEMASK)==SF_FORMAT_AU)
	  printf("Sun/NeXT AU format (big endian). ");
	if ((gs_wfInfo.format&SF_FORMAT_TYPEMASK)==SF_FORMAT_AULE)
	  printf("DEC AU format (little endian). ");
	if ((gs_wfInfo.format&SF_FORMAT_TYPEMASK)==SF_FORMAT_RAW)
	  printf("RAW PCM data. ");
	if ((gs_wfInfo.format&SF_FORMAT_TYPEMASK)==SF_FORMAT_PAF)
	  printf("Ensoniq PARIS file format. ");
	if ((gs_wfInfo.format&SF_FORMAT_TYPEMASK)==SF_FORMAT_SVX)
	  printf("Amiga IFF / SVX8 / SV16 format. ");
	if ((gs_wfInfo.format&SF_FORMAT_TYPEMASK)==SF_FORMAT_NIST)
	  printf("Sphere NIST format. ");

	if ((gs_wfInfo.format&SF_FORMAT_SUBMASK)==SF_FORMAT_PCM)
	  printf("PCM data in 8, 16, 24 or 32 bits.");
	if ((gs_wfInfo.format&SF_FORMAT_SUBMASK)==SF_FORMAT_FLOAT)
	  printf("32 bit Intel x86 floats.");
	if ((gs_wfInfo.format&SF_FORMAT_SUBMASK)==SF_FORMAT_ULAW)
	  printf("U-Law encoded.");
	if ((gs_wfInfo.format&SF_FORMAT_SUBMASK)==SF_FORMAT_ALAW)
	  printf("A-Law encoded.");
	if ((gs_wfInfo.format&SF_FORMAT_SUBMASK)==SF_FORMAT_IMA_ADPCM)
	  printf("IMA ADPCM.");
	if ((gs_wfInfo.format&SF_FORMAT_SUBMASK)==SF_FORMAT_MS_ADPCM)
	  printf("Microsoft ADPCM.");
	if ((gs_wfInfo.format&SF_FORMAT_SUBMASK)==SF_FORMAT_PCM_BE)
	  printf("Big endian PCM data.");
	if ((gs_wfInfo.format&SF_FORMAT_SUBMASK)==SF_FORMAT_PCM_LE)
	  printf("Little endian PCM data.");
	if ((gs_wfInfo.format&SF_FORMAT_SUBMASK)==SF_FORMAT_PCM_S8)
	  printf("Signed 8 bit PCM.");
	if ((gs_wfInfo.format&SF_FORMAT_SUBMASK)==SF_FORMAT_PCM_U8)
	  printf("Unsigned 8 bit PCM.");
	if ((gs_wfInfo.format&SF_FORMAT_SUBMASK)==SF_FORMAT_SVX_FIB)
	  printf("SVX Fibonacci Delta encoding.");
	if ((gs_wfInfo.format&SF_FORMAT_SUBMASK)==SF_FORMAT_SVX_EXP)
	  printf("SVX Exponential Delta encoding.");




	printf("\n");
	printf("pcmbitwidth       :%d\n",gs_wfInfo.pcmbitwidth);
	printf("sections          :%d\n",gs_wfInfo.sections);
	printf("seekable          :\n",gs_wfInfo.seekable);
#endif
  }

  if (gs_wfInfo.samples==MAX_U_32_NUM) {
    struct stat sb;
    /* try to figure out num_samples */
    stat(lpszFileName,&sb);  /* try file size, assume 2 bytes per sample */
    if (gf.input_format == sf_mp3) {
      FLOAT totalseconds = (sb.st_size*8.0/(1000.0*GetSndBitrate()));
      gs_wfInfo.samples= totalseconds*GetSndSampleRate();
    }else{
      gs_wfInfo.samples = sb.st_size/(2*GetSndChannels());
    }
  }
  return musicin;    
}


/************************************************************************
*
* read_samples()
*
* PURPOSE:  reads the PCM samples from a file to the buffer
*
*  SEMANTICS:
* Reads #samples_read# number of shorts from #musicin# filepointer
* into #sample_buffer[]#.  Returns the number of samples read.
*
************************************************************************/

int read_samples_pcm(short sample_buffer[2304],int frame_size,int samples_to_read)
{
    int 		samples_read;
    int			rcode;

    samples_read=sf_read_short(gs_pSndFileIn,sample_buffer,samples_to_read);
    
    rcode = samples_read;
    if (samples_read < frame_size)
      {
	/*fprintf(stderr,"Insufficient PCM input for one frame - fillout with zeros\n"); 
	*/
	if (samples_read<0) samples_read=0;
	for (; samples_read < frame_size; sample_buffer[samples_read++] = 0);
      }

	if (8==gs_wfInfo.pcmbitwidth)
	  for (; samples_read >= 0; sample_buffer[samples_read] = sample_buffer[samples_read--] * 256);

    return(rcode);
}


#else /* ifdef LIBSNDFILE */

/************************************************************************
 ************************************************************************
 ************************************************************************
 ************************************************************************
 ************************************************************************
 ************************************************************************
 *
 * OLD ISO/LAME routines follow.  Used if you dont have LIBSNDFILE
 * or for stdin/stdout support
 *
 ************************************************************************
 ************************************************************************
 ************************************************************************
 ************************************************************************
 ************************************************************************
 ************************************************************************/

/* Replacement for forward fseek(,,SEEK_CUR), because fseek() fails on pipes */
int fskip(FILE *sf,long num_bytes,int dummy)
{
  char data[1024];
  int nskip = 0;
  while (num_bytes > 0) {
    nskip = (num_bytes>1024) ? 1024 : num_bytes;
    num_bytes -= fread(data,(size_t)1,(size_t)nskip,sf);
  }
  /* return 0 if last read was successful */
  return num_bytes;
}



void CloseSndFile(void)
{
  if (fclose(musicin) != 0){
    fprintf(stderr, "Could not close audio input file\n");
    exit(2);
  }
}


unsigned long GetSndSamples(void)
{
       return num_samples;
}
int GetSndSampleRate(void)
{
	return samp_freq;
}

int GetSndChannels(void)
{
	return num_channels;
}


FILE * OpenSndFile(const char* inPath, int default_samp,
int default_channels)
{
  struct stat sb;
  void parse_file_header(FILE *sf);
  /* set the defaults from info incase we cannot determine them from file */
  num_samples=MAX_U_32_NUM;
  samp_freq=default_samp;
  num_channels = default_channels;
  
  if (!strcmp(inPath, "-")) {
    /* Read from standard input. */
#ifdef __EMX__
    _fsetmode(stdin,"b");
#elif (defined  __BORLANDC__)
    setmode(_fileno(stdin), O_BINARY);
#elif (defined  __CYGWIN__)
    setmode(fileno(stdin), _O_BINARY);
#elif (defined _WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    musicin = stdin;
  } else {
    if ((musicin = fopen(inPath, "rb")) == NULL) {
      fprintf(stderr, "Could not find \"%s\".\n", inPath);
      exit(1);
    }
  }
  
  input_bitrate=0;
  if (gf.input_format==sf_mp3) {
#ifdef AMIGA_MPEGA
    if (-1==lame_decode_initfile(inPath,&num_channels,&samp_freq,&input_bitrate,&num_samples)) {
#else
    if (-1==lame_decode_initfile(musicin,&num_channels,&samp_freq,&input_bitrate,&num_samples)) {
#endif
      fprintf(stderr,"Error reading headers in mp3 input file %s.\n", inPath);
      exit(1);
    }
 }else{
   if (gf.input_format != sf_raw) {
     parse_file_header(musicin);
   }
   
   if (gf.input_format==sf_raw) {
     /* assume raw PCM */
     fprintf(stderr, "Assuming raw pcm input file");
     if (gf.swapbytes==TRUE)
       fprintf(stderr, " : Forcing byte-swapping\n");
     else
       fprintf(stderr, "\n");
   }
 }
    
  if (num_samples==MAX_U_32_NUM && musicin != stdin) {
    /* try to figure out num_samples */
    stat(inPath,&sb);  /* try file size, assume 2 bytes per sample */
    if (gf.input_format == sf_mp3) {
      FLOAT totalseconds = (sb.st_size*8.0/(1000.0*GetSndBitrate()));
      num_samples= totalseconds*GetSndSampleRate();
    }else{
      num_samples = sb.st_size/(2*GetSndChannels());
    }
  }
  return musicin;
}
  
  
/************************************************************************
*
* read_samples()
*
* PURPOSE:  reads the PCM samples from a file to the buffer
*
*  SEMANTICS:
* Reads #samples_read# number of shorts from #musicin# filepointer
* into #sample_buffer[]#.  Returns the number of samples read.
*
************************************************************************/

int read_samples_pcm(short sample_buffer[2304], int frame_size,int samples_to_read)
{
    int samples_read;
    int rcode;
    int iswav=(gf.input_format==sf_wave);

    samples_read = fread(sample_buffer, sizeof(short), samples_to_read, musicin);
    if (ferror(musicin)) {
      fprintf(stderr, "Error reading input file\n");
      exit(2);
    }

    /*
       Samples are big-endian. If this is a little-endian machine
       we must swap
     */
    if ( NativeByteOrder == order_unknown )
      {
	NativeByteOrder = DetermineByteOrder();
	if ( NativeByteOrder == order_unknown )
	  {
	    fprintf( stderr, "byte order not determined\n" );
	    exit( 1 );
	  }
      }
    if (!iswav && ( NativeByteOrder == order_littleEndian ))
      SwapBytesInWords( sample_buffer, samples_read );

    if (iswav && ( NativeByteOrder == order_bigEndian ))
      SwapBytesInWords( sample_buffer, samples_read );

    if (gf.swapbytes==TRUE)
      SwapBytesInWords( sample_buffer, samples_read );


    rcode=samples_read;
    if (samples_read < frame_size) {
      if (samples_read<0) samples_read=0;
      /*fprintf(stderr,"Insufficient PCM input for one frame - fillout with zeros\n");
      */
      for (; samples_read < frame_size; sample_buffer[samples_read++] = 0);
    }
    return(rcode);
}



#define WAV_ID_RIFF 0x52494646 /* "RIFF" */
#define WAV_ID_WAVE 0x57415645 /* "WAVE" */
#define WAV_ID_FMT  0x666d7420 /* "fmt " */
#define WAV_ID_DATA 0x64617461 /* "data" */

typedef struct fmt_chunk_data_struct {
	short	format_tag;			 /* Format category */
	u_short channels;			 /* Number of channels */
	u_long	samples_per_sec;	 /* Sampling rate */
	u_long	avg_bytes_per_sec;	 /* For buffer estimation */
	u_short block_align;		 /* Data block size */
	u_short bits_per_sample;	 /* for PCM data, anyway... */
} fmt_chunk_data;






/************************************************************************
*
* wave_check
*
* PURPOSE:	Checks Wave header information to make sure it is valid.
*			Exits if not.
*
************************************************************************/

static void
wave_check(char *file_name, fmt_chunk_data *wave_info)
{
	if (wave_info->bits_per_sample != 16) {
		fprintf(stderr, "%d-bit sample-size is not supported!\n",
			wave_info->bits_per_sample);
		exit(1);
	}
}


/*****************************************************************************
 *
 *	Read Microsoft Wave headers
 *
 *	By the time we get here the first 32-bits of the file have already been
 *	read, and we're pretty sure that we're looking at a WAV file.
 *
 *****************************************************************************/

static int
parse_wave_header(FILE *sf)
{
	fmt_chunk_data wave_info;
	int is_wav = 0;
	long data_length = 0, file_length, subSize = 0;
	int loop_sanity = 0;

	memset(&wave_info, 0, sizeof(wave_info));

	file_length = Read32BitsHighLow(sf);

	if (Read32BitsHighLow(sf) != WAV_ID_WAVE)
		return 0;

	for (loop_sanity = 0; loop_sanity < 20; ++loop_sanity) {
		u_int type = Read32BitsHighLow(sf);

		if (type == WAV_ID_FMT) {
			subSize = Read32BitsLowHigh(sf);
			if (subSize < 16) {
			  /*fprintf(stderr,
			    "'fmt' chunk too short (only %ld bytes)!", subSize);  */
				return 0;
			}

			wave_info.format_tag		= Read16BitsLowHigh(sf);
			subSize -= 2;
			wave_info.channels			= Read16BitsLowHigh(sf);
			subSize -= 2;
			wave_info.samples_per_sec	= Read32BitsLowHigh(sf);
			subSize -= 4;
			wave_info.avg_bytes_per_sec = Read32BitsLowHigh(sf);
			subSize -= 4;
			wave_info.block_align		= Read16BitsLowHigh(sf);
			subSize -= 2;
			wave_info.bits_per_sample	= Read16BitsLowHigh(sf);
			subSize -= 2;

			/* fprintf(stderr, "   skipping %d bytes\n", subSize); */

			if (subSize > 0) {
				if (fskip(sf, (long)subSize, SEEK_CUR) != 0 )
					return 0;
			};

		} else if (type == WAV_ID_DATA) {
			subSize = Read32BitsLowHigh(sf);
			data_length = subSize;
			is_wav = 1;
			/* We've found the audio data.	Read no further! */
			break;

		} else {
			subSize = Read32BitsLowHigh(sf);
			if (fskip(sf, (long) subSize, SEEK_CUR) != 0 ) return 0;
		}
	}

	if (is_wav) {
		/* make sure the header is sane */
		wave_check("name", &wave_info);

		num_channels  = wave_info.channels;
		samp_freq     = wave_info.samples_per_sec;
		num_samples   = data_length / (wave_info.channels * wave_info.bits_per_sample / 8);
	}
	return is_wav;
}



/************************************************************************
*
* aiff_check
*
* PURPOSE:	Checks AIFF header information to make sure it is valid.
*			Exits if not.
*
************************************************************************/

static void
aiff_check2(const char *file_name, IFF_AIFF *pcm_aiff_data)
{
	if (pcm_aiff_data->sampleType != IFF_ID_SSND) {
	   fprintf(stderr, "Sound data is not PCM in \"%s\".\n", file_name);
	   exit(1);
	}

	if (pcm_aiff_data->sampleSize != sizeof(short) * BITS_IN_A_BYTE) {
		fprintf(stderr, "Sound data is not %d bits in \"%s\".\n",
				(unsigned int) sizeof(short) * BITS_IN_A_BYTE, file_name);
		exit(1);
	}

	if (pcm_aiff_data->numChannels != 1 &&
		pcm_aiff_data->numChannels != 2) {
	   fprintf(stderr, "Sound data is not mono or stereo in \"%s\".\n",
			   file_name);
	   exit(1);
	}

	if (pcm_aiff_data->blkAlgn.blockSize != 0) {
	   fprintf(stderr, "Block size is not %d bytes in \"%s\".\n",
			   0, file_name);
	   exit(1);
	}

	if (pcm_aiff_data->blkAlgn.offset != 0) {
	   fprintf(stderr, "Block offset is not %d bytes in \"%s\".\n",
			   0, file_name);
	   exit(1);
	}
}

/*****************************************************************************
 *
 *	Read Audio Interchange File Format (AIFF) headers.
 *
 *	By the time we get here the first 32-bits of the file have already been
 *	read, and we're pretty sure that we're looking at an AIFF file.
 *
 *****************************************************************************/

static int
parse_aiff_header(FILE *sf)
{
	int is_aiff = 0;
	long chunkSize = 0, subSize = 0;
	IFF_AIFF aiff_info;

	memset(&aiff_info, 0, sizeof(aiff_info));
	chunkSize = Read32BitsHighLow(sf);
	
	if ( Read32BitsHighLow(sf) != IFF_ID_AIFF )
		return 0;
	
	while ( chunkSize > 0 )
	{
		u_int type = 0;
		chunkSize -= 4;

		type = Read32BitsHighLow(sf);

		/* fprintf(stderr,
			"found chunk type %08x '%4.4s'\n", type, (char*)&type); */

		/* don't use a switch here to make it easier to use 'break' for SSND */
		if (type == IFF_ID_COMM) {
			subSize = Read32BitsHighLow(sf);
			chunkSize -= subSize;

			aiff_info.numChannels	  = Read16BitsHighLow(sf);
			subSize -= 2;
			aiff_info.numSampleFrames = Read32BitsHighLow(sf);
			subSize -= 4;
			aiff_info.sampleSize	  = Read16BitsHighLow(sf);
			subSize -= 2;
			aiff_info.sampleRate	  = ReadIeeeExtendedHighLow(sf);
			subSize -= 10;

			if (fskip(sf, (long) subSize, SEEK_CUR) != 0 )
				return 0;

		} else if (type == IFF_ID_SSND) {
			subSize = Read32BitsHighLow(sf);
			chunkSize -= subSize;

			aiff_info.blkAlgn.offset	= Read32BitsHighLow(sf);
			subSize -= 4;
			aiff_info.blkAlgn.blockSize = Read32BitsHighLow(sf);
			subSize -= 4;

			if (fskip(sf, aiff_info.blkAlgn.offset, SEEK_CUR) != 0 )
				return 0;

			aiff_info.sampleType = IFF_ID_SSND;
			is_aiff = 1;

			/* We've found the audio data.	Read no further! */
			break;
			
		} else {
			subSize = Read32BitsHighLow(sf);
			chunkSize -= subSize;

			if (fskip(sf, (long) subSize, SEEK_CUR) != 0 )
				return 0;
		}
	}

	/* fprintf(stderr, "Parsed AIFF %d\n", is_aiff); */
	if (is_aiff) {
		/* make sure the header is sane */
		aiff_check2("name", &aiff_info);
		num_channels  = aiff_info.numChannels;
		samp_freq     = aiff_info.sampleRate;
		num_samples   = aiff_info.numSampleFrames;
	}
	return is_aiff;
}



/************************************************************************
*
* parse_file_header
*
* PURPOSE: Read the header from a bytestream.  Try to determine whether
*		   it's a WAV file or AIFF without rewinding, since rewind
*		   doesn't work on pipes and there's a good chance we're reading
*		   from stdin (otherwise we'd probably be using libsndfile).
*
* When this function returns, the file offset will be positioned at the
* beginning of the sound data.
*
************************************************************************/

void parse_file_header(FILE *sf)
{
	u_int type = 0;
	type = Read32BitsHighLow(sf);

	/* fprintf(stderr,
		"First word of input stream: %08x '%4.4s'\n", type, (char*) &type); */

	gf.input_format = sf_raw;

	if (type == WAV_ID_RIFF) {
		/* It's probably a WAV file */
		if (parse_wave_header(sf)) {
			gf.input_format = sf_wave;
		}

	} else if (type == IFF_ID_FORM) {
		/* It's probably an AIFF file */
		if (parse_aiff_header(sf)) {
			gf.input_format = sf_aiff;
		}
	}
	if (gf.input_format==sf_raw) {
	  /*
	  ** Assume it's raw PCM.	 Since the audio data is assumed to begin
	  ** at byte zero, this will unfortunately require seeking.
	  */
	  if (fseek(sf, 0L, SEEK_SET) != 0) {
	    /* ignore errors */
	  }
	  gf.input_format = sf_raw;
	}
}
#endif  /* LIBSNDFILE */


