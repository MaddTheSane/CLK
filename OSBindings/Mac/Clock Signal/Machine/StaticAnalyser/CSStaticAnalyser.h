//
//  CSStaticAnalyser.h
//  Clock Signal
//
//  Created by Thomas Harte on 31/08/2016.
//  Copyright 2016 Thomas Harte. All rights reserved.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@class CSMachine;

typedef NS_ENUM(NSInteger, CSMachineAppleIIModel) {
	CSMachineAppleIIModelAppleII,
	CSMachineAppleIIModelAppleIIPlus,
	CSMachineAppleIIModelAppleIIe,
	CSMachineAppleIIModelAppleEnhancedIIe
} NS_SWIFT_NAME(CSMachine.AppleIIModel);

typedef NS_ENUM(NSInteger, CSMachineAppleIIDiskController) {
	CSMachineAppleIIDiskControllerNone,
	CSMachineAppleIIDiskControllerSixteenSector,
	CSMachineAppleIIDiskControllerThirteenSector
} NS_SWIFT_NAME(CSMachine.AppleIIDiskController);

typedef NS_ENUM(NSInteger, CSMachineAppleIIgsModel) {
	CSMachineAppleIIgsModelROM00,
	CSMachineAppleIIgsModelROM01,
	CSMachineAppleIIgsModelROM03,
} NS_SWIFT_NAME(CSMachine.AppleIIgsModel);

typedef NS_ENUM(NSInteger, CSMachineAtariSTModel) {
	CSMachineAtariSTModel512k,
} NS_SWIFT_NAME(CSMachine.AtariSTModel);

typedef NS_ENUM(NSInteger, CSMachineCPCModel) {
	CSMachineCPCModel464,
	CSMachineCPCModel664,
	CSMachineCPCModel6128
} NS_SWIFT_NAME(CSMachine.CPCModel);

typedef NS_ENUM(NSInteger, CSMachineEnterpriseModel) {
	CSMachineEnterpriseModel64,
	CSMachineEnterpriseModel128,
	CSMachineEnterpriseModel256,
} NS_SWIFT_NAME(CSMachine.EnterpriseModel);

typedef NS_ENUM(NSInteger, CSMachineEnterpriseSpeed) {
	CSMachineEnterpriseSpeed4MHz,
	CSMachineEnterpriseSpeed6MHz
} NS_SWIFT_NAME(CSMachine.EnterpriseSpeed);

typedef NS_ENUM(NSInteger, CSMachineEnterpriseEXOS) {
	CSMachineEnterpriseEXOSVersion21,
	CSMachineEnterpriseEXOSVersion20,
	CSMachineEnterpriseEXOSVersion10,
} NS_SWIFT_NAME(CSMachine.EnterpriseEXOS);

typedef NS_ENUM(NSInteger, CSMachineEnterpriseBASIC) {
	CSMachineEnterpriseBASICVersion21,
	CSMachineEnterpriseBASICVersion11,
	CSMachineEnterpriseBASICVersion10,
	CSMachineEnterpriseBASICNone,
} NS_SWIFT_NAME(CSMachine.EnterpriseBASIC);

typedef NS_ENUM(NSInteger, CSMachineEnterpriseDOS) {
	CSMachineEnterpriseDOSEXDOS,
	CSMachineEnterpriseDOSNone,
} NS_SWIFT_NAME(CSMachine.EnterpriseDOS);

typedef NS_ENUM(NSInteger, CSMachineMacintoshModel) {
	CSMachineMacintoshModel128k,
	CSMachineMacintoshModel512k,
	CSMachineMacintoshModel512ke,
	CSMachineMacintoshModelPlus,
} NS_SWIFT_NAME(CSMachine.MacintoshModel);

typedef NS_ENUM(NSInteger, CSMachineOricModel) {
	CSMachineOricModelOric1,
	CSMachineOricModelOricAtmos,
	CSMachineOricModelPravetz
} NS_SWIFT_NAME(CSMachine.OricModel);

typedef NS_ENUM(NSInteger, CSMachineOricDiskInterface) {
	CSMachineOricDiskInterfaceNone,
	CSMachineOricDiskInterfaceMicrodisc,
	CSMachineOricDiskInterfacePravetz,
	CSMachineOricDiskInterfaceJasmin,
	CSMachineOricDiskInterfaceBD500
} NS_SWIFT_NAME(CSMachine.OricDiskInterface);

typedef NS_ENUM(NSInteger, CSMachineSpectrumModel) {
	CSMachineSpectrumModelSixteenK,
	CSMachineSpectrumModelFortyEightK,
	CSMachineSpectrumModelOneTwoEightK,
	CSMachineSpectrumModelPlus2,
	CSMachineSpectrumModelPlus2a,
	CSMachineSpectrumModelPlus3,
} NS_SWIFT_NAME(CSMachine.SpectrumModel);

typedef NS_ENUM(NSInteger, CSMachineVic20Region) {
	CSMachineVic20RegionAmerican,
	CSMachineVic20RegionEuropean,
	CSMachineVic20RegionDanish,
	CSMachineVic20RegionSwedish,
	CSMachineVic20RegionJapanese,
} NS_SWIFT_NAME(CSMachine.Vic20Region);

typedef NS_ENUM(NSInteger, CSMachineMSXRegion) {
	CSMachineMSXRegionAmerican,
	CSMachineMSXRegionEuropean,
	CSMachineMSXRegionJapanese,
} NS_SWIFT_NAME(CSMachine.MSXRegion);

typedef int Kilobytes;

@interface CSStaticAnalyser : NSObject

- (nullable instancetype)initWithFileAtURL:(NSURL *)url;

- (instancetype)initWithAmstradCPCModel:(CSMachineCPCModel)model;
- (instancetype)initWithAppleIIModel:(CSMachineAppleIIModel)model diskController:(CSMachineAppleIIDiskController)diskController;
- (instancetype)initWithAppleIIgsModel:(CSMachineAppleIIgsModel)model memorySize:(Kilobytes)memorySize;
- (instancetype)initWithAtariSTModel:(CSMachineAtariSTModel)model;
- (instancetype)initWithElectronDFS:(BOOL)dfs adfs:(BOOL)adfs ap6:(BOOL)ap6 sidewaysRAM:(BOOL)sidewaysRAM;
- (instancetype)initWithEnterpriseModel:(CSMachineEnterpriseModel)model speed:(CSMachineEnterpriseSpeed)speed exosVersion:(CSMachineEnterpriseEXOS)exosVersion basicVersion:(CSMachineEnterpriseBASIC)basicVersion dos:(CSMachineEnterpriseDOS)dos;
- (instancetype)initWithMacintoshModel:(CSMachineMacintoshModel)model;
- (instancetype)initWithMSXRegion:(CSMachineMSXRegion)region hasDiskDrive:(BOOL)hasDiskDrive;
- (instancetype)initWithOricModel:(CSMachineOricModel)model diskInterface:(CSMachineOricDiskInterface)diskInterface;
- (instancetype)initWithSpectrumModel:(CSMachineSpectrumModel)model;
- (instancetype)initWithVic20Region:(CSMachineVic20Region)region memorySize:(Kilobytes)memorySize hasC1540:(BOOL)hasC1540;
- (instancetype)initWithZX80MemorySize:(Kilobytes)memorySize useZX81ROM:(BOOL)useZX81ROM;
- (instancetype)initWithZX81MemorySize:(Kilobytes)memorySize;

@property(nonatomic, readonly, nullable) NSString *optionsNibName;
@property(nonatomic, readonly) NSString *displayName;

@end

@interface CSMediaSet : NSObject

- (instancetype)initWithFileAtURL:(NSURL *)url;
- (void)applyToMachine:(CSMachine *)machine;

@property(nonatomic, readonly) BOOL empty;

@end

NS_ASSUME_NONNULL_END
