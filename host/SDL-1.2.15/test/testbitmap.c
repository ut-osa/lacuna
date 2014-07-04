
/* Simple program:  Test bitmap blits */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"
#include "picture.xbm"

int MODULO;
/* Call this instead of exit(), so we can clean up SDL: atexit() is evil. */
static void quit(int rc)
{
	SDL_Quit();
	exit(rc);
}

SDL_Surface *LoadXBM(SDL_Surface *screen, int w, int h, Uint8 *bits)
{
	SDL_Surface *bitmap;
	Uint8 *line;

	/* Allocate the bitmap */
	bitmap = SDL_CreateRGBSurface(SDL_SWSURFACE, w, h, 1, 0, 0, 0, 0);
	if ( bitmap == NULL ) {
		fprintf(stderr, "Couldn't allocate bitmap: %s\n",
						SDL_GetError());
		return(NULL);
	}
	/* Copy the pixels */
	line = (Uint8 *)bitmap->pixels;
	w = (w+7)/8;
	while ( h-- ) {
		memcpy(line, bits, w);
		/* X11 Bitmap images have the bits reversed */
/*		{ int i, j; Uint8 *buf, byte;
			for ( buf=line, i=0; i<w; ++i, ++buf ) {
				byte = *buf;
				*buf = 0;
				for ( j=7; j>=0; --j ) {
					*buf |= (byte&0x01)<<j;
					byte >>= 1;
				}
			}
		}
*/		line += bitmap->pitch;
		bits += w;
	}
	return(bitmap);
}

int main(int argc, char *argv[])
{
	SDL_Surface *screen;
	SDL_Surface *bitmap;
	Uint8  video_bpp;
	Uint32 videoflags;
	Uint8 *buffer;
	int i, k, done;
	SDL_Event event;
	Uint16 *buffer16;
        Uint16 color;
        Uint8  gradient;
	SDL_Color palette[256];

	MODULO=252;
	if (argc!=1 ){ MODULO=atoi(argv[1]); }
	printf("modulo=%d \n",MODULO);
	/* Initialize SDL */
	if ( SDL_Init(SDL_INIT_VIDEO) < 0 ) {
		fprintf(stderr, "Couldn't initialize SDL: %s\n",SDL_GetError());
		return(1);
	}

	video_bpp = 0;
	videoflags = SDL_SWSURFACE;
	while ( argc > 1 ) {
		--argc;
		if ( strcmp(argv[argc-1], "-bpp") == 0 ) {
			video_bpp = atoi(argv[argc]);
			--argc;
		} else
		if ( strcmp(argv[argc], "-warp") == 0 ) {
			videoflags |= SDL_HWPALETTE;
		} else
		if ( strcmp(argv[argc], "-hw") == 0 ) {
			videoflags |= SDL_HWSURFACE;
		} else
		if ( strcmp(argv[argc], "-fullscreen") == 0 ) {
			videoflags |= SDL_FULLSCREEN;
		} else {
/*			fprintf(stderr,
			"Usage: %s [-bpp N] [-warp] [-hw] [-fullscreen]\n",
								argv[0]);
			quit(1);
*/		}
	}

	/* Set 640x480 video mode */
	if ( (screen=SDL_SetVideoMode(640,480,video_bpp,videoflags)) == NULL ) {
		fprintf(stderr, "Couldn't set 640x480x%d video mode: %s\n",
						video_bpp, SDL_GetError());
		quit(2);
	}

	if (video_bpp==8) {
		/* Set a gray colormap, reverse order from white to black */
		for ( i=0; i<256; ++i ) {
			palette[i].r = 255-i;
			palette[i].g = 255-i;
			palette[i].b = 255-i;
		}
		SDL_SetColors(screen, palette, 0, 256);
	}

	/* Set the surface pixels and refresh! */
	if ( SDL_LockSurface(screen) < 0 ) {
		fprintf(stderr, "Couldn't lock the display surface: %s\n",
							SDL_GetError());
		quit(2);
	}
	buffer=(Uint8 *)screen->pixels;
	int buffer_size=(screen->h * screen->pitch);
	int tilenum=0;
	int tilecounter=0;
	if (screen->format->BytesPerPixel!=2) {
        	for ( i=0; i<(buffer_size);  ) {

	        	buffer[i]=(MODULO-1)-tilecounter%MODULO;

			if (tilecounter==MODULO-1) {
				i++; 
				if (i<buffer_size-4) {
					*((int*)&buffer[i])=tilenum++;
				}
				i+=3;
			}
			tilecounter++; tilecounter%=MODULO;
			i++;

        	}
        }
	SDL_UnlockSurface(screen);
	SDL_UpdateRect(screen, 0, 0, 0, 0);
	buffer=(Uint8 *)screen->pixels;
	if (screen->format->BytesPerPixel!=2) {
        	for ( i=0; i<(screen->h * screen->pitch); ++i ) {
	        	buffer[i]=0;
        	}
        }

	/* Load the bitmap */
	bitmap = LoadXBM(screen, picture_width, picture_height,
					(Uint8 *)picture_bits);
	if ( bitmap == NULL ) {
		quit(1);
	}

	/* Wait for a keystroke */
	done = 0;
	while ( !done ) {
		/* Check for events */
		while ( SDL_PollEvent(&event) ) {
			switch (event.type) {
				case SDL_MOUSEBUTTONDOWN: 
/*
					SDL_Rect dst;

					dst.x = event.button.x - bitmap->w/2;
					dst.y = event.button.y - bitmap->h/2;
					dst.w = bitmap->w;
					dst.h = bitmap->h;
					SDL_BlitSurface(bitmap, NULL,
								screen, &dst);
					SDL_UpdateRects(screen,1,&dst);
					}
*/
					break;
				case SDL_KEYDOWN:

	/* Set the surface pixels and refresh! */
/*	if ( SDL_LockSurface(screen) < 0 ) {
		fprintf(stderr, "Couldn't lock the display surface: %s\n",
							SDL_GetError());
		quit(2);
	}
	buffer=(Uint8 *)screen->pixels;
	if (screen->format->BytesPerPixel!=2) {
        	for ( i=0; i<screen->h; ++i ) {
			int z=0;
			for(z=0;z<screen->pitch;z++)
			{
	        		buffer[screen->pitch-z-1]=(Uint8)z;
			}
	        	buffer += screen->pitch;
        	}
        }
	SDL_UnlockSurface(screen);
	SDL_UpdateRect(screen, 0, 0, 0, 0);
*/					/* Any key press quits the app... */
					done = 1;
					break;
				case SDL_QUIT:
					done = 1;
					break;
				default:
					break;
			}
		}
	}
	SDL_FreeSurface(bitmap);
	SDL_Quit();
	return(0);
}
