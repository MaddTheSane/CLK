//
//  CSAppleII.h
//  Clock Signal
//
//  Created by Thomas Harte on 07/06/2021.
//  Copyright © 2021 Thomas Harte. All rights reserved.
//

#import <Foundation/Foundation.h>
@class CSMachine;

@interface CSAppleII : NSObject

- (instancetype)initWithAppleII:(void *)appleII owner:(CSMachine *)machine;

@property (nonatomic, assign) BOOL useSquarePixels;

@end
