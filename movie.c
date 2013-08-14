//===========================================================================
//
//
//
//
//===========================================================================


#include "3d_def.h"
#include "jm_io.h"
#include "an_codes.h"


boolean IN_CheckAck();
void VH_UpdateScreen();


//===========================================================================
//
//
//
//
//===========================================================================


void VL_LatchToScreen(int source, int width, int height, int x, int y);


//#define  DRAW_TO_FRONT

//
// Various types for various purposes...
//

typedef enum
{
  	FADE_NONE,
   FADE_SET,
   FADE_IN,
   FADE_OUT,
   FADE_IN_FRAME,
   FADE_OUT_FRAME,
} FADES;


typedef enum
{
   MV_NONE,
	MV_FILL,
   MV_SKIP,
   MV_READ,

} MOVIE_FLAGS;


//===========================================================================
//
//											VARIABLES
//
//===========================================================================


// Movie File variables

Sint16 Movie_FHandle;

// Fade Variables

FADES fade_flags, fi_type, fo_type;
Uint8	fi_rate,fo_rate;

// MOVIE_GetFrame & MOVIE_LoadBuffer variables

void* MovieBuffer;					// Ptr to Allocated Memory for Buffer
Uint32 BufferLen;			// Len of MovieBuffer (Ammount of RAM allocated)
Uint32 PageLen;				// Len of data loaded into MovieBuffer
char * BufferPtr;				// Ptr to next frame in MovieBuffer
char * NextPtr;   				// Ptr Ofs to next frame after BufferOfs

boolean MorePagesAvail;				// More Pages avail on disk?

//

MOVIE_FLAGS  movie_flag;
boolean ExitMovie;
boolean EverFaded;
Sint32 seek_pos;
char movie_reps;
ControlInfo ci;
void* movie_palette;


//
// MOVIE Definations for external movies
//
// NOTE: This list is ordered according to mv_???? enum list.
//


MovieStuff_t Movies[] =
{
	{{"IANIM."},1,3,0,0,200},				//mv_intro
	{{"EANIM."},1,30,0,0,200},				//mv_final
};



//===========================================================================
//
//										LOCAL PROTO TYPES
//
//===========================================================================

void JM_MemToScreen(void);
void JM_ClearVGAScreen(Uint8 fill);
void FlipPages(void);
boolean CheckFading(void);
boolean CheckPostFade(void);


//===========================================================================
//
//										   FUNCTIONS
//
//===========================================================================


//---------------------------------------------------------------------------
// SetupMovie() - Inits all the internal routines for the Movie Presenter
//
//
//
//---------------------------------------------------------------------------
void SetupMovie(MovieStuff_t *MovieStuff)
{
#ifdef DRAW_TO_FRONT
	bufferofs=displayofs;
#endif

   movie_reps = MovieStuff->rep;
	movie_flag = MV_FILL;
   LastScan = 0;										  
   PageLen = 0;
   MorePagesAvail = true;
	ExitMovie = false;
	EverFaded = false;
	IN_ClearKeysDown();

   movie_palette = MovieStuff->palette;
	JM_VGALinearFill(screenloc[0],3*80*208,0);

	VL_FillPalette (0,0,0);
   LastScan = 0;

   // Find out how much memory we have to work with..

// FIXME
#if 0
	BufferLen = MM_LargestAvail();
#endif // 0
    BufferLen = 65535;

   BufferLen -= 65535;						// HACK: Save some room for sounds - This is cludgey

   if (BufferLen < 64256)
   	BufferLen = 64256;

    MovieBuffer = malloc(BufferLen);
}


//---------------------------------------------------------------------------
// void ShutdownMovie(void)
//---------------------------------------------------------------------------
void ShutdownMovie(void)
{
    free(MovieBuffer);
    MovieBuffer = NULL;

   close (Movie_FHandle);
}

//---------------------------------------------------------------------------
// void JM_DrawBlock()
//
// dest_offset = Correct offset value for memory location for Paged/Latch mem
//
// byte_offset = Offset for the image to be drawn - This address is NOT
//					  a calculated Paged/Latch value but a byte offset in
//					  conventional memory.
//
// source		= Source image of graphic to be blasted to latch memory.  This
//					  pic is NOT 'munged'
//
// length		= length of the source image in bytes
//---------------------------------------------------------------------------

// FIXME
#if 0
void JM_DrawBlock(Uint16 dest_offset,Uint16 byte_offset,char *source,Uint16 length)
{
	Uint8 numplanes;
   Uint8 mask,plane;
	char *dest_ptr;
	char *source_ptr;
   char *dest;
   char *end_ptr;
   Uint16 count,total_len;


   end_ptr = source+length;

   //
   // Byte offset to determine our starting page to write to...
   //

   mask = 1<<(byte_offset & 3);

   //
   // Check to see if we are writting more than 4 bytes (to loop pages...)
   //

   if (length >= 4)
   	numplanes = 4;
   else
   	numplanes = length;

   //
   // Compute our DEST memory location
   //

   dest = (char*)0xA0000 + dest_offset + (byte_offset >> 2);

   //
   // Move that memory.
   //

	for (plane = 0; plane<numplanes; plane++)
	{
   	dest_ptr = dest;
	   source_ptr = source+plane;

		VGAMAPMASK(mask);
		mask <<= 1;
		if (mask == 16)
      {
			mask = 1;
         dest++;
      }

		for (count=0;count<length,source_ptr < end_ptr;count+=4,dest_ptr++,source_ptr+=4)
      	*dest_ptr = *source_ptr;
	}
}
#endif // 0

void JM_DrawBlock(
    int dest_offset,
    int byte_offset,
    const char* source,
    int length)
{
    char* dest;

    dest = (char*)&vga_memory[(4 * dest_offset) + byte_offset];
    memcpy(dest, source, length);
}



//---------------------------------------------------------------------------
// MOVIE_ShowFrame() - Shows an animation frame
//
// PARAMETERS: pointer to animpic
//---------------------------------------------------------------------------
void MOVIE_ShowFrame (char *inpic)
{
   anim_chunk *ah;

   if (inpic == NULL)
      return;

   for (;;)
   {
      ah = (anim_chunk *)inpic;

      if (ah->opt == 0)
			break;

      inpic += sizeof(anim_chunk);
		JM_DrawBlock(bufferofs, ah->offset, (char *)inpic, ah->length);
      inpic += ah->length;
   }
}



//---------------------------------------------------------------------------
// MOVIE_LoadBuffer() - Loads the RAM Buffer full of graphics...
//
// RETURNS:  true  	- MORE Pages avail on disk..
//				 false   - LAST Pages from disk..
//
// PageLen = Length of data loaded into buffer
//
//---------------------------------------------------------------------------
boolean MOVIE_LoadBuffer()
{
    anim_frame blk;
    long chunkstart;
    char* frame;
    Uint32 free_space;

    NextPtr = BufferPtr = frame = (char*)MovieBuffer;
    free_space = BufferLen;

    while (free_space) {
        chunkstart = tell(Movie_FHandle);

        if (!IO_FarRead(
            Movie_FHandle,
            &blk.code,
            sizeof(blk.code)))
        {
            AN_ERROR(AN_BAD_ANIM_FILE);
        }

        if (!IO_FarRead(
            Movie_FHandle,
            &blk.block_num,
            sizeof(blk.block_num)))
        {
            AN_ERROR(AN_BAD_ANIM_FILE);
        }

        if (!IO_FarRead(
            Movie_FHandle,
            &blk.recsize,
            sizeof(blk.recsize)))
        {
            AN_ERROR(AN_BAD_ANIM_FILE);
        }

        if (blk.code == AN_END_OF_ANIM)
            return false;

        if (free_space >= (blk.recsize + sizeof(anim_frame))) {
            memcpy(frame, &blk, sizeof(anim_frame));

            free_space -= sizeof(anim_frame);
            frame += sizeof(anim_frame);
            PageLen += sizeof(anim_frame);

            if (!IO_FarRead(Movie_FHandle, frame, blk.recsize))
                AN_ERROR(AN_BAD_ANIM_FILE);

            free_space -= blk.recsize;
            frame += blk.recsize;
            PageLen += blk.recsize;
        } else {
            lseek(Movie_FHandle, chunkstart, SEEK_SET);
            free_space = 0;
        }
    }

    return true;
}


//---------------------------------------------------------------------------
// MOVIE_GetFrame() - Returns pointer to next Block/Screen of animation
//
// PURPOSE: This function "Buffers" the movie presentation from allocated
//				ram.  It loads and buffers incomming frames of animation..
//
// RETURNS:  0 - Ok
//				 1 - End Of File
//---------------------------------------------------------------------------
Sint16 MOVIE_GetFrame()
{
	Uint16 ReturnVal;
   anim_frame blk;

	if (PageLen == 0)
   {
    	if (MorePagesAvail)
	      MorePagesAvail = MOVIE_LoadBuffer(Movie_FHandle);
      else
      	return(1);
	}

   BufferPtr = NextPtr;
	memcpy(&blk, BufferPtr, sizeof(anim_frame));
   PageLen-=sizeof(anim_frame);
   PageLen-=blk.recsize;
   NextPtr = BufferPtr+sizeof(anim_frame)+blk.recsize;
	return(0);
}



//---------------------------------------------------------------------------
// MOVIE_HandlePage() - This handles the current page of data from the
//								ram buffer...
//
// PURPOSE: Process current Page of anim.
//
//
// RETURNS:
//
//---------------------------------------------------------------------------
void MOVIE_HandlePage(MovieStuff_t *MovieStuff)
{
	anim_frame blk;
	char *frame;
   Uint16 wait_time;

	memcpy(&blk,BufferPtr,sizeof(anim_frame));
	BufferPtr+=sizeof(anim_frame);
   frame = BufferPtr;

	IN_ReadControl(0,&ci);

   switch (blk.code)
   {

      //-------------------------------------------
      //
      //
      //-------------------------------------------

	 	case AN_SOUND:				// Sound Chunk
		{
      	Uint16 sound_chunk;
         sound_chunk = *(Uint16 *)frame;
      	SD_PlaySound(sound_chunk);
         BufferPtr+=blk.recsize;
      }
      break;


      //-------------------------------------------
      //
      //
      //-------------------------------------------

#if 0
		case MV_CNVT_CODE('P','M'):				// Play Music
		{
      	unsigned song_chunk;
         song_chunk = *(unsigned *)frame;
         SD_MusicOff();

			if (!audiosegs[STARTMUSIC+musicchunk])
			{
//				MM_BombOnError(false);
				CA_CacheAudioChunk(STARTMUSIC + musicchunk);
//				MM_BombOnError(true);
			}

			if (mmerror)
				mmerror = false;
			else
			{
				MM_SetLock(&((memptr)audiosegs[STARTMUSIC + musicchunk]),true);
				SD_StartMusic((MusicGroup *)audiosegs[STARTMUSIC + musicchunk]);
			}

         BufferPtr+=blk.recsize;
      }
      break;
#endif


      //-------------------------------------------
      //
      //
      //-------------------------------------------

	 	case AN_FADE_IN_FRAME:				// Fade In Page
        	VL_FadeIn(0,255,(Uint8*)movie_palette,30);
			fade_flags = FADE_NONE;
         EverFaded = true;
			screenfaded = false;
      break;



      //-------------------------------------------
      //
      //
      //-------------------------------------------

	 	case AN_FADE_OUT_FRAME:				// Fade Out Page
			VW_FadeOut();
			screenfaded = true;
			fade_flags = FADE_NONE;
      break;


      //-------------------------------------------
      //
      //
      //-------------------------------------------

	 	case AN_PAUSE:				// Pause
		{
      	Uint16 vbls;
         vbls = *(Uint16 *)frame;
			IN_UserInput(vbls);
         BufferPtr+=blk.recsize;

         // BBi
         // FIXME Clear entire input state.
         LastScan = 0;
         ci.button0 = 0;
         ci.button1 = 0;
         // BBi
      }
      break;



      //-------------------------------------------
      //
      //
      //-------------------------------------------

   	case AN_PAGE:				// Graphics Chunk
#if 1
         if (movie_flag == MV_FILL)
         {
            // First page comming in.. Fill screen with fill color...
            //
//            movie_flag = MV_READ;	// Set READ flag to skip the first frame on an anim repeat
            movie_flag = MV_NONE;	// Set READ flag to skip the first frame on an anim repeat
			  	JM_VGALinearFill(screenloc[0],3*80*208,*frame);
            frame++;
         }
         else
#endif 
				VL_LatchToScreen(displayofs+ylookup[MovieStuff->start_line], 320>>2, MovieStuff->end_line-MovieStuff->start_line, 0, MovieStuff->start_line);

         MOVIE_ShowFrame(frame);

#if 0  
         if (movie_flag == MV_READ)
         {
         	seek_pos = tell(Movie_FHandle);
            movie_flag = MV_NONE;
         }
#endif 
   		FlipPages();

         if (TimeCount < MovieStuff->ticdelay)
         {
	         wait_time = MovieStuff->ticdelay - TimeCount;
				VL_WaitVBL(wait_time);
         }
         else
				VL_WaitVBL(1);

			TimeCount = 0;

			if ((!screenfaded) && (ci.button0 || ci.button1 || LastScan))
			{
				ExitMovie = true;
				if (EverFaded)					// This needs to be a passed flag...
				{
					VW_FadeOut();
					screenfaded = true;
				}
			}
		break;


#if 0
      //-------------------------------------------
      //
      //
      //-------------------------------------------

		case AN_PRELOAD_BEGIN:			// These are NOT handled YET!
		case AN_PRELOAD_END:
		break;

#endif
      //-------------------------------------------
      //
      //
      //-------------------------------------------

		case AN_END_OF_ANIM:
			ExitMovie = true;
		break;


      //-------------------------------------------
      //
      //
      //-------------------------------------------

		default:
			AN_ERROR(HANDLEPAGE_BAD_CODE);
		break;
   }
}


//---------------------------------------------------------------------------
// MOVIE_Play() - Playes an Animation
//
// RETURNS: true  - Movie File was found and "played"
//				false - Movie file was NOT found!
//---------------------------------------------------------------------------
boolean MOVIE_Play(MovieStuff_t *MovieStuff)
{
	// Init our Movie Stuff...
   //

   SetupMovie(MovieStuff);

   // Start the anim process
   //

   if ((Movie_FHandle = open(MovieStuff->FName, O_RDONLY|O_BINARY, S_IREAD)) == -1)	  
     	return(false);

   while (movie_reps && (!ExitMovie))
	{
#if 0 	
      if (movie_flag == MV_SKIP)
	   	if (lseek(Movie_FHandle, seek_pos, SEEK_SET) == -1)
         	return(false);
#endif
	   for (;!ExitMovie;)
   	{
      	if (MOVIE_GetFrame(Movie_FHandle))
         	break;

         MOVIE_HandlePage(MovieStuff);
      }

      movie_reps--;
      movie_flag = MV_SKIP;
   }

   ShutdownMovie();

   return(true);
}




//--------------------------------------------------------------------------
// FlipPages()
//---------------------------------------------------------------------------
void FlipPages(void)
{

// FIXME
#if 0
#ifndef DRAW_TO_FRONT

	displayofs = bufferofs;

	asm	cli
	asm	mov	cx,[displayofs]
	asm	mov	dx,3d4h		// CRTC address register
	asm	mov	al,0ch		// start address high register
	asm	out	dx,al
	asm	inc	dx
	asm	mov	al,ch
	asm	out	dx,al   	// set the high byte
#if 0
	asm	dec	dx
	asm	mov	al,0dh		// start address low register
	asm	out	dx,al
	asm	inc	dx
	asm	mov	al,cl
	asm	out	dx,al		// set the low byte
#endif
	asm	sti
#endif // 0

	bufferofs += SCREENSIZE;
	if (bufferofs > PAGE3START)
		bufferofs = PAGE1START;

#endif

    displayofs = bufferofs;

    VL_RefreshScreen();

    bufferofs += SCREENSIZE;

    if (bufferofs > PAGE3START)
        bufferofs = PAGE1START;
}