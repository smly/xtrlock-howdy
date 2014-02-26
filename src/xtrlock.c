/*
 * xtrlock.c
 *
 * X Transparent Lock
 *
 * Copyright (C)1993,1994 Ian Jackson
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xos.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>

#include <security/pam_appl.h>
#include <security/pam_misc.h>

static struct pam_conv conv = {
    misc_conv,
    NULL
};

#include "config.h"

#include "lock.bitmap"
#include "mask.bitmap"

Display *display;
Window window, root;

#define TIMEOUTPERATTEMPT 30000
#define MAXGOODWILL  (TIMEOUTPERATTEMPT*5)
#define INITIALGOODWILL MAXGOODWILL
#define GOODWILLPORTION 0.3
enum { MNONE, MBG, MBLANK };

char *pam_domain = "system-local-login";

int passwordok(char *password)
{
    return auth_pam(getenv("USER"), password);
}

void
lock(int mode)
{
    XEvent ev;
    KeySym ks;
    char cbuf[10], rbuf[128];
    int clen, rlen=0;
    long goodwill= INITIALGOODWILL, timeout= 0;
    XSetWindowAttributes attrib;
    unsigned long values;
    Cursor cursor;
    Pixmap csr_source,csr_mask;
    XColor csr_fg, csr_bg, dummy;
    int ret;

    display = XOpenDisplay(0);
    if (display == NULL)
    {
        fprintf(stderr,"xtrlock (version %s): cannot open display\n",
	    PROJECT_VERSION);
        exit(1);
    }

    attrib.override_redirect= True;
    values = CWOverrideRedirect;
    if (mode == MNONE)
    {
        window= XCreateWindow(display,DefaultRootWindow(display),
            0,0,1,1,0,CopyFromParent,InputOnly,CopyFromParent,
            values,&attrib);
    }
    else
    {
        int screen;

        screen = DefaultScreen(display);

        if (mode == MBG)
        {
            Atom a_XROOTPMAP_ID;
            Atom ret_type;
            int  act_format;
            unsigned long  nitems, bytes_after ;
            unsigned char *prop = NULL;

            a_XROOTPMAP_ID = XInternAtom(display, "_XROOTPMAP_ID", True);
            if (XGetWindowProperty(display, DefaultRootWindow(display),
                    a_XROOTPMAP_ID, 0, 1, False, XA_PIXMAP,
                    &ret_type, &act_format, &nitems, &bytes_after, &prop)
                == Success)
            {
              
                if (ret_type == XA_PIXMAP)
                {
                    attrib.background_pixmap = *((Pixmap *)prop); 
                    values |= CWBackPixmap;            
                }
                XFree(prop);           
            } 
        }
        if (!(values & CWBackPixmap))
        {
            attrib.background_pixel = BlackPixel(display, screen);
            values |= CWBackPixel;
        }
        window = XCreateWindow(display,DefaultRootWindow(display),
            0, 0, DisplayWidth(display, screen),
            DisplayHeight(display, screen), 0, 
            CopyFromParent, CopyFromParent, CopyFromParent, values, &attrib);
        XClearWindow(display, window);
    }
    XSelectInput(display,window,KeyPressMask|KeyReleaseMask);

    csr_source = XCreateBitmapFromData(display, window, lock_bits,
        lock_width, lock_height);
    csr_mask = XCreateBitmapFromData(display, window, mask_bits, mask_width,
        mask_height);

    ret = XAllocNamedColor(display,
        DefaultColormap(display, DefaultScreen(display)),
        "steelblue3",
        &dummy, &csr_bg);
    if (ret == 0)
        XAllocNamedColor(display,
            DefaultColormap(display, DefaultScreen(display)),
            "black",
            &dummy, &csr_bg);

    ret = XAllocNamedColor(display,
        DefaultColormap(display,DefaultScreen(display)),
        "grey25",
        &dummy, &csr_fg);
    if (ret == 0)
        XAllocNamedColor(display,
            DefaultColormap(display, DefaultScreen(display)),
            "white",
            &dummy, &csr_bg);

    cursor= XCreatePixmapCursor(display,csr_source,csr_mask,&csr_fg,&csr_bg,
        lock_x_hot,lock_y_hot);

    XMapWindow(display,window);
    if (XGrabKeyboard(display,window,False,GrabModeAsync,GrabModeAsync,
            CurrentTime)!=GrabSuccess)
    {
        exit(1);
    }
    if (XGrabPointer(display,window,False,(KeyPressMask|KeyReleaseMask)&0,
            GrabModeAsync,GrabModeAsync,None,
            cursor,CurrentTime)!=GrabSuccess)
    {
        XUngrabKeyboard(display,CurrentTime);
        exit(1);
    }

    for (;;)
    {
        XNextEvent(display,&ev);
        switch (ev.type)
        {
        case KeyPress:
            if (ev.xkey.time < timeout)
            {
                XBell(display,0);
                break;
            }
            clen= XLookupString(&ev.xkey,cbuf,9,&ks,0);
            switch (ks)
            {
            case XK_Escape: case XK_Clear:
                rlen=0; break;
            case XK_Delete: case XK_BackSpace:
                if (rlen>0)
                    rlen--;
                break;
            case XK_Linefeed: case XK_Return:
                if (rlen==0)
                    break;
                rbuf[rlen]=0;
                if (passwordok(rbuf))
                    goto loop_x;
                XBell(display,0);
                rlen= 0;
                if (timeout)
                {
                    goodwill+= ev.xkey.time - timeout;
                    if (goodwill > MAXGOODWILL)
                    {
                        goodwill= MAXGOODWILL;
                    }
                }
                timeout = -goodwill*GOODWILLPORTION;
                goodwill += timeout;
                timeout += ev.xkey.time + TIMEOUTPERATTEMPT;
                break;
            default:
                if (clen != 1)
                    break;
                /* allow space for the trailing \0 */
                if (rlen < (sizeof(rbuf) - 1))
                {
                    rbuf[rlen]=cbuf[0];
                    rlen++;
                }
                break;
            }
            break;
        default:
            break;
        }
    }
loop_x:
    exit(0);
}

int main(int argc, char *argv[])
{
    int mode = MBG;
    if (argc == 2)
    {
        if (!strcmp(argv[1], "bg"))
            mode = MBG;
        else if (!strcmp(argv[1], "blank"))
            mode = MBLANK;
        if (!strcmp(argv[1], "none"))
            mode = MNONE;
    }
    lock(mode);
    return 0;
}