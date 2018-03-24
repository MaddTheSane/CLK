//
//  CSStaticAnalyser.h
//  Clock Signal
//
//  Created by Thomas Harte on 31/08/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <AppKit/NSNib.h>

@class CSMachine;

@interface CSStaticAnalyser : NSObject

- (instancetype)initWithFileAtURL:(NSURL *)url;

@property(nonatomic, readonly) NSNibName optionsPanelNibName;
@property(nonatomic, readonly) NSString *displayName;

@end

@interface CSMediaSet : NSObject

- (instancetype)initWithFileAtURL:(NSURL *)url;
- (void)applyToMachine:(CSMachine *)machine;

@end
