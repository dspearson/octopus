// in this file, _Rect is os x Rect,
// _Point is os x Point
#define Point _Point
#define Rect _Rect

#include <Carbon/Carbon.h>
#include <QuickTime/QuickTime.h> // for full screen

#undef Rect
#undef Point

#undef nil

#include "dat.h"
#include "fns.h"
#undef log2
#include <draw.h>
#include <memdraw.h>
#include "cursor.h"
#include "keyboard.h"
#include "keycodes.h"

#define	Kup	Up
#define	Kleft	Left
#define	Kdown	Down
#define	Kright	Right
#define	Kalt	LAlt
#define	Kctl	LCtrl
#define	Kshift	LShift
#define	Kpgup	Pgup
#define	Kpgdown	Pgdown
#define	Khome	Home
#define	Kins	Ins
#define	Kend	End

#define rWindowResource  128

extern	void		flushmemscreen(Rectangle);

Memimage	*gscreen;

static int readybit;
static Rendez	rend;
static int triedscreen;

///
// menu
//
static MenuRef windMenu;
static MenuRef viewMenu;

enum {
	kQuitCmd = 1,
	kFullScreenCmd = 2,
};

static WindowGroupRef winGroup = NULL;
static WindowRef theWindow = NULL;
static CGContextRef context;
static CGDataProviderRef dataProviderRef;
static CGImageRef fullScreenImage;
static CGRect devRect;
static CGRect bounds;
static PasteboardRef appleclip;
static _Rect winRect;

static Boolean altPressed = false;
static Boolean cmdPressed = false;
static Boolean button2 = false;
static Boolean button3 = false;
static uint32_t mousebuttons = 0; // bitmask of buttons currently down
static uint32_t mouseX = 0;
static uint32_t mouseY = 0;

static Boolean needflush = false;


static int
isready(void*a)
{
	return readybit;
}

CGContextRef QuartzContext;

static OSStatus MainWindowEventHandler(EventHandlerCallRef nextHandler, EventRef event, void *userData);
static OSStatus MainWindowCommandHandler(EventHandlerCallRef nextHandler, EventRef event, void *userData);

static void winproc(void *a);
static void flushproc(void *a);

void
screeninit(void)
{
	int fmt;
	int dx, dy;
	ProcessSerialNumber psn = { 0, kCurrentProcess };
	TransformProcessType(&psn, kProcessTransformToForegroundApplication);
	SetFrontProcess(&psn);

	fmt = XBGR32; //XRGB32;

	devRect = CGDisplayBounds(CGMainDisplayID());
//	devRect.origin.x = 0;
//	devRect.origin.y = 0;
//	devRect.size.width = 1024;
//	devRect.size.height = 768;
	dx = devRect.size.width;
	dy = devRect.size.height;

	if(1){	/* TO DO: new dev draw for changing screen size */
		dx = Xsize;
		dy = Ysize;
	}

	gscreen = allocmemimage(Rect(0,0,dx,dy), fmt);
	dataProviderRef = CGDataProviderCreateWithData(0, gscreen->data->bdata,
						dx * dy * 4, 0);
	fullScreenImage = CGImageCreate(dx, dy, 8, 32, dx * 4,
				CGColorSpaceCreateDeviceRGB(),
				kCGImageAlphaNoneSkipLast,
				dataProviderRef, 0, 0, kCGRenderingIntentDefault);

	kproc("osxscreen", winproc, nil, 0);
	kproc("osxflush", flushproc, nil, 0);
	Sleep(&rend, isready, nil);
}

void
window_resized(void)
{
	GetWindowBounds(theWindow, kWindowContentRgn, &winRect);

	bounds = CGRectMake(0, 0, winRect.right-winRect.left, winRect.bottom - winRect.top);
}

static void
flushproc(void *a)
{
	for(;;) {
		if(needflush) {
			drawqlock();
			QDBeginCGContext(GetWindowPort(theWindow), &context);
			CGContextFlush(context);
			QDEndCGContext(GetWindowPort(theWindow), &context);
			needflush = false;
 			drawqunlock();
		}
		usleep(33333);
	}
}

static void
winproc(void *a)
{
MenuItemIndex index;
	int dx, dy;

	winRect.left = 30;
	winRect.top = 60;
	dx = devRect.size.width*0.75;	/* devRect is full screen; take only most of it */
	dy = devRect.size.height*0.75;
	if(1){	/* TO DO */
		dx = Xsize;
		dy = Ysize;
	}
	winRect.bottom = winRect.top + dy;
	winRect.right = winRect.left + dx;

	ClearMenuBar();
	InitCursor();

	CreateStandardWindowMenu(0, &windMenu);
	InsertMenu(windMenu, 0);

	CreateNewMenu(1004, 0, &viewMenu);
	SetMenuTitleWithCFString(viewMenu, CFSTR("View"));
	AppendMenuItemTextWithCFString(viewMenu, CFSTR("Full Screen"), 0,
			kFullScreenCmd, &index);
	SetMenuItemCommandKey(viewMenu, index, 0, 'F');
	AppendMenuItemTextWithCFString(viewMenu, CFSTR("ctrl-opt to return"),
			kMenuItemAttrDisabled,
			kFullScreenCmd, &index);
	InsertMenu(viewMenu, GetMenuID(windMenu));

	DrawMenuBar();
	uint32_t windowAttrs = 0
				| kWindowCloseBoxAttribute
				| kWindowCollapseBoxAttribute
//				| kWindowResizableAttribute		// TO DO
				| kWindowStandardHandlerAttribute
//				| kWindowFullZoomAttribute		// TO DO
		;

	CreateNewWindow(kDocumentWindowClass, windowAttrs, &winRect, &theWindow);
	CreateWindowGroup(0, &winGroup);
	SetWindowGroup(theWindow, winGroup);

	SetWindowTitleWithCFString(theWindow, CFSTR("Octopus"));

	if(PasteboardCreate(kPasteboardClipboard, &appleclip) != noErr)
		sysfatal("pasteboard create failed");

	const EventTypeSpec commands[] = {
		{ kEventClassWindow, kEventWindowClosed },
		{ kEventClassWindow, kEventWindowBoundsChanged },
		{ kEventClassCommand, kEventCommandProcess }
	};
	const EventTypeSpec events[] = {
		{ kEventClassTextInput, kEventTextInputUpdateActiveInputArea },
		{ kEventClassTextInput, kEventTextInputUnicodeForKeyEvent },
		{ kEventClassTextInput, kEventTextInputOffsetToPos },
		{ kEventClassTextInput, kEventTextInputPosToOffset },
		{ kEventClassTextInput, kEventTextInputShowHideBottomWindow },
		{ kEventClassTextInput, kEventTextInputGetSelectedText },
		{ kEventClassTextInput, kEventTextInputUnicodeText },
		{ kEventClassTextInput, kEventTextInputFilterText },
		{ kEventClassKeyboard, kEventRawKeyDown },
		{ kEventClassKeyboard, kEventRawKeyModifiersChanged },
		{ kEventClassKeyboard, kEventRawKeyRepeat },
		{ kEventClassMouse, kEventMouseDown },
		{ kEventClassMouse, kEventMouseUp },
		{ kEventClassMouse, kEventMouseMoved },
		{ kEventClassMouse, kEventMouseDragged },
		{ kEventClassMouse, kEventMouseWheelMoved },
	};

 	InstallApplicationEventHandler (
 								NewEventHandlerUPP (MainWindowEventHandler),
								GetEventTypeCount(events),
								events,
								NULL,
								NULL);
	InstallWindowEventHandler (
								theWindow,
								NewEventHandlerUPP (MainWindowCommandHandler),
								GetEventTypeCount(commands),
								commands,
								theWindow,
								NULL);

	ShowWindow(theWindow);
	ShowMenuBar();
	window_resized();
	SelectWindow(theWindow);
	// Run the event loop
	readybit = 1;
	Wakeup(&rend);
	RunApplicationEventLoop();

}


void
sendbuttons(int b, int x, int y)
{
	mousetrack(b, x, y, 0);
}

static Ptr fullScreenRestore;
static int amFullScreen = 0;
static WindowRef oldWindow = NULL;

static void
leave_full_screen(void)
{
	if (0 && amFullScreen) {
		EndFullScreen(fullScreenRestore, 0);
		theWindow = oldWindow;
		ShowWindow(theWindow);
		amFullScreen = 0;
		window_resized();
		Rectangle rect =  { { 0, 0 }, { bounds.size.width, bounds.size.height} };
		drawqlock();
 		flushmemscreen(rect);
 		drawqunlock();
	}
}

enum {
	kF1KeyCode	 = 0x7A,	// Undo
	kF2KeyCode	 = 0x78,	// Cut
	kF3KeyCode	 = 0x63,	// Copy
	kF4KeyCode	 = 0x76,	// Paste
	kF5KeyCode	 = 0x60,
	kF6KeyCode	 = 0x61,
	kF7KeyCode	 = 0x62,
	kF8KeyCode	 = 0x64,
	kF9KeyCode	 = 0x65,
	kF10KeyCode	 = 0x6D,
	kF11KeyCode	 = 0x67,
	kF12KeyCode	 = 0x6F,
	kF13KeyCode	 = 0x69,	// Print Screen
	kF14KeyCode	 = 0x6B,	// Scroll Lock
	kF15KeyCode	 = 0x71,	// Pause
	};
static int
convert_unichar(UInt32 charcode)
{
	switch(charcode) {
	case kEnterCharCode:
	case kReturnCharCode:		return '\n';
	case kEscapeCharCode:		return 27;
	case kBackspaceCharCode:	return '\b';
	case kOptionUnicode:		return Kalt;
	case kControlUnicode:		return Kctl;
	case kShiftUnicode:			return Kshift;
	case kFunctionKeyCharCode:
		switch(charcode) {
		case kF1KeyCode:		return KF+1;
		case kF2KeyCode:		return KF+2;
		case kF3KeyCode:		return KF+3;
		case kF4KeyCode:		return KF+4;
		case kF5KeyCode:		return KF+5;
		case kF6KeyCode:		return KF+6;
		case kF7KeyCode:		return KF+7;
		case kF8KeyCode:		return KF+8;
		case kF9KeyCode:		return KF+9;
		case kF10KeyCode:		return KF+10;
		case kF11KeyCode:		return KF+11;
		case kF12KeyCode:		return KF+12;
		default:				return charcode;
		}
//	case kInsertCharCode:		return Kins;
	case kDeleteCharCode:		return 0x7F;
	case kHomeCharCode:			return Khome;
	case kEndCharCode:			return Kend;
	case kTabCharCode:			return '\t';
	case kPageUpCharCode:		return Kpgup;
	case kPageDownCharCode:		return Kpgdown;
	case kUpArrowCharCode:		return Kup;
	case kDownArrowCharCode:	return Kdown;
	case kLeftArrowCharCode:	return Kleft;
	case kRightArrowCharCode:	return Kright;
	default:					return charcode;
	}
}

static void
full_screen(void)
{
	if (0 && !amFullScreen) {
		oldWindow = theWindow;
		HideWindow(theWindow);
		BeginFullScreen(&fullScreenRestore, 0, 0, 0, &theWindow, 0, 0);
		amFullScreen = 1;
		window_resized();
		Rectangle rect =  { { 0, 0 },
 							{ bounds.size.width,
 							  bounds.size.height} };
		drawqlock();
 		flushmemscreen(rect);
 		drawqunlock();
	}
}
static OSStatus
handle_unicode(EventRef event)
{
	UInt32 actual_size, i;
	UniChar *text;
	UniCharCount num_chars;
	OSStatus result = noErr;

	// hack to keep sequences like Cmd+f from writing to buffer
	if(! cmdPressed) {
		result = GetEventParameter (event, kEventParamTextInputSendText,
								typeUnicodeText, NULL, 0, &actual_size, NULL);
		if(result == noErr) {
			text = (UniChar*) NewPtr(actual_size);
			result = GetEventParameter (event, kEventParamTextInputSendText,
		   							typeUnicodeText, NULL, actual_size, NULL, text);
			if(result == noErr) {
				num_chars = actual_size / sizeof(UniChar);
				for(i=0; i < num_chars; i++) {
					int key = convert_unichar(text[i]);
					gkbdputc(gkbdq, key);
				}
			}
		}
	}

	return result;
}
static OSStatus
handle_text_input_event(EventRef event)
{
	OSStatus result;

	UInt32 kind = GetEventKind (event);
	switch(kind) {

	case kEventTextInputUnicodeForKeyEvent:
	case kEventTextInputUnicodeText:
//		fprint(2, "unicode input event:\n");
		result = handle_unicode(event);
		break;

	case kEventTextInputUpdateActiveInputArea:
	case kEventTextInputOffsetToPos:	
	case kEventTextInputPosToOffset:
	case kEventTextInputShowHideBottomWindow:
	case kEventTextInputGetSelectedText:
	case kEventTextInputFilterText:
	default:
		result = eventNotHandledErr;
		break;
		
	}
	
	return result;
}
static OSStatus
handle_kbd_event(EventRef event)
{
	OSStatus result = noErr;

	char macCharCodes;
	UInt32 macKeyCode;
	UInt32 macKeyModifiers;

	GetEventParameter(event, kEventParamKeyMacCharCodes, typeChar,
					NULL, sizeof(macCharCodes), NULL, &macCharCodes);
	GetEventParameter(event, kEventParamKeyCode, typeUInt32, NULL,
					sizeof(macKeyCode), NULL, &macKeyCode);
	GetEventParameter(event, kEventParamKeyModifiers, typeUInt32, NULL,
					sizeof(macKeyModifiers), NULL, &macKeyModifiers);

//	fprint(2, "mac char is %04x=%c\n", macCharCodes, macCharCodes);
//	fprint(2, "mac key code is %ld\n", macKeyCode);

	UInt32 kind = GetEventKind (event);
	switch(kind) {

	case kEventRawKeyModifiersChanged:
//		fprint(2, "kbd event:	key modifiers changed!\n");
		switch(macKeyModifiers & (optionKey | cmdKey)) {

		case (optionKey | cmdKey):
			/* due to chording we need to handle the case when both
			 * modifier keys are pressed at the same time.
			 * currently it's only 2-3 snarf and the 3-2 noop
			 */
			 
//			fprint(2, "kbd event:	Opt|Cmd pressed!\n");
			altPressed = true;
			cmdPressed = true;
			if(mousebuttons & 1 || mousebuttons & 2 || mousebuttons & 4) {
				mousebuttons |= 2;	/* set button 2 */
				mousebuttons |= 4;	/* set button 3 */
				button2 = true;
				button3 = true;
				sendbuttons(mousebuttons, mouseX, mouseY);
			}
			break;
		
		case optionKey:
//			fprint(2, "kbd event:	Opt pressed!\n");
			altPressed = true;
			if(mousebuttons & 1 || mousebuttons & 4) {
				mousebuttons |= 2;	/* set button 2 */
				button2 = true;
				sendbuttons(mousebuttons, mouseX, mouseY);
			}
			break;
		
		case cmdKey:
//			fprint(2, "kbd event:	Cmd pressed!\n");
			cmdPressed = true;
			if(mousebuttons & 1 || mousebuttons & 2) {
				mousebuttons |= 4;	/* set button 3 */
				button3 = true;
				sendbuttons(mousebuttons, mouseX, mouseY);
			}
			break;
		
		default:
			if(button2 || button3) {
				if(button2) {
					mousebuttons &= ~2;	/* clear button 2 */
					button2 = false;
				}
				if(button3) {
					mousebuttons &= ~4;	/* clear button 3 */
					button3 = false;
				}
				sendbuttons(mousebuttons, mouseX, mouseY);
			}		
			altPressed = false;
			cmdPressed = false;
			break;
		}
		break;
		
	case kEventRawKeyDown:
	case kEventRawKeyRepeat:
		if(macKeyModifiers == cmdKey) {
			// catch fullscreen toggle key sequences while in fullscreen mode
			}
		else {
			if(macKeyCode == QZ_ESCAPE)
				gkbdputc(gkbdq, 27);
			else
				result = eventNotHandledErr;
		}
		break;
						
	default:	
		result = eventNotHandledErr;
		break;

	}

	return result;
}

static OSStatus
MainWindowEventHandler(EventHandlerCallRef nextHandler, EventRef event, void *userData)
{
	OSStatus result = noErr;
	result = CallNextEventHandler(nextHandler, event);
	UInt32 class = GetEventClass (event);
	UInt32 kind = GetEventKind (event);
	static uint32_t mousebuttons = 0; // bitmask of buttons currently down
	static uint32_t mouseX = 0;
	static uint32_t mouseY = 0;

	if(class == kEventClassKeyboard) {
		 handle_kbd_event(event);
	} else if (class == kEventClassTextInput){
		handle_text_input_event(event); }
	else if(class == kEventClassMouse) {
		_Point mousePos;

		GetEventParameter(event, kEventParamMouseLocation, typeQDPoint,
							0, sizeof mousePos, 0, &mousePos);
		
		switch(kind) {
		case kEventMouseWheelMoved:
		{
		    int32_t wheeldelta;
			GetEventParameter(event,kEventParamMouseWheelDelta,typeSInt32,
								0,sizeof(wheeldelta), 0, &wheeldelta);
			mouseX = mousePos.h - winRect.left;
			mouseY = mousePos.v - winRect.top;
			if (wheeldelta > 0)
				gkbdputc(gkbdq, Kup);
			else if(wheeldelta < 0)
				gkbdputc(gkbdq, Kdown);
			// sendbuttons(wheeldelta>0 ? 8 : 16, mouseX, mouseY);
			break;
		}
		case kEventMouseUp:
		case kEventMouseDown:
		{
			uint32_t buttons;
			uint32_t modifiers;
			GetEventParameter(event, kEventParamKeyModifiers, typeUInt32,
								0, sizeof(modifiers), 0, &modifiers);
			GetEventParameter(event, kEventParamMouseChord, typeUInt32,
								0, sizeof buttons, 0, &buttons);
			/* simulate other buttons via alt/apple key. like x11 */
			if(modifiers & optionKey) {
				mousebuttons = ((buttons & 1) ? 2 : 0);
				altPressed = false;
			} else if(modifiers & cmdKey)
				mousebuttons = ((buttons & 1) ? 4 : 0);
			else
				mousebuttons = (buttons & 1);

			mousebuttons |= ((buttons & 2)<<1);
			mousebuttons |= ((buttons & 4)>>1);

		} /* Fallthrough */
		case kEventMouseMoved:
		case kEventMouseDragged:
			mouseX = mousePos.h - winRect.left;
			mouseY = mousePos.v - winRect.top;
			sendbuttons(mousebuttons, mouseX, mouseY);
			break;
		default:
			result = eventNotHandledErr;
			break;
		}
	}
	return result;
}


//default window command handler (from menus)
static OSStatus
MainWindowCommandHandler(EventHandlerCallRef nextHandler, EventRef event, void *userData)
{
	OSStatus result = noErr;
	UInt32 class = GetEventClass (event);
	UInt32 kind = GetEventKind (event);

	result = CallNextEventHandler(nextHandler, event);

	if(class == kEventClassCommand) {
		HICommand theHICommand;
		GetEventParameter(event, kEventParamDirectObject, typeHICommand,
							NULL, sizeof(HICommand), NULL, &theHICommand);

		switch(theHICommand.commandID) {
		case kHICommandQuit:
			cleanexit(0);
			break;

		case kFullScreenCmd:
			full_screen();
			break;

		default:
			result = eventNotHandledErr;
			break;
		}
	} else if(class == kEventClassWindow) {
		WindowRef     window;
		_Rect          rectPort = {0,0,0,0};

		GetEventParameter(event, kEventParamDirectObject, typeWindowRef,
							NULL, sizeof(WindowRef), NULL, &window);

		if(window)
			GetPortBounds(GetWindowPort(window), &rectPort);

		switch(kind) {
		case kEventWindowClosed:
			theWindow = NULL;
			cleanexit(0); // only one window
			break;

		//resize window
		case kEventWindowBoundsChanged:
			window_resized();
			Rectangle rect =  { { 0, 0 },
 									{ bounds.size.width,
 									  bounds.size.height} };
			drawqlock();
 			flushmemscreen(rect);
 			drawqunlock();
			break;

		default:
			result = eventNotHandledErr;
			break;
		}
	}

	return result;
}

void
flushmemscreen(Rectangle r)
{
	CGRect rbounds;

	// sanity check.  Trips from the initial "terminal"
	if (r.max.x < r.min.x || r.max.y < r.min.y)
		return;

	rbounds.size.width = r.max.x - r.min.x;
	rbounds.size.height = r.max.y - r.min.y;
	rbounds.origin.x = r.min.x;
	rbounds.origin.y = r.min.y;

	if(rbounds.size.width <= 0 || rbounds.size.height <= 0)
		return;
		
	QDBeginCGContext(GetWindowPort(theWindow), &context);
	
	// The sub-image is relative to our whole screen image.
	CGImageRef subimg = CGImageCreateWithImageInRect(fullScreenImage, rbounds);
	
	// Drawing the sub-image is relative to the window.
	rbounds.origin.y = winRect.bottom - winRect.top - r.min.y - rbounds.size.height;
	CGContextDrawImage(context, rbounds, subimg);
	CGImageRelease(subimg);
	QDEndCGContext(GetWindowPort(theWindow), &context);

	needflush = true;
}

uchar*
attachscreen(Rectangle *r, ulong *chan, int *depth, int *width, int *softscreen)
{	
	if(!triedscreen) {
		triedscreen = 1;
		screeninit();	/* TO DO: call this elsewhere? */
	}
	*r = gscreen->r;
	*chan = gscreen->chan;
	*depth = gscreen->depth;
	*width = gscreen->width;
	*softscreen = 1;

	return gscreen->data->bdata;
}

// PAL - no palette handling.  Don't intend to either.
void
getcolor(ulong i, ulong *r, ulong *g, ulong *b)
{

// PAL: Certainly wrong to return a grayscale.
	 *r = i;
	 *g = i;
	 *b = i;
}

void
setcolor(ulong index, ulong r, ulong g, ulong b)
{
	USED(index); USED(r); USED(g); USED(b);
}

enum{
	CursorSize=	16,	/* biggest cursor size, 16 in osx */
	SnarfSize=	100*1024,
};

static char snarf[3*SnarfSize+1];
static Rune rsnarf[SnarfSize+1];

char*
clipread(void)
{
	CFDataRef cfdata;
	OSStatus err = noErr;
	ItemCount nitems;
	int i;
	char *s;

	// Wow.  This is ridiculously complicated.
	PasteboardSynchronize(appleclip);
	if((err = PasteboardGetItemCount(appleclip, &nitems)) != noErr) {
		fprint(2, "apple pasteboard GetItemCount failed - Error %d\n", err);
		return 0;
	}

	// Yes, based at 1.  Silly API.
	for(i = 1; i <= nitems; i++) {
		PasteboardItemID itemID;
		CFArrayRef flavorTypeArray;
		CFIndex flavorCount;

		if((err = PasteboardGetItemIdentifier(appleclip, i, &itemID)) != noErr){
			fprint(2, "Can't get pasteboard item identifier: %d\n", err);
			return 0;
		}

		if((err = PasteboardCopyItemFlavors(appleclip, itemID, &flavorTypeArray))!=noErr){
			fprint(2, "Can't copy pasteboard item flavors: %d\n", err);
			return 0;
		}

		flavorCount = CFArrayGetCount(flavorTypeArray);
		CFIndex flavorIndex;
		for(flavorIndex = 0; flavorIndex < flavorCount; ++flavorIndex){
			CFStringRef flavorType;
			flavorType = (CFStringRef)CFArrayGetValueAtIndex(flavorTypeArray, flavorIndex);
			if (UTTypeConformsTo(flavorType, CFSTR("public.utf16-plain-text"))){
				if((err = PasteboardCopyItemFlavorData(appleclip, itemID,
					CFSTR("public.utf16-plain-text"), &cfdata)) != noErr){
					fprint(2, "apple pasteboard CopyItem failed - Error %d\n", err);
					return 0;
				}
				CFIndex length = CFDataGetLength(cfdata);
				if (length > sizeof rsnarf) length = sizeof rsnarf;
				CFDataGetBytes(cfdata, CFRangeMake(0, length), (uint8_t *)rsnarf);
				snprint(snarf, sizeof snarf, "%.*S", length/sizeof(Rune), rsnarf);
				for(s = snarf; *s; s++)
					if(*s == '\r')
						*s = '\n';
				CFRelease(cfdata);
				return strdup(snarf);
			}
		}
	}
	return 0;
}

int
clipwrite(char *snarf)
{
	CFDataRef cfdata;
	PasteboardSyncFlags flags;

	runeseprint(rsnarf, rsnarf+nelem(rsnarf), "%s", snarf);
	if(PasteboardClear(appleclip) != noErr){
		fprint(2, "apple pasteboard clear failed\n");
		return 0;
	}
	flags = PasteboardSynchronize(appleclip);
	if((flags&kPasteboardModified) || !(flags&kPasteboardClientIsOwner)){
		fprint(2, "apple pasteboard cannot assert ownership\n");
		return 0;
	}
	cfdata = CFDataCreate(kCFAllocatorDefault, (uchar*)rsnarf, runestrlen(rsnarf)*2);
	if(cfdata == nil){
		fprint(2, "apple pasteboard cfdatacreate failed\n");
		return 0;
	}
	if(PasteboardPutItemFlavor(appleclip, (PasteboardItemID)1,
		CFSTR("public.utf16-plain-text"), cfdata, 0) != noErr){
		fprint(2, "apple pasteboard putitem failed\n");
		CFRelease(cfdata);
		return 0;
	}
	CFRelease(cfdata);
	return 1;
}

static int
revbyte(int b)
{
	int r;

	r = 0;
	r |= (b&0x01) << 7;
	r |= (b&0x02) << 5;
	r |= (b&0x04) << 3;
	r |= (b&0x08) << 1;
	r |= (b&0x10) >> 1;
	r |= (b&0x20) >> 3;
	r |= (b&0x40) >> 5;
	r |= (b&0x80) >> 7;
	return r;
}

void
setpointer(int x, int y)
{
	CGPoint pnt;

	pnt.x = x + winRect.left;
	pnt.y = y + winRect.top;
	CGWarpMouseCursorPosition(pnt);
}

void
drawcursor(Drawcursor* c)
{
	Cursor crsr;
    short *ps, *pm;
	short *bc, *bs;
    int i;

	if(c->data == nil){
		InitCursor();
	} else {	
		ps = crsr.data;
		pm = crsr.mask;
		bc = c->data;
		bs = c->data + 32;
     for(i = 0; i < 16; i++){
                       *ps++ = bs[i];
                       *pm++ = bs[i] | bc[i];
               }


		crsr.hotSpot.h = -c->hotx;
		crsr.hotSpot.v = -c->hoty;
		SetCursor(&crsr);
	}
}
