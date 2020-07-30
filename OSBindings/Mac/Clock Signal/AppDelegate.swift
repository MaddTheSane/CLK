//
//  AppDelegate.swift
//  Clock Signal
//
//  Created by Thomas Harte on 16/07/2015.
//  Copyright 2015 Thomas Harte. All rights reserved.
//

import Cocoa

@NSApplicationMain
class AppDelegate: NSObject, NSApplicationDelegate {

	func applicationDidFinishLaunching(_ aNotification: Notification) {
		// Insert code here to initialize your application.
	}

	func applicationWillTerminate(_ aNotification: Notification) {
		// Insert code here to tear down your application.
	}

	private var hasShownOpenDocument = false
	func applicationShouldOpenUntitledFile(_ sender: NSApplication) -> Bool {
		// Decline to show the 'New...' selector by default; the 'Open...'
		// dialogue has already been shown if this application was started
		// without a file.
		//
		// Obiter: I dislike it when other applications do this for me, but it
		// seems to be the new norm, and I've had user feedback that showing
		// nothing is confusing. So here it is.
		if !hasShownOpenDocument {
			NSDocumentController.shared.openDocument(self)
			hasShownOpenDocument = true
		}
		return false
	}
}
