#ifdef _HARMATTAN_PLUS

#include "../idlib/precompiled.h"
#include "../sys/posix/posix_public.h"
#include "../sys/linux/local.h"

#include <pthread.h>

#include "gl_vkb.h"

idCVar in_mouse( "in_mouse", "1", CVAR_SYSTEM | CVAR_ARCHIVE, "" );
idCVar in_dgamouse( "in_dgamouse", "1", CVAR_SYSTEM | CVAR_ARCHIVE, "" );
idCVar in_nograb( "in_nograb", "0", CVAR_SYSTEM | CVAR_NOCHEAT, "" );

#ifdef _HARMATTAN_PLUS
idCVar h_cUsingVKB( "h_cUsingVKB", "1", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_BOOL, "[harmattan] - Rendering virtual buttons layer and response events: 0 / 1" );
idCVar h_cVKBAlpha( "h_cVKBAlpha", "1.0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "[harmattan] - Virtual buttons layer alpha scale: float (0.0, 1.0]" );
idCVar h_cSwipeSens( "h_cSwipeSens", "1.0", CVAR_SYSTEM | CVAR_ARCHIVE | CVAR_FLOAT, "[harmattan] - Swipe sensitivity on virtual buttons layer: float." );
#endif

// toggled by grab calls - decides if we ignore MotionNotify events
static bool mouse_active = false;

// non-DGA pointer-warping mouse input
static int mwx, mwy;
static int mx = 0, my = 0;

// time mouse was last reset, we ignore the first 50ms of the mouse to allow settling of events
static int mouse_reset_time = 0;
#define MOUSE_RESET_DELAY 50

// backup original values for pointer grab/ungrab
static int mouse_accel_numerator;
static int mouse_accel_denominator;
static int mouse_threshold;

/*
=================
glBegin(karin_FUNC)
=================
*/
static int karinSwipeSens(int x)
{
#define FLOAT_TO_INT
	float sens = h_cSwipeSens.GetFloat();
	return (int)FLOAT_TO_INT((float)x * sens);
#undef FLOAT_TO_INT
}

unsigned karinHandleVKBAction(int action, unsigned pressed, int dx, int dy)
{
#define		MAXCMDLINE	256
#define		MAXCMDLENGTH 1024
	static int _keys[MAXCMDLINE];
	static char _cmd[MAXCMDLENGTH];
	unsigned int key_count = 0;

	int r = karinGetActionData(action, _keys, MAXCMDLINE, &key_count, _cmd, MAXCMDLENGTH);
	if(r == Cmd_Data)
	{
		return karinAddActionPollEvent(_keys[0], pressed != 0);
	}
	else if(r == Key_Data)
	{
		int i = 0;
		for(i = 0; i < key_count; i++)
		{
			Posix_QueEvent(SE_KEY, _keys[i], pressed != 0, 0, NULL);
		}
		return 1;
	}
	else if(r == Char_Data)
	{
		if(!pressed)
		{
			int i = 0;
			for(i = 0; i < key_count; i++)
			{
				{
					Posix_QueEvent(SE_CHAR, _keys[i], 0, 0, NULL);
				}
			}
		}
		return 1;
	}
	else if(r == Button_Data)
	{
		if(pressed)
		{
			int x = 0;
			//Posix_QueEvent(SE_MOUSE, dx, -dy, 0, NULL);
			if(dx != 0)
			{
				x = karinSwipeSens(dx);
				if(x != 0)
					karinAddTouchPollEvent(M_DELTAX, x);
			}
			if(dy != 0)
			{
				x = karinSwipeSens(-dy);
				if(dy != 0)
					karinAddTouchPollEvent(M_DELTAY, x);
			}
		}
	}

	return 0;
#undef MAXCMDLINE
#undef MAXCMDLENGTH
}

/*
=================
glEnd(karin_FUNC)
=================
*/

/*
=================
IN_Clear_f
=================
*/
void IN_Clear_f( const idCmdArgs &args ) {
	idKeyInput::ClearStates();
}

/*
=================
Sys_InitInput
=================
*/
void Sys_InitInput(void) {
	common->Printf( "\n------- Input Initialization -------\n" );
	assert( dpy );
	cmdSystem->AddCommand( "in_clear", IN_Clear_f, CMD_FL_SYSTEM, "reset the input keys" );
	common->Printf( "------------------------------------\n" );
}

//#define XEVT_DBG
//#define XEVT_DBG2

static void Sys_XInstallGrabs( void ) {
	assert( dpy );

	mouse_reset_time = Sys_Milliseconds ();
	
	in_dgamouse.SetBool( false ); // DGA not support
	
	if ( in_dgamouse.GetBool() ) {
	} else {
		mwx = glConfig.vidWidth / 2;
		mwy = glConfig.vidHeight / 2;
		mx = my = 0;
	}
	
	mouse_active = true;
}

void Sys_XUninstallGrabs(void) {
	assert( dpy );

#if defined( ID_ENABLE_DGA )
	if ( in_dgamouse.GetBool() ) {
	}
#endif
	
	mouse_active = false;
}

void Sys_GrabMouseCursor( bool grabIt ) {

#if defined( ID_DEDICATED )
	return;
#endif

	if ( !dpy ) {
		#ifdef XEVT_DBG
			common->DPrintf("Sys_GrabMouseCursor: !dpy\n");
		#endif
		return;
	}
	
	if ( glConfig.isFullscreen ) {
		if ( !grabIt ) {
			return; // never ungrab while fullscreen
		}
		if ( in_nograb.GetBool() ) {
			common->DPrintf("forcing in_nograb 0 while running fullscreen\n");
			in_nograb.SetBool( false );
		}
	}
	
	if ( in_nograb.GetBool() ) {
		if ( in_dgamouse.GetBool() ) {
			common->DPrintf("in_nograb 1, forcing forcing DGA mouse off\n");
			in_dgamouse.SetBool( false );
		}
		if (grabIt) {
			mouse_active = true;
		} else {
			mouse_active = false;
		}
		return;
	}

	if ( grabIt && !mouse_active ) {
		Sys_XInstallGrabs();
	} else if ( !grabIt && mouse_active ) {
		Sys_XUninstallGrabs();
	}
}

/*
==========================
Posix_PollInput
==========================
*/
void Posix_PollInput() {
	if(!dpy)
		return;
	wl_display_dispatch_pending(dpy);
#if 0
	static char buf[16];
	static XEvent event;
	static XKeyEvent *key_event = (XKeyEvent*)&event;
  	int lookupRet;
	int b, dx, dy;
	KeySym keysym;	
	
	if ( !dpy ) {
		return;
	}
	
	// NOTE: Sys_GetEvent only calls when there are no events left
	// but here we pump all X events that have accumulated
	// pump one by one? or use threaded input?
#ifdef _HARMATTAN_PLUS
	while ( X11_Pending( dpy ) ) 
#else
	while ( XPending( dpy ) ) 
#endif
	{
		XNextEvent( dpy, &event );
		switch (event.type) {
			case KeyPress:
				#ifdef XEVT_DBG
				if (key_event->keycode > 0x7F)
					common->DPrintf("WARNING: KeyPress keycode > 0x7F");
				#endif
					//printf("SE_KEY press %c  0x%X ox%X | %d %d\n", key_event->keycode, key_event->keycode, key_event->keycode & 0x7F, key_event->keycode, key_event->keycode & 0x7F);
				key_event->keycode &= 0x7F;
				#ifdef XEVT_DBG2
					printf("SE_KEY press %d\n", key_event->keycode);
				#endif
				Posix_QueEvent( SE_KEY, s_scantokey[key_event->keycode], true, 0, NULL);
				lookupRet = XLookupString(key_event, buf, sizeof(buf), &keysym, NULL);
				if (lookupRet > 0) {
					char s = buf[0];
					#ifdef XEVT_DBG
						if (buf[1]!=0)
							common->DPrintf("WARNING: got XLookupString buffer '%s' (%d)\n", buf, strlen(buf));
					#endif
					#ifdef XEVT_DBG2
						printf("SE_CHAR %s\n", buf);
					#endif
					Posix_QueEvent( SE_CHAR, s, 0, 0, NULL);
				}
				if (!Posix_AddKeyboardPollEvent( s_scantokey[key_event->keycode], true ))
					return;
			break;			
				
			case KeyRelease:
				if (Sys_XRepeatPress(&event)) {
					#ifdef XEVT_DBG2
						printf("RepeatPress\n");
					#endif
					continue;
				}
				#ifdef XEVT_DBG
				if (key_event->keycode > 0x7F)
					common->DPrintf("WARNING: KeyRelease keycode > 0x7F");
				#endif
				key_event->keycode &= 0x7F;
				#ifdef XEVT_DBG2
					printf("SE_KEY release %d\n", key_event->keycode);
				#endif
				Posix_QueEvent( SE_KEY, s_scantokey[key_event->keycode], false, 0, NULL);
				if (!Posix_AddKeyboardPollEvent( s_scantokey[key_event->keycode], false ))
					return;
			break;
				
#ifndef _HARMATTAN_PLUS
			case ButtonPress:
				if (event.xbutton.button == 4) {
					Posix_QueEvent( SE_KEY, K_MWHEELUP, true, 0, NULL);
					if (!Posix_AddMousePollEvent( M_DELTAZ, 1 ))
						return;
				} else if (event.xbutton.button == 5) {
					Posix_QueEvent( SE_KEY, K_MWHEELDOWN, true, 0, NULL);
					if (!Posix_AddMousePollEvent( M_DELTAZ, -1 ))
						return;
				} else {
#ifndef _HARMATTAN_PLUS
					b = -1;
					if (event.xbutton.button == 1) {
						b = 0;		// K_MOUSE1
					} else if (event.xbutton.button == 2) {
						b = 2;		// K_MOUSE3
					} else if (event.xbutton.button == 3) {
						b = 1;		// K_MOUSE2
					} else if (event.xbutton.button == 6) {
						b = 3;		// K_MOUSE4
					} else if (event.xbutton.button == 7) {
						b = 4;		// K_MOUSE5
					}
					if (b == -1 || b > 4) {
						common->DPrintf("X ButtonPress %d not supported\n", event.xbutton.button);
					} else {
						Posix_QueEvent( SE_KEY, K_MOUSE1 + b, true, 0, NULL);
						if (!Posix_AddMousePollEvent( M_ACTION1 + b, true ))
							return;
					}
#endif
				}
			break;

			case ButtonRelease:
				if (event.xbutton.button == 4) {
					Posix_QueEvent( SE_KEY, K_MWHEELUP, false, 0, NULL);
				} else if (event.xbutton.button == 5) {
					Posix_QueEvent( SE_KEY, K_MWHEELDOWN, false, 0, NULL);
				} else {
#ifndef _HARMATTAN_PLUS
					b = -1;
					if (event.xbutton.button == 1) {
						b = 0;
					} else if (event.xbutton.button == 2) {
						b = 2;
					} else if (event.xbutton.button == 3) {
						b = 1;
					} else if (event.xbutton.button == 6) {
						b = 3;		// K_MOUSE4
					} else if (event.xbutton.button == 7) {
						b = 4;		// K_MOUSE5
					}
					if (b == -1 || b > 4) {
						common->DPrintf("X ButtonRelease %d not supported\n", event.xbutton.button);
					} else {
						Posix_QueEvent( SE_KEY, K_MOUSE1 + b, false, 0, NULL);
						if (!Posix_AddMousePollEvent( M_ACTION1 + b, false ))
							return;
					}
#endif
				}
			break;
			
			case MotionNotify:
				if (!mouse_active)
					break;
				if (in_dgamouse.GetBool()) {
					dx = event.xmotion.x_root;
					dy = event.xmotion.y_root;

					Posix_QueEvent( SE_MOUSE, dx, dy, 0, NULL);

					// if we overflow here, we'll get a warning, but the delta will be completely processed anyway
					Posix_AddMousePollEvent( M_DELTAX, dx );
					if (!Posix_AddMousePollEvent( M_DELTAY, dy ))
						return;
				} else {
					// if it's a center motion, we've just returned from our warp
					// FIXME: we generate mouse delta on wrap return, but that lags us quite a bit from the initial event..
					if (event.xmotion.x == glConfig.vidWidth / 2 &&
						event.xmotion.y == glConfig.vidHeight / 2) {
						mwx = glConfig.vidWidth / 2;
						mwy = glConfig.vidHeight / 2;

						Posix_QueEvent( SE_MOUSE, mx, my, 0, NULL);

						Posix_AddMousePollEvent( M_DELTAX, mx );
						if (!Posix_AddMousePollEvent( M_DELTAY, my ))
							return;
						mx = my = 0;
						break;
					}

					dx = ((int) event.xmotion.x - mwx);
					dy = ((int) event.xmotion.y - mwy);
					mx += dx;
					my += dy;

					mwx = event.xmotion.x;
					mwy = event.xmotion.y;
				    XWarpPointer(dpy,None,win,0,0,0,0, (glConfig.vidWidth/2),(glConfig.vidHeight/2));
				}
			break;
#endif

#ifdef _HARMATTAN_PLUS
				case GenericEvent:
					karinXI2Event(&event);
					/*
					else
						printf("Unhandle XGenericEvent\n");
						*/
					break;
#endif

		}
	}
#endif
}

/*
=================
Sys_ShutdownInput
=================
*/
void Sys_ShutdownInput( void ) { }

//#include "vkb.cpp" // not add to scons main script.

unsigned char Sys_MapCharForKey( int _key ) {
	return (unsigned char)_key;
}

#endif
