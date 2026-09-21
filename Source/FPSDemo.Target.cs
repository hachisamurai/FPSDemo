// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class FPSDemoTarget : TargetRules
{
	// Target为UBT构建上下文；Shipping保留关键诊断日志，使用独立引擎产物避免改变共享UEGame的编译宏。
	public FPSDemoTarget(TargetInfo Target) : base(Target)
	{
		System.Console.WriteLine("[FPSDemo.Target] Configure packaged logging: Display/Warning/Error only for project categories");
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_4;
		ExtraModuleNames.Add("FPSDemo");
		if (Configuration == UnrealTargetConfiguration.Shipping)
		{
			// UE默认关闭Shipping所有日志；源码引擎独立构建启用后，项目类别仍以Display为编译上限。
			BuildEnvironment = TargetBuildEnvironment.Unique;
			bUseLoggingInShipping = true;
		}
	}
}
