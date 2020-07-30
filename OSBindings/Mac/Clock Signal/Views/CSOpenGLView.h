//
//  CSOpenGLView.h
//  Clock Signal
//
//  Created by Thomas Harte on 16/07/2015.
//  Copyright 2015 Thomas Harte. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>

@class CSOpenGLView;

typedef NS_ENUM(NSInteger, CSOpenGLViewRedrawEvent) {
	/// Indicates that AppKit requested a redraw for some reason (mostly likely, the window is being resized). So,
	/// if the delegate doesn't redraw the view, the user is likely to see a graphical flaw.
	CSOpenGLViewRedrawEventAppKit,
	/// Indicates that the view's display-linked timer has triggered a redraw request. So, if the delegate doesn't
	/// redraw the view, the user will just see the previous drawing without interruption.
	CSOpenGLViewRedrawEventTimer
};

@protocol CSOpenGLViewDelegate <NSObject>
/*!
	Requests that the delegate produce an image of its current output state. May be called on
	any queue or thread.
	@param view The view making the request.
	@param redrawEvent If @c YES then the delegate may decline to redraw if its output would be
	identical to the previous frame. If @c NO then the delegate must draw.
*/
- (void)openGLViewRedraw:(nonnull CSOpenGLView *)view event:(CSOpenGLViewRedrawEvent)redrawEvent;

/*!
	Announces receipt of a file by drag and drop to the delegate.
	@param view The view making the request.
	@param URL The file URL of the received file.
*/
- (void)openGLView:(nonnull CSOpenGLView *)view didReceiveFileAtURL:(nonnull NSURL *)URL;

/*!
	Announces 'capture' of the mouse — i.e. that the view is now preventing the mouse from exiting
	the window, in order to forward continuous mouse motion.
	@param view The view making the announcement.
*/
- (void)openGLViewDidCaptureMouse:(nonnull CSOpenGLView *)view;

/*!
	Announces that the mouse is no longer captured.
	@param view The view making the announcement.
*/
- (void)openGLViewDidReleaseMouse:(nonnull CSOpenGLView *)view;

/*!
	Announces that the OS mouse cursor is now being displayed again, after having been invisible.
	@param view The view making the announcement.
*/
- (void)openGLViewDidShowOSMouseCursor:(nonnull CSOpenGLView *)view;

/*!
	Announces that the OS mouse cursor will now be hidden.
	@param view The view making the announcement.
*/
- (void)openGLViewWillHideOSMouseCursor:(nonnull CSOpenGLView *)view;

@end

@protocol CSOpenGLViewResponderDelegate <NSObject>
/*!
	Supplies a keyDown event to the delegate.
	@param event The @c NSEvent describing the keyDown.
*/
- (void)keyDown:(nonnull NSEvent *)event;

/*!
	Supplies a keyUp event to the delegate.
	@param event The @c NSEvent describing the keyUp.
*/
- (void)keyUp:(nonnull NSEvent *)event;

/*!
	Supplies a flagsChanged event to the delegate.
	@param event The @c NSEvent describing the flagsChanged.
*/
- (void)flagsChanged:(nonnull NSEvent *)event;

/*!
	Supplies a paste event to the delegate.
*/
- (void)paste:(nonnull id)sender;

@optional

/*!
	Supplies a mouse moved event to the delegate. This functions only if
	shouldCaptureMouse is set to YES, in which case the view will ensure it captures
	the mouse and returns only relative motion
	(Cf. CGAssociateMouseAndMouseCursorPosition). It will also elide mouseDragged:
	(and rightMouseDragged:, etc) and mouseMoved: events.
*/
- (void)mouseMoved:(nonnull NSEvent *)event;

/*!
	Supplies a mouse button down event. This elides mouseDown, rightMouseDown and otherMouseDown.
	@c shouldCaptureMouse must be set to @c YES to receive these events.
*/
- (void)mouseDown:(nonnull NSEvent *)event;

/*!
	Supplies a mouse button up event. This elides mouseUp, rightMouseUp and otherMouseUp.
	@c shouldCaptureMouse must be set to @c YES to receive these events.
*/
- (void)mouseUp:(nonnull NSEvent *)event;

@end

/*!
	Although I'm still on the fence about this as a design decision, CSOpenGLView  is itself responsible
	for creating and destroying a CVDisplayLink. There's a practical reason for this: you'll get real synchronisation
	only if a link is explicitly tied to a particular display, and the CSOpenGLView therefore owns the knowledge
	necessary to decide when to create and modify them. It doesn't currently just propagate "did change screen"-type
	messages because I haven't yet found a way to track that other than polling, in which case I might as well put
	that into the display link callback.
*/
@protocol CSOpenGLViewDisplayLinkDelegate <NSObject>

/*!
	Informs the delegate that the display link has fired.
*/
- (void)openGLViewDisplayLinkDidFire:(nonnull CSOpenGLView *)view now:(nonnull const CVTimeStamp *)now outputTime:(nonnull const CVTimeStamp *)outputTime;

@end

/*!
	Provides an OpenGL canvas with a refresh-linked update timer that can forward a subset
	of typical first-responder actions.
*/
@interface CSOpenGLView : NSOpenGLView

@property (atomic, weak, nullable) id <CSOpenGLViewDelegate> delegate;
@property (nonatomic, weak, nullable) id <CSOpenGLViewResponderDelegate> responderDelegate;
@property (atomic, weak, nullable) id <CSOpenGLViewDisplayLinkDelegate> displayLinkDelegate;

/// Determines whether the view offers mouse capturing — i.e. if the user clicks on the view then
/// then the system cursor is disabled and the mouse events defined by CSOpenGLViewResponderDelegate
/// are forwarded, unless and until the user releases the mouse using the control+command shortcut.
@property (nonatomic, assign) BOOL shouldCaptureMouse;

/// Determines whether the CSOpenGLViewResponderDelegate of this window expects to use the command
/// key as though it were any other key — i.e. all command combinations should be forwarded to the delegate,
/// not being allowed to trigger regular application shortcuts such as command+q or command+h.
///
/// How the view respects this will depend on other state; if this view is one that captures the mouse then it
/// will usurp command only while the mouse is captured.
///
/// TODO: what's smart behaviour if this view doesn't capture the mouse? Probably
/// force a similar capturing behaviour?
@property (nonatomic, assign) BOOL shouldUsurpCommand;

/*!
	Ends the timer tracking time; should be called prior to giving up the last owning reference
	to ensure that any retain cycles implied by the timer are resolved.
*/
- (void)invalidate;

/// The size in pixels of the OpenGL canvas, factoring in screen pixel density and view size in points.
@property (nonatomic, readonly) CGSize backingSize;

/*!
	Locks this view's OpenGL context and makes it current, performs @c action and then unlocks
	the context. @c action is performed on the calling queue.
*/
- (void)performWithGLContext:(nonnull dispatch_block_t NS_NOESCAPE)action flushDrawable:(BOOL)flushDrawable;
- (void)performWithGLContext:(nonnull dispatch_block_t NS_NOESCAPE)action;

/*!
	Instructs that the mouse cursor, if currently captured, should be released.
*/
- (void)releaseMouse;

@end
