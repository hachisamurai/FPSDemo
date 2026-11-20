// 职责：R04原始FontFace/静态中文字重、UFont复合字体、背景及产品图导入与只读复用验证。
// 使用：运行prepare_r04_fonts.py后，Editor-Cmd '-run=FPSDemoEditor.DemoCreateR04Assets'；旧UI/WBP不改。
// 依赖：UnrealEd TextureFactory、Engine FontFace/Font、SlateCore CompositeFont、正式SavePackage。
#include "UI/DemoCreateR04AssetsCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Debug/DemoLog.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Factories/TextureFactory.h"
#include "Fonts/CompositeFont.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace DemoR04Authoring
{
    const FString AssetRoot=TEXT("/Game/UI/RedesignR04"); // 固定候选命名空间，不接受外部输入扩大写入范围。

    /** Asset为当前新建对象；只保存其所在固定R04包，不保存或修改其他打开的包。 */
    bool SaveNewAsset(UObject* Asset)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoR04Authoring.SaveNewAsset %s"),*GetNameSafe(Asset));
        if(!Asset||!Asset->GetOutermost()->GetName().StartsWith(AssetRoot+TEXT("/")))
        { UE_LOG(LogFPSDemo,Error,TEXT("UIR_R04 rejected invalid asset or out-of-scope package")); return false; }
        const FString Filename=FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension()); // 引擎负责路径映射与uasset序列化。
        if(!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true))
        { UE_LOG(LogFPSDemo,Error,TEXT("UIR_R04 cannot create asset directory %s"),*Filename); return false; }
        FAssetRegistryModule::AssetCreated(Asset);
        Asset->MarkPackageDirty();
        FSavePackageArgs SaveArgs; // 只提交当前新建包，不触碰旧资产引用或Designer布局。
        SaveArgs.TopLevelFlags=RF_Public|RF_Standalone;
        SaveArgs.SaveFlags=SAVE_NoError;
        const bool bSaved=UPackage::SavePackage(Asset->GetOutermost(),Asset,*Filename,SaveArgs); // 保存失败返回非零命令结果。
        if(bSaved) { UE_LOG(LogFPSDemo,Log,TEXT("UIR_R04 asset save PASS path=%s"),*Filename); }
        else { UE_LOG(LogFPSDemo,Error,TEXT("UIR_R04 asset save FAIL path=%s"),*Filename); }
        return bSaved;
    }

    /** Name固定资产短名，Source相对项目根；新建只用官方FontFace API复制原字节，已有资产必须字节一致。 */
    UFontFace* CreateFontFace(const FString& Name,const FString& Source,bool bInline)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoR04Authoring.CreateFontFace %s"),*Name);
        const FString SourcePath=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/Source); // 作者源路径仅供导入；运行时读取包内字体，不访问此路径。
        TArray<uint8> Bytes; // 当前字体原始字节，导入完成后释放本地副本。
        if(!FFileHelper::LoadFileToArray(Bytes,*SourcePath)||Bytes.IsEmpty())
        { UE_LOG(LogFPSDemo,Error,TEXT("UIR_R04 missing font source %s; run prepare_r04_fonts.py"),*SourcePath); return nullptr; }
        const FString PackageName=AssetRoot/TEXT("Fonts")/Name; // 不修改旧/Game/UI资产。
        UObject* Existing=LoadObject<UObject>(nullptr,*(PackageName+TEXT(".")+Name),nullptr,LOAD_NoWarn|LOAD_Quiet); // 幂等只读检查，不默认覆盖候选。
        if(Existing)
        {
            UFontFace* Face=Cast<UFontFace>(Existing); // 错误类或不同字节均拒绝，避免悄悄替换人工资源。
            if(!Face||Face->GetFontFaceData()->GetData()!=Bytes)
            { UE_LOG(LogFPSDemo,Error,TEXT("UIR_R04 existing font differs from source: %s"),*PackageName); return nullptr; }
            UE_LOG(LogFPSDemo,Log,TEXT("UIR_R04 reused verified font %s bytes=%d"),*PackageName,Bytes.Num());
            return Face;
        }
        UPackage* Package=CreatePackage(*PackageName); // 新包由引擎持有，保存失败不会误标完成。
        UFontFace* Face=NewObject<UFontFace>(Package,*Name,RF_Public|RF_Standalone); // 不手写uasset或伪造FontData。
        Face->InitializeFromBulkData(SourcePath,EFontHinting::Default,Bytes.GetData(),Bytes.Num());
        Face->LoadingPolicy=bInline?EFontLoadingPolicy::Inline:EFontLoadingPolicy::LazyLoad; // 拉丁小字库Inline；中文全字库沿引擎LazyLoad，不承诺无初次解析成本。
        Face->LayoutMethod=EFontLayoutMethod::Metrics;
        return SaveNewAsset(Face)?Face:nullptr;
    }

    /** Typeface由所属UFont拥有，Faces顺序为Regular/Medium/SemiBold；不设置伪粗体或系统路径。 */
    void FillTypeface(FTypeface& Typeface,const TArray<UFontFace*>& Faces)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoR04Authoring.FillTypeface"));
        static const FName Names[]={TEXT("Regular"),TEXT("Medium"),TEXT("SemiBold")}; // 字重名称与运行时接口一致。
        Typeface.Fonts.Reset();
        for(int32 Index=0;Index<3;++Index)
        {
            FTypefaceEntry& Entry=Typeface.Fonts.AddDefaulted_GetRef(); // 持久化FFontData指向FontFace UObject，Cook可遍历依赖。
            Entry.Name=Names[Index]; Entry.Font=FFontData(Faces[Index]);
        }
    }

    /** Font是已有候选，Default/Fallback为当前静态实例；只验引用、字重与CacheType，不覆盖人工调整。 */
    bool ValidateComposite(const UFont* Font,const TArray<UFontFace*>& Default,const TArray<UFontFace*>& Fallback)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoR04Authoring.ValidateComposite"));
        if(!Font||Font->FontCacheType!=EFontCacheType::Runtime||Font->CompositeFont.DefaultTypeface.Fonts.Num()!=3||Font->CompositeFont.FallbackTypeface.Typeface.Fonts.Num()!=3) return false;
        static const FName Names[]={TEXT("Regular"),TEXT("Medium"),TEXT("SemiBold")}; // 完整三字重缺一即失败。
        for(int32 Index=0;Index<3;++Index)
        {
            const FTypefaceEntry& Main=Font->CompositeFont.DefaultTypeface.Fonts[Index]; // 本函数短期借用。
            const FTypefaceEntry& Backup=Font->CompositeFont.FallbackTypeface.Typeface.Fonts[Index]; // 字符回退也必须对应同字重。
            if(Main.Name!=Names[Index]||Main.Font.GetFontFaceAsset()!=Default[Index]||Backup.Name!=Names[Index]||Backup.Font.GetFontFaceAsset()!=Fallback[Index]) return false;
        }
        return true;
    }

    /** Name是固定复合字体名；Default/Fallback是已保存FontFace，创建UFont后保存持久化引用，不改全局CoreStyle。 */
    bool CreateComposite(const FString& Name,const TArray<UFontFace*>& Default,const TArray<UFontFace*>& Fallback)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoR04Authoring.CreateComposite %s"),*Name);
        const FString PackageName=AssetRoot/TEXT("Fonts")/Name; // 独立候选包，旧Roboto/WBP仍可回退。
        if(UObject* Existing=LoadObject<UObject>(nullptr,*(PackageName+TEXT(".")+Name),nullptr,LOAD_NoWarn|LOAD_Quiet))
        {
            const bool bValid=ValidateComposite(Cast<UFont>(Existing),Default,Fallback); // 已有候选只读，不借重跑覆盖美术修改。
            if(!bValid) UE_LOG(LogFPSDemo,Error,TEXT("UIR_R04 existing composite mismatch %s"),*PackageName);
            return bValid;
        }
        UPackage* Package=CreatePackage(*PackageName); // 生成资产生命周期归包。
        UFont* Font=NewObject<UFont>(Package,*Name,RF_Public|RF_Standalone); // 可序列化FontObject供UMG/Canvas同时引用。
        Font->FontCacheType=EFontCacheType::Runtime;
        FillTypeface(Font->CompositeFont.DefaultTypeface,Default);
        FillTypeface(Font->CompositeFont.FallbackTypeface.Typeface,Fallback);
        Font->CompositeFont.FallbackTypeface.ScalingFactor=1.f;
        Font->CompositeFont.MakeDirty();
        return ValidateComposite(Font,Default,Fallback)&&SaveNewAsset(Font);
    }

    /** Name为固定贴图名，Source相对设计目录；已有候选只验证类型/尺寸，新建PNG经TextureFactory保留Alpha。 */
    bool CreateTexture(const FString& Name,const FString& Source,int32 ExpectedWidth,int32 ExpectedHeight)
    {
        UE_LOG(LogFPSDemo,Log,TEXT("[CALL] DemoR04Authoring.CreateTexture %s"),*Name);
        const FString PackageName=AssetRoot/TEXT("Textures")/Name; // 与字体共用唯一版本目录。
        if(UObject* Existing=LoadObject<UObject>(nullptr,*(PackageName+TEXT(".")+Name),nullptr,LOAD_NoWarn|LOAD_Quiet))
        {
            UTexture2D* Texture=Cast<UTexture2D>(Existing); // 仅核对来源导出尺寸；不默默覆盖现有材质/压缩调优。
            const bool bValid=Texture&&Texture->Source.GetSizeX()==ExpectedWidth&&Texture->Source.GetSizeY()==ExpectedHeight;
            if(!bValid) UE_LOG(LogFPSDemo,Error,TEXT("UIR_R04 existing texture type or size mismatch %s"),*PackageName);
            return bValid;
        }
        const FString SourcePath=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Art/UI/Redesign20261106")/Source); // 原PNG不包含按钮文字。
        TArray<uint8> Bytes; // 当前PNG压缩源，经正式工厂解码为Texture Source。
        if(!FFileHelper::LoadFileToArray(Bytes,*SourcePath)||Bytes.IsEmpty())
        { UE_LOG(LogFPSDemo,Error,TEXT("UIR_R04 texture source missing %s"),*SourcePath); return false; }
        UPackage* Package=CreatePackage(*PackageName); // 新包不替换旧背景纹理。
        UTextureFactory* Factory=NewObject<UTextureFactory>(); // 仅此同步导入期间使用，无异步回调。
        Factory->bCreateMaterial=false; Factory->LODGroup=TEXTUREGROUP_UI; Factory->CompressionSettings=TC_EditorIcon;
        const uint8* Begin=Bytes.GetData(); // 工厂可推进读取指针，源数组在整个调用期间保持有效。
        UTexture2D* Texture=Cast<UTexture2D>(Factory->FactoryCreateBinary(UTexture2D::StaticClass(),Package,*Name,RF_Public|RF_Standalone,nullptr,TEXT("png"),Begin,Begin+Bytes.Num(),GWarn)); // 工厂创建真实引擎纹理。
        if(!Texture||Texture->Source.GetSizeX()!=ExpectedWidth||Texture->Source.GetSizeY()!=ExpectedHeight)
        { UE_LOG(LogFPSDemo,Error,TEXT("UIR_R04 texture decode or dimensions failed %s"),*Name); return false; }
        Texture->LODGroup=TEXTUREGROUP_UI;
        Texture->SRGB=true;
        Texture->MipGenSettings=TMGS_NoMipmaps; // UI候选保持母版边缘，后续性能/Cook另验。
        Texture->NeverStream=true;
        Texture->CompressionSettings=TC_EditorIcon;
        Texture->PostEditChange();
        return SaveNewAsset(Texture);
    }
}

UDemoCreateR04AssetsCommandlet::UDemoCreateR04AssetsCommandlet()
{
    DEMO_LOG_CALL();
    IsClient=false; IsServer=false; IsEditor=true; LogToConsole=true;
}

int32 UDemoCreateR04AssetsCommandlet::Main(const FString& Params)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo,Log,TEXT("UIR_R04 asset creation started; Params ignored; writes restricted to /Game/UI/RedesignR04"));
    TArray<UFontFace*> Barlow,Noto; // 命令生命周期短期强可达资产，保存到包后由UFont建立持久化引用。
    static const TCHAR* Weights[]={TEXT("Regular"),TEXT("Medium"),TEXT("SemiBold")}; // 顺序对应运行时字体API。
    for(const TCHAR* Weight:Weights)
    {
        Barlow.Add(DemoR04Authoring::CreateFontFace(FString(TEXT("FF_Barlow_"))+Weight,FString(TEXT("SourceAssets/UI/RedesignR04/Fonts/BarlowCondensed/BarlowCondensed-"))+Weight+TEXT(".ttf"),true));
        Noto.Add(DemoR04Authoring::CreateFontFace(FString(TEXT("FF_NotoSC_"))+Weight,FString(TEXT("SourceAssets/UI/RedesignR04/Fonts/NotoSansSC/BreachUISc-"))+Weight+TEXT(".ttf"),false));
        if(!Barlow.Last()||!Noto.Last()) { UE_LOG(LogFPSDemo,Error,TEXT("UIR_R04 font import failed; stopping before composite creation")); return 1; }
    }
    bool bSucceeded=DemoR04Authoring::CreateComposite(TEXT("F_BreachText"),Noto,Noto);
    bSucceeded&=DemoR04Authoring::CreateComposite(TEXT("F_BreachDisplay"),Barlow,Noto);
    bSucceeded&=DemoR04Authoring::CreateTexture(TEXT("T_LobbyBackground"),TEXT("Source/KeyArt-Lobby.png"),1672,941);
    for(const TCHAR* Weapon:{TEXT("Pistol"),TEXT("Rifle"),TEXT("Shotgun"),TEXT("Sniper")})
        bSucceeded&=DemoR04Authoring::CreateTexture(FString(TEXT("T_Product_"))+Weapon,FString(TEXT("Assets/"))+Weapon+TEXT(".png"),1280,720);
    if(bSucceeded) { UE_LOG(LogFPSDemo,Display,TEXT("UIR_R04_ASSETS PASS: 6 static FontFaces, 2 persistent composite fonts, 5 textures; no runtime/gameplay acceptance claimed")); }
    else { UE_LOG(LogFPSDemo,Error,TEXT("UIR_R04_ASSETS FAIL: see import/save errors; partial candidate assets retained, no old assets replaced")); }
    return bSucceeded?0:1;
}
