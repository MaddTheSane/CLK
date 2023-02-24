//
//  CSZX8081.h
//  Clock Signal
//
//  Created by Thomas Harte on 04/06/2017.
//  Copyright 2017 Thomas Harte. All rights reserved.
//

#import <Foundation/Foundation.h>
@class CSMachine;

@interface CSZX8081 : NSObject

- (instancetype)initWithZX8081:(void *)zx8081 owner:(CSMachine *)machine;

@property (nonatomic, assign) BOOL tapeIsPlaying;

@end
