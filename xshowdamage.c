/*
 * Copyright © 2005 Novell, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Novell, Inc. not be used in advertising or publicity pertaining to
 * distribution of the software without specific, written prior permission.
 * Novell, Inc. makes no representations about the suitability of this
 * software for any purpose. It is provided "as is" without express or
 * implied warranty.
 *
 * NOVELL, INC. DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN
 * NO EVENT SHALL NOVELL, INC. BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 * NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: David Reveman <davidr@novell.com>
 */

/*
 * Idea by Eric Anholt
 */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xproto.h>

#include <X11/extensions/shape.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xdamage.h>

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/poll.h>
#include <limits.h>

#define FADEOUT_SLEEP 40

typedef struct _DamageBox {
    struct _DamageBox *next;

    int		 x1, y1, x2, y2;
    unsigned int alpha;
} DamageBox;

static Display		 *dpy;
static Window		 win;
static GC		 gc;
static XRenderPictFormat *pictureFormat;
static DamageBox	 *boxes = NULL;

static void
redrawBoxes (void)
{
    Pixmap	 pixmap;
    Picture	 picture;
    DamageBox	 *box;
    int		 x1, y1, x2, y2;
    XRenderColor color;

    x1 = SHRT_MAX;
    y1 = SHRT_MAX;
    x2 = SHRT_MIN;
    y2 = SHRT_MIN;

    for (box = boxes; box; box = box->next)
    {
	if (box->x1 < x1)
	    x1 = box->x1;
	if (box->y1 < y1)
	    y1 = box->y1;
	if (box->x2 > x2)
	    x2 = box->x2;
	if (box->y2 > y2)
	    y2 = box->y2;
    }

    if (x2 < x1 || y2 < y1)
	return;

    pixmap  = XCreatePixmap (dpy, win, x2 - x1, y2 - y1, 32);
    picture = XRenderCreatePicture (dpy, pixmap, pictureFormat, 0, NULL);

    color.red   = 0x0000;
    color.green = 0x0000;
    color.blue  = 0x0000;
    color.alpha = 0x0000;

    XRenderFillRectangle (dpy,
			  PictOpSrc,
			  picture,
			  &color,
			  0, 0, x2 - x1, y2 - y1);

    for (box = boxes; box; box = box->next)
    {
	color.red   = box->alpha;
	color.alpha = box->alpha;

	XRenderFillRectangle (dpy,
			      PictOpOver,
			      picture,
			      &color,
			      box->x1 - x1, box->y1 - y1,
			      box->x2 - box->x1,
			      box->y2 - box->y1);
    }

    XCopyArea (dpy, pixmap, win, gc, 0, 0, x2 - x1, y2 - y1, x1, y1);

    XRenderFreePicture (dpy, picture);
    XFreePixmap (dpy, pixmap);
}

static Visual *
findArgbVisual (Display *dpy, int scr)
{
    XVisualInfo		*xvi;
    XVisualInfo		template;
    int			nvi;
    int			i;
    XRenderPictFormat	*format;
    Visual		*visual;

    template.screen = scr;
    template.depth  = 32;
    template.class  = TrueColor;

    xvi = XGetVisualInfo (dpy,
			  VisualScreenMask |
			  VisualDepthMask  |
			  VisualClassMask,
			  &template,
			  &nvi);
    if (!xvi)
	return 0;

    visual = 0;
    for (i = 0; i < nvi; i++)
    {
	format = XRenderFindVisualFormat (dpy, xvi[i].visual);
	if (format->type == PictTypeDirect && format->direct.alphaMask)
	{
	    visual = xvi[i].visual;
	    break;
	}
    }

    XFree (xvi);

    return visual;
}

int
main (int argc, char **argv)
{
    Window		 root;
    int			 screen;
    XSizeHints		 xsh;
    XWMHints		 xwmh;
    Atom		 state[256];
    int			 nState = 0;
    XSetWindowAttributes attr;
    Visual		 *visual;
    Region		 region;
    int			 damageError, damageEvent;
    XEvent		 event;
    struct pollfd	 ufd;
    Window		 watchWindow;
    Damage		 damage;
    int			 redraw = 0;
    XGCValues		 gcv;

    if (argc < 2)
    {
	fprintf (stderr, "Usage: %s WINDOW\n", argv[0]);
	return 1;
    }

    watchWindow = strtol (argv[1], NULL, 0);
    fprintf (stderr, "0x%x\n", (int) watchWindow);

    dpy = XOpenDisplay (NULL);
    if (!dpy)
    {
	fprintf (stderr, "%s: Error: couldn't open display\n", argv[0]);
	return 1;
    }

    ufd.fd     = ConnectionNumber (dpy);
    ufd.events = POLLIN;

    if (!XDamageQueryExtension (dpy, &damageEvent, &damageError))
    {
	fprintf (stderr, "%s: No damage extension\n", argv[0]);
	return 1;
    }

    damage = XDamageCreate (dpy, watchWindow, XDamageReportRawRectangles);

    state[nState++] = XInternAtom (dpy, "_NET_WM_STATE_FULLSCREEN", 0);
    state[nState++] = XInternAtom (dpy, "_NET_WM_STATE_ABOVE", 0);

    screen = DefaultScreen (dpy);
    root   = RootWindow (dpy, screen);

    xsh.flags  = PSize | PPosition;
    xsh.width  = DisplayWidth (dpy, screen);
    xsh.height = DisplayHeight (dpy, screen);

    xwmh.flags = InputHint;
    xwmh.input = 0;

    visual = findArgbVisual (dpy, screen);
    if (!visual)
    {
	fprintf (stderr, "%s: Error: couldn't find argb visual\n", argv[0]);
	return 1;
    }

    pictureFormat = XRenderFindStandardFormat (dpy, PictStandardARGB32);

    attr.background_pixel = 0;
    attr.border_pixel     = 0;
    attr.colormap	  = XCreateColormap (dpy, root, visual, AllocNone);

    win = XCreateWindow (dpy, root, 0, 0, xsh.width, xsh.height, 0,
			 32, InputOutput, visual,
			 CWBackPixel | CWBorderPixel | CWColormap, &attr);

    XSetWMProperties (dpy, win, NULL, NULL, argv, argc, &xsh, &xwmh, NULL);

    region = XCreateRegion ();
    if (region)
    {
	XShapeCombineRegion (dpy, win, ShapeInput, 0, 0, region, ShapeSet);
	XDestroyRegion (region);
    }

    XChangeProperty (dpy, win, XInternAtom (dpy, "_NET_WM_STATE", 0),
		     XA_ATOM, 32, PropModeReplace,
		     (unsigned char *) state, nState);

    XMapWindow (dpy, win);

    gcv.graphics_exposures = False;
    gc = XCreateGC (dpy, win, GCGraphicsExposures, &gcv);

    for (;;)
    {
	while (XPending (dpy))
	{
	    XNextEvent (dpy, &event);

	    switch (event.type) {
	    case Expose:
		XClearWindow (dpy, win);
		redraw = 1;
		break;
	    default:
		if (event.type == damageEvent + XDamageNotify)
		{
		    XDamageNotifyEvent *de = (XDamageNotifyEvent *) &event;
		    DamageBox	       *box, *b;

		    box = malloc (sizeof (DamageBox));
		    if (box)
		    {
			box->x1 = de->area.x + de->geometry.x;
			box->y1 = de->area.y + de->geometry.y;
			box->x2 = box->x1 + de->area.width;
			box->y2 = box->y1 + de->area.height;

			box->alpha = 0xffff;

			box->next = 0;

			if (boxes)
			{
			    for (b = boxes; b; b = b->next)
			    {
				if (!b->next)
				{
				    b->next = box;
				    break;
				}
			    }
			}
			else
			{
			    boxes = box;
			}

			redraw = 1;
		    }
		}
		break;
	    }
	}

	if (redraw)
	{
	    redrawBoxes ();
	    redraw = 0;
	}

	if (boxes)
	{
	    DamageBox *box;

	    for (box = boxes; box; box = box->next)
	    {
		/* good enough for now */
		box->alpha >>= 1;
	    }

	    /* removed boxes that have faded out */
	    while (boxes)
	    {
		if (boxes->alpha > 0x00ff)
		    break;

		box   = boxes;
		boxes = boxes->next;

		free (box);
	    }

	    poll (&ufd, 1, FADEOUT_SLEEP);

	    redraw = 1;
	}
	else
	{
	    poll (&ufd, 1, -1);
	}
    }

    XDestroyWindow (dpy, win);
    XCloseDisplay (dpy);

    return 0;
}
