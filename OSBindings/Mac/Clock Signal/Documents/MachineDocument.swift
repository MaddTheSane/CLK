//
//  MachineDocument.swift
//  Clock Signal
//
//  Created by Thomas Harte on 04/01/2016.
//  Copyright 2016 Thomas Harte. All rights reserved.
//

import AudioToolbox
import Cocoa

class MachineDocument:
	NSDocument,
	NSWindowDelegate,
	CSMachineDelegate,
	CSOpenGLViewDelegate,
	CSOpenGLViewResponderDelegate,
	CSBestEffortUpdaterDelegate,
	CSAudioQueueDelegate,
	CSROMReciverViewDelegate
{
	// MARK: - Mutual Exclusion.

	/// Ensures exclusive access between calls to self.machine.run and close().
	private let actionLock = NSLock()
	/// Ensures exclusive access between calls to machine.updateView and machine.drawView, and close().
	private let drawLock = NSLock()
	/// Ensures exclusive access to the best-effort updater.
	private let bestEffortLock = NSLock()

	// MARK: - Machine details.

	/// A description of the machine this document should represent once fully set up.
	private var machineDescription: CSStaticAnalyser?

	/// The active machine, following its successful creation.
	private var machine: CSMachine!

	/// @returns the appropriate window content aspect ratio for this @c self.machine.
	private func aspectRatio() -> NSSize {
		return NSSize(width: 4.0, height: 3.0)
	}

	/// The output audio queue, if any.
	private var audioQueue: CSAudioQueue!

	/// The best-effort updater.
	private var bestEffortUpdater: CSBestEffortUpdater?

	// MARK: - Main NIB connections.

	/// The OpenGL view to receive this machine's display.
	@IBOutlet weak var openGLView: CSOpenGLView!

	/// The options panel, if any.
	@IBOutlet var optionsPanel: MachinePanel!

	/// An action to display the options panel, if there is one.
	@IBAction func showOptions(_ sender: AnyObject!) {
		optionsPanel?.setIsVisible(true)
	}

	/// The activity panel, if one is deemed appropriate.
	@IBOutlet var activityPanel: NSPanel!

	/// An action to display the activity panel, if there is one.
	@IBAction func showActivity(_ sender: AnyObject!) {
		activityPanel.setIsVisible(true)
	}

	// MARK: - NSDocument Overrides and NSWindowDelegate methods.

	/// Links this class to the MachineDocument NIB.
	override var windowNibName: NSNib.Name? {
		return "MachineDocument"
	}

	convenience init(type typeName: String) throws {
		self.init()
		self.fileType = typeName
	}

	override func read(from url: URL, ofType typeName: String) throws {
		if let analyser = CSStaticAnalyser(fileAt: url) {
			self.displayName = analyser.displayName
			self.configureAs(analyser)
		} else {
			throw NSError(domain: "MachineDocument", code: -1, userInfo: nil)
		}
	}

	override func close() {
		activityPanel?.setIsVisible(false)
		activityPanel = nil

		optionsPanel?.setIsVisible(false)
		optionsPanel = nil

		bestEffortLock.lock()
		if let bestEffortUpdater = bestEffortUpdater {
			bestEffortUpdater.delegate = nil
			bestEffortUpdater.flush()
			self.bestEffortUpdater = nil
		}
		bestEffortLock.unlock()

		actionLock.lock()
		drawLock.lock()
		machine = nil
		openGLView.delegate = nil
		openGLView.invalidate()
		actionLock.unlock()
		drawLock.unlock()

		super.close()
	}

	override func data(ofType typeName: String) throws -> Data {
		throw NSError(domain: NSOSStatusErrorDomain, code: unimpErr, userInfo: nil)
	}

	override func windowControllerDidLoadNib(_ aController: NSWindowController) {
		super.windowControllerDidLoadNib(aController)
		aController.window?.contentAspectRatio = self.aspectRatio()
	}

	private var missingROMs: [CSMissingROM] = []
	func configureAs(_ analysis: CSStaticAnalyser) {
		self.machineDescription = analysis

		let missingROMs = NSMutableArray()
		if let machine = CSMachine(analyser: analysis, missingROMs: missingROMs) {
			self.machine = machine
			setupMachineOutput()
			setupActivityDisplay()
		} else {
			// Store the selected machine and list of missing ROMs, and
			// show the missing ROMs dialogue.
			self.missingROMs = []
			for untypedMissingROM in missingROMs {
				self.missingROMs.append(untypedMissingROM as! CSMissingROM)
			}

			requestRoms()
		}
	}

	enum InteractionMode {
		case notStarted, showingMachinePicker, showingROMRequester, showingMachine
	}
	private var interactionMode: InteractionMode = .notStarted

	// Attempting to show a sheet before the window is visible (such as when the NIB is loaded) results in
	// a sheet mysteriously floating on its own. For now, use windowDidUpdate as a proxy to know that the window
	// is visible, though it's a little premature.
	func windowDidUpdate(_ notification: Notification) {
		// Grab the regular window title, if it's not already stored.
		if self.unadornedWindowTitle.count == 0 {
			self.unadornedWindowTitle = self.windowControllers[0].window!.title
		}

		// If an interaction mode is not yet in effect, pick the proper one and display the relevant thing.
		if self.interactionMode == .notStarted {
			// If a full machine exists, just continue showing it.
			if self.machine != nil {
				self.interactionMode = .showingMachine
				setupMachineOutput()
				return
			}

			// If a machine has been picked but is not showing, there must be ROMs missing.
			if self.machineDescription != nil {
				self.interactionMode = .showingROMRequester
				requestRoms()
				return
			}

			// If a machine hasn't even been picked yet, show the machine picker.
			self.interactionMode = .showingMachinePicker
			Bundle.main.loadNibNamed("MachinePicker", owner: self, topLevelObjects: nil)
			self.machinePicker?.establishStoredOptions()
			self.windowControllers[0].window?.beginSheet(self.machinePickerPanel!, completionHandler: nil)
		}
	}

	// MARK: - Connections Between Machine and the Outside World

	private func setupMachineOutput() {
		if let machine = self.machine, let openGLView = self.openGLView {
			// Establish the output aspect ratio and audio.
			let aspectRatio = self.aspectRatio()
			openGLView.perform(glContext: {
				machine.setView(openGLView, aspectRatio: Float(aspectRatio.width / aspectRatio.height))
			})

			// Attach an options panel if one is available.
			if let optionsPanelNibName = self.machineDescription?.optionsPanelNibName {
				Bundle.main.loadNibNamed(optionsPanelNibName, owner: self, topLevelObjects: nil)
				self.optionsPanel.machine = machine
				self.optionsPanel?.establishStoredOptions()
				showOptions(self)
			}

			machine.delegate = self
			self.bestEffortUpdater = CSBestEffortUpdater()

			// Callbacks from the OpenGL may come on a different thread, immediately following the .delegate set;
			// hence the full setup of the best-effort updater prior to setting self as a delegate.
			openGLView.delegate = self
			openGLView.responderDelegate = self

			// If this machine has a mouse, enable mouse capture.
			openGLView.shouldCaptureMouse = machine.hasMouse

			setupAudioQueueClockRate()

			// Bring OpenGL view-holding window on top of the options panel and show the content.
			openGLView.isHidden = false
			openGLView.window!.makeKeyAndOrderFront(self)
			openGLView.window!.makeFirstResponder(openGLView)

			// Start accepting best effort updates.
			self.bestEffortUpdater!.delegate = self
		}
	}

	func machineSpeakerDidChangeInputClock(_ machine: CSMachine) {
		setupAudioQueueClockRate()
	}

	private func setupAudioQueueClockRate() {
		// establish and provide the audio queue, taking advice as to an appropriate sampling rate
		let maximumSamplingRate = CSAudioQueue.preferredSamplingRate()
		let selectedSamplingRate = self.machine.idealSamplingRate(from: NSRange(location: 0, length: NSInteger(maximumSamplingRate)))
		if selectedSamplingRate > 0 {
			audioQueue = CSAudioQueue(samplingRate: Float64(selectedSamplingRate))
			audioQueue.delegate = self
			self.machine.audioQueue = self.audioQueue
			self.machine.setAudioSamplingRate(selectedSamplingRate, bufferSize:audioQueue.preferredBufferSize)
		}
	}

	/// Responds to the CSAudioQueueDelegate dry-queue warning message by requesting a machine update.
	final func audioQueueIsRunningDry(_ audioQueue: CSAudioQueue) {
		bestEffortLock.lock()
		bestEffortUpdater?.update()
		bestEffortLock.unlock()
	}

	/// Responds to the CSOpenGLViewDelegate redraw message by requesting a machine update if this is a timed
	/// request, and ordering a redraw regardless of the motivation.
	final func openGLViewRedraw(_ view: CSOpenGLView, event redrawEvent: CSOpenGLViewRedrawEvent) {
		if redrawEvent == .timer {
			bestEffortLock.lock()
			if let bestEffortUpdater = bestEffortUpdater {
				bestEffortLock.unlock()
				bestEffortUpdater.update()
			} else {
				bestEffortLock.unlock()
			}
		}

		if drawLock.try() {
			if redrawEvent == .timer {
				machine.updateView(forPixelSize: view.backingSize)
			}
			machine.drawView(forPixelSize: view.backingSize)
			drawLock.unlock()
		}
	}

	/// Responds to CSBestEffortUpdaterDelegate update message by running the machine.
	final func bestEffortUpdater(_ bestEffortUpdater: CSBestEffortUpdater!, runForInterval duration: TimeInterval, didSkipPreviousUpdate: Bool) {
		if let machine = self.machine, actionLock.try() {
			machine.run(forInterval: duration)
			actionLock.unlock()
		}
	}

	// MARK: - Pasteboard Forwarding.

	/// Forwards any text currently on the pasteboard into the active machine.
	func paste(_ sender: Any) {
		let pasteboard = NSPasteboard.general
		if let string = pasteboard.string(forType: .string), let machine = self.machine {
			machine.paste(string)
		}
	}

	// MARK: - Runtime Media Insertion.

	/// Delegate message to receive drag and drop files.
	final func openGLView(_ view: CSOpenGLView, didReceiveFileAt URL: URL) {
		let mediaSet = CSMediaSet(fileAt: URL)
		if let mediaSet = mediaSet {
			mediaSet.apply(to: self.machine)
		}
	}

	/// Action for the insert menu command; displays an NSOpenPanel and then segues into the same process
	/// as if a file had been received via drag and drop.
	@IBAction final func insertMedia(_ sender: AnyObject!) {
		let openPanel = NSOpenPanel()
		openPanel.message = "Hint: you can also insert media by dragging and dropping it onto the machine's window."
		openPanel.beginSheetModal(for: self.windowControllers[0].window!) { (response) in
			if response == .OK {
				for url in openPanel.urls {
					let mediaSet = CSMediaSet(fileAt: url)
					if let mediaSet = mediaSet {
						mediaSet.apply(to: self.machine)
					}
				}
			}
		}
	}

	// MARK: - Input Management.

	/// Upon a resign key, immediately releases all ongoing input mechanisms — any currently pressed keys,
	/// and joystick and mouse inputs.
	func windowDidResignKey(_ notification: Notification) {
		if let machine = self.machine {
			machine.clearAllKeys()
			machine.joystickManager = nil
		}
		self.openGLView.releaseMouse()
	}

	/// Upon becoming key, attaches joystick input to the machine.
	func windowDidBecomeKey(_ notification: Notification) {
		if let machine = self.machine {
			machine.joystickManager = (DocumentController.shared as! DocumentController).joystickManager
		}
	}

	/// Forwards key down events directly to the machine.
	func keyDown(_ event: NSEvent) {
		if let machine = self.machine {
			machine.setKey(event.keyCode, characters: event.characters, isPressed: true)
		}
	}

	/// Forwards key up events directly to the machine.
	func keyUp(_ event: NSEvent) {
		if let machine = self.machine {
			machine.setKey(event.keyCode, characters: event.characters, isPressed: false)
		}
	}

	/// Synthesies appropriate key up and key down events upon any change in modifiers.
	func flagsChanged(_ newModifiers: NSEvent) {
		if let machine = self.machine {
			machine.setKey(VK_Shift, characters: nil, isPressed: newModifiers.modifierFlags.contains(.shift))
			machine.setKey(VK_Control, characters: nil, isPressed: newModifiers.modifierFlags.contains(.control))
			machine.setKey(VK_Command, characters: nil, isPressed: newModifiers.modifierFlags.contains(.command))
			machine.setKey(VK_Option, characters: nil, isPressed: newModifiers.modifierFlags.contains(.option))
		}
	}

	/// Forwards mouse movement events to the mouse.
	func mouseMoved(_ event: NSEvent) {
		if let machine = self.machine {
			machine.addMouseMotionX(event.deltaX, y: event.deltaY)
		}
	}

	/// Forwards mouse button down events to the mouse.
	func mouseUp(_ event: NSEvent) {
		if let machine = self.machine {
			machine.setMouseButton(Int32(event.buttonNumber), isPressed: false)
		}
	}

	/// Forwards mouse button up events to the mouse.
	func mouseDown(_ event: NSEvent) {
		if let machine = self.machine {
			machine.setMouseButton(Int32(event.buttonNumber), isPressed: true)
		}
	}

	// MARK: - MachinePicker Outlets and Actions
	@IBOutlet var machinePicker: MachinePicker?
	@IBOutlet var machinePickerPanel: NSWindow?
	@IBAction func createMachine(_ sender: NSButton?) {
		let selectedMachine = machinePicker!.selectedMachine()
		self.windowControllers[0].window?.endSheet(self.machinePickerPanel!)
		self.machinePicker = nil
		self.configureAs(selectedMachine)
	}

	@IBAction func cancelCreateMachine(_ sender: NSButton?) {
		close()
	}

	// MARK: - ROMRequester Outlets and Actions
	@IBOutlet var romRequesterPanel: NSWindow?
	@IBOutlet var romRequesterText: NSTextField?
	@IBOutlet var romReceiverErrorField: NSTextField?
	@IBOutlet var romReceiverView: CSROMReceiverView?
	private var romRequestBaseText = ""
	func requestRoms() {
		// Don't act yet if there's no window controller yet.
		if self.windowControllers.count == 0 {
			return
		}

		// Load the ROM requester dialogue.
		Bundle.main.loadNibNamed("ROMRequester", owner: self, topLevelObjects: nil)
		self.romReceiverView!.delegate = self
		self.romRequestBaseText = romRequesterText!.stringValue
		romReceiverErrorField?.alphaValue = 0.0

		// Populate the current absentee list.
		populateMissingRomList()

		// Show the thing.
		self.windowControllers[0].window?.beginSheet(self.romRequesterPanel!, completionHandler: nil)
	}

	@IBAction func cancelRequestROMs(_ sender: NSButton?) {
		close()
	}

	func populateMissingRomList() {
		// Fill in the missing details; first build a list of all the individual
		// line items.
		var requestLines: [String] = []
		for missingROM in self.missingROMs {
			if let descriptiveName = missingROM.descriptiveName {
				requestLines.append("• " + descriptiveName)
			} else {
				requestLines.append("• " + missingROM.fileName)
			}
		}

		// Suffix everything up to the penultimate line with a semicolon;
		// the penultimate line with a semicolon and a conjunctive; the final
		// line with a full stop.
		for x in 0 ..< requestLines.count {
			if x < requestLines.count - 2 {
				requestLines[x].append(";")
			} else if x < requestLines.count - 1 {
				requestLines[x].append("; and")
			} else {
				requestLines[x].append(".")
			}
		}
		romRequesterText!.stringValue = self.romRequestBaseText + requestLines.joined(separator: "\n")
	}

	func romReceiverView(_ view: CSROMReceiverView, didReceiveFileAt URL: URL) {
		// Test whether the file identified matches any of the currently missing ROMs.
		// If so then remove that ROM from the missing list and update the request screen.
		// If no ROMs are still missing, start the machine.
		do {
			let fileData = try Data(contentsOf: URL)
			var didInstallRom = false

			// Try to match by size first, CRC second. Accept that some ROMs may have
			// some additional appended data. Arbitrarily allow them to be up to 10kb
			// too large.
			var index = 0
			for missingROM in self.missingROMs {
				if fileData.count >= missingROM.size && fileData.count < missingROM.size + 10*1024 {
					// Trim to size.
					let trimmedData = fileData[0 ..< missingROM.size]

					// Get CRC.
					if missingROM.crc32s.contains( (trimmedData as NSData).crc32 ) {
						// This ROM matches; copy it into the application library,
						// strike it from the missing ROM list and decide how to
						// proceed.
						let fileManager = FileManager.default
						let targetPath = fileManager.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
							.appendingPathComponent("ROMImages")
							.appendingPathComponent(missingROM.machineName)
						let targetFile = targetPath
							.appendingPathComponent(missingROM.fileName)

						do {
							try fileManager.createDirectory(atPath: targetPath.path, withIntermediateDirectories: true, attributes: nil)
							try trimmedData.write(to: targetFile)
						} catch let error {
							showRomReceiverError(error: "Couldn't write to application support directory: \(error)")
						}

						self.missingROMs.remove(at: index)
						didInstallRom = true
						break
					}
				}

				index = index + 1
			}

			if didInstallRom {
				if self.missingROMs.count == 0 {
					self.windowControllers[0].window?.endSheet(self.romRequesterPanel!)
					configureAs(self.machineDescription!)
				} else {
					populateMissingRomList()
				}
			} else {
				showRomReceiverError(error: "Didn't recognise contents of \(URL.lastPathComponent)")
			}
		} catch let error {
			showRomReceiverError(error: "Couldn't read file at \(URL.absoluteString): \(error)")
		}
	}

	// Yucky ugliness follows; my experience as an iOS developer intersects poorly with
	// NSAnimationContext hence the various stateful diplications below. isShowingError
	// should be essentially a duplicate of the current alphaValue, and animationCount
	// is to resolve my inability to figure out how to cancel scheduled animations.
	private var errorText = ""
	private var isShowingError = false
	private var animationCount = 0
	private func showRomReceiverError(error: String) {
		// Set or append the new error.
		if self.errorText.count > 0 {
			self.errorText = self.errorText + "\n" + error
		} else {
			self.errorText = error
		}

		// Apply the new complete text.
		romReceiverErrorField!.stringValue = self.errorText

		if !isShowingError {
			// Schedule the box's appearance.
			NSAnimationContext.beginGrouping()
			NSAnimationContext.current.duration = 0.1
			romReceiverErrorField?.animator().alphaValue = 1.0
			NSAnimationContext.endGrouping()
			isShowingError = true
		}

		// Schedule the box to disappear.
		self.animationCount = self.animationCount + 1
		let capturedAnimationCount = animationCount
		DispatchQueue.main.asyncAfter(deadline: DispatchTime.now() + .seconds(2)) {
			if self.animationCount == capturedAnimationCount {
				NSAnimationContext.beginGrouping()
				NSAnimationContext.current.duration = 1.0
				self.romReceiverErrorField?.animator().alphaValue = 0.0
				NSAnimationContext.endGrouping()
				self.isShowingError = false
				self.errorText = ""
			}
		}
	}

	// MARK: Joystick-via-the-keyboard selection
	@IBAction func useKeyboardAsKeyboard(_ sender: NSMenuItem?) {
		machine.inputMode = .keyboard
	}

	@IBAction func useKeyboardAsJoystick(_ sender: NSMenuItem?) {
		machine.inputMode = .joystick
	}

	/// Determines which of the menu items to enable and disable based on the ability of the
	/// current machine to handle keyboard and joystick input, accept new media and whether
	/// it has an associted activity window.
	override func validateUserInterfaceItem(_ item: NSValidatedUserInterfaceItem) -> Bool {
		if let menuItem = item as? NSMenuItem {
			switch item.action {
				case #selector(self.useKeyboardAsKeyboard):
					if machine == nil || !machine.hasExclusiveKeyboard {
						menuItem.state = .off
						return false
					}

					menuItem.state = machine.inputMode == .keyboard ? .on : .off
					return true

				case #selector(self.useKeyboardAsJoystick):
					if machine == nil || !machine.hasJoystick {
						menuItem.state = .off
						return false
					}

					menuItem.state = machine.inputMode == .joystick ? .on : .off
					return true

				case #selector(self.showActivity(_:)):
					return self.activityPanel != nil

				case #selector(self.insertMedia(_:)):
					return self.machine != nil && self.machine.canInsertMedia

				default: break
			}
		}
		return super.validateUserInterfaceItem(item)
	}

	/// Saves a screenshot of the
	@IBAction func saveScreenshot(_ sender: AnyObject!) {
		// Grab a date formatter and form a file name.
		let dateFormatter = DateFormatter()
		dateFormatter.dateStyle = .short
		dateFormatter.timeStyle = .long

		let filename = ("Clock Signal Screen Shot " + dateFormatter.string(from: Date()) + ".png").replacingOccurrences(of: "/", with: "-")
			.replacingOccurrences(of: ":", with: ".")
		let pictursURL = FileManager.default.urls(for: .picturesDirectory, in: .userDomainMask)[0]
		let url = pictursURL.appendingPathComponent(filename)

		// Obtain the machine's current display.
		var imageRepresentation: NSBitmapImageRep? = nil
		self.openGLView.perform {
			imageRepresentation = self.machine.imageRepresentation
		}

		// Encode as a PNG and save.
		let pngData = imageRepresentation!.representation(using: .png, properties: [:])
		try! pngData?.write(to: url)
	}

	// MARK: - Window Title Updates.
	private var unadornedWindowTitle = ""
	func openGLViewDidCaptureMouse(_ view: CSOpenGLView) {
		self.windowControllers[0].window?.title = self.unadornedWindowTitle + " (press ⌘+control to release mouse)"
	}

	func openGLViewDidReleaseMouse(_ view: CSOpenGLView) {
		self.windowControllers[0].window?.title = self.unadornedWindowTitle
	}

	// MARK: - Activity Display.

	private class LED {
		let levelIndicator: NSLevelIndicator
		init(levelIndicator: NSLevelIndicator) {
			self.levelIndicator = levelIndicator
		}
		var isLit = false
		var isBlinking = false
	}
	private var leds: [String: LED] = [:]

	func setupActivityDisplay() {
		var leds = machine.leds
		if leds.count > 0 {
			Bundle.main.loadNibNamed("Activity", owner: self, topLevelObjects: nil)
			showActivity(nil)

			// Inspect the activity panel for indicators.
			var activityIndicators: [NSLevelIndicator] = []
			var textFields: [NSTextField] = []
			if let contentView = self.activityPanel.contentView {
				for view in contentView.subviews {
					if let levelIndicator = view as? NSLevelIndicator {
						activityIndicators.append(levelIndicator)
					}

					if let textField = view as? NSTextField {
						textFields.append(textField)
					}
				}
			}

			// If there are fewer level indicators than LEDs, trim that list.
			if activityIndicators.count < leds.count {
				leds.removeSubrange(activityIndicators.count ..< leds.count)
			}

			// Remove unused views.
			for c in leds.count ..< activityIndicators.count {
				textFields[c].removeFromSuperview()
				activityIndicators[c].removeFromSuperview()
			}

			// Apply labels and create leds entries.
			for c in 0 ..< leds.count {
				textFields[c].stringValue = leds[c]
				self.leds[leds[c]] = LED(levelIndicator: activityIndicators[c])
			}

			// Add a constraints to minimise window height.
			let heightConstraint = NSLayoutConstraint(
				item: self.activityPanel.contentView!,
				attribute: .bottom,
				relatedBy: .equal,
				toItem: activityIndicators[leds.count-1],
				attribute: .bottom,
				multiplier: 1.0,
				constant: 20.0)
			self.activityPanel.contentView?.addConstraint(heightConstraint)
		}
	}

	func machine(_ machine: CSMachine, ledShouldBlink ledName: String) {
		// If there is such an LED, switch it off for 0.03 of a second; if it's meant
		// to be off at the end of that, leave it off. Don't allow the blinks to
		// pile up — allow there to be only one in flight at a time.
		if let led = leds[ledName] {
			DispatchQueue.main.async {
				if !led.isBlinking {
					led.levelIndicator.floatValue = 0.0
					led.isBlinking = true

					DispatchQueue.main.asyncAfter(deadline: .now() + 0.03) {
						led.levelIndicator.floatValue = led.isLit ? 1.0 : 0.0
						led.isBlinking = false
					}
				}
			}
		}
	}

	func machine(_ machine: CSMachine, led ledName: String, didChangeToLit isLit: Bool) {
		// If there is such an LED, switch it appropriately.
		if let led = leds[ledName] {
			DispatchQueue.main.async {
				led.levelIndicator.floatValue = isLit ? 1.0 : 0.0
				led.isLit = isLit
			}
		}
	}
}
