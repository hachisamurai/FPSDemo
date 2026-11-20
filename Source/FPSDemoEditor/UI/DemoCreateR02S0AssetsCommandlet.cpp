// 职责：R02 S0字体、主题与UI氛围资产的幂等作者流程；只创建固定新候选，旧候选仅核验，不覆盖人工修改。
// 使用：独立Editor-Cmd运行DemoCreateR02S0Assets；字体来源/授权/SHA由S0 Fonts/preparation.json冻结。
// 依赖：正式FontFace/CompositeFont/MaterialEditingLibrary/SavePackage，Runtime主题仅保存视觉引用，不参与业务或输入。
#include "UI/DemoCreateR02S0AssetsCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Debug/DemoLog.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Fonts/CompositeFont.h"
#include "HAL/FileManager.h"
#include "MaterialEditingLibrary.h"
#include "MaterialDomain.h" // UE5.4的MD_UI枚举定义；不依赖其他作者工具的Unity隐式包含。
#include "Materials/Material.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UI/R02/DemoUITheme.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace DemoR02S0Authoring
{
    const FString AssetRoot = TEXT("/Game/UI/RedesignR02"); // 唯一可写候选根；不接受命令行覆盖路径。
    const FString FontSourceRoot = TEXT("Art/UI/Redesign20261116/R02_Cinematic/S0/Fonts"); // 已固定原始/静态字体及许可证的项目相对目录。
    const TCHAR* WeightNames[] = { TEXT("Regular"), TEXT("SemiBold") }; // 与Runtime主题公开字重及本次四静态文件一一对应。

    /** Asset必须是当前新建的R02包对象；只保存此包，不调用SaveAll或修改旧资产引用。 */
    bool SaveNewAsset(UObject* Asset)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.SaveNewAsset asset=%s"), *GetNameSafe(Asset));
        if (!Asset || !Asset->GetOutermost()->GetName().StartsWith(AssetRoot + TEXT("/")))
        {
            UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS rejected invalid/out-of-scope asset"));
            return false;
        }
        const FString Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()); // 引擎映射固定包名到项目Content。
        if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true))
        {
            UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS directory creation failed path=%s"), *Filename);
            return false;
        }
        FAssetRegistryModule::AssetCreated(Asset);
        Asset->MarkPackageDirty();
        FSavePackageArgs SaveArgs; // 当前调用只提交一个候选包；同步完成，无异步对象捕获。
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        const bool bSaved = UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, SaveArgs); // 失败不记PASS，已创建源保留供诊断。
        UE_LOG(LogFPSDemo, Log, TEXT("R02_S0_ASSETS save=%d package=%s"), bSaved, *Asset->GetOutermost()->GetName());
        return bSaved;
    }

    /** PackageName固定在R02目录；已存在返回真实对象供只读核验，不改名/删除/重建它。 */
    UObject* FindExisting(const FString& PackageName)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.FindExisting package=%s"), *PackageName);
        const FString Name = FPackageName::GetLongPackageAssetName(PackageName); // 稳定对象短名与包短名一致。
        return LoadObject<UObject>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
    }

    /** 加载返回空不代表磁盘无文件；损坏/无法解析的已有候选必须受保护，不能随后CreatePackage覆盖。 */
    bool HasUnreadablePackage(const FString& PackageName)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.HasUnreadablePackage package=%s"), *PackageName);
        const FString Filename = FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension()); // 只查询固定候选包磁盘存在性。
        const bool bProtected = IFileManager::Get().FileExists(*Filename);
        if (bProtected) UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS unreadable existing package protected path=%s"), *Filename);
        return bProtected;
    }

    /** Family为固定R02SC/R02Latin，Source为已登记静态字库；已有Face必须与原件逐字节一致。 */
    UFontFace* CreateFontFace(const FString& Family, const FString& Source, int32 WeightIndex, bool bInline)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.CreateFontFace family=%s index=%d"), *Family, WeightIndex);
        if (WeightIndex < 0 || WeightIndex >= UE_ARRAY_COUNT(WeightNames))
        {
            UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS font weight index invalid"));
            return nullptr;
        }
        const FString SourcePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / FontSourceRoot / Source); // 仅本次作者读入；Runtime只读包内资源。
        TArray<uint8> Bytes; // 同步导入源副本，FontFaceInitialize复制后由当前函数释放。
        if (!FFileHelper::LoadFileToArray(Bytes, *SourcePath) || Bytes.IsEmpty())
        {
            UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS missing prepared font source=%s"), *SourcePath);
            return nullptr;
        }
        const FString Name = TEXT("FF_") + Family + TEXT("_") + WeightNames[WeightIndex]; // 四个固定新Face名称。
        const FString PackageName = AssetRoot / TEXT("Fonts") / Name;
        if (UObject* Existing = FindExisting(PackageName))
        {
            UFontFace* Face = Cast<UFontFace>(Existing); // 复用前核类型/完整字节/装载策略，避免把旧错误候选当成成功。
            if (!Face || Face->GetFontFaceData()->GetData() != Bytes || Face->LayoutMethod != EFontLayoutMethod::Metrics
                || Face->LoadingPolicy != (bInline ? EFontLoadingPolicy::Inline : EFontLoadingPolicy::LazyLoad))
            {
                UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS existing FontFace differs package=%s"), *PackageName);
                return nullptr;
            }
            UE_LOG(LogFPSDemo, Log, TEXT("R02_S0_ASSETS verified Face package=%s bytes=%d"), *PackageName, Bytes.Num());
            return Face;
        }
        if (HasUnreadablePackage(PackageName)) return nullptr;
        UPackage* Package = CreatePackage(*PackageName); // 新包拥有候选Face，绝不写旧R04包。
        UFontFace* Face = NewObject<UFontFace>(Package, *Name, RF_Public | RF_Standalone);
        Face->InitializeFromBulkData(SourcePath, EFontHinting::Default, Bytes.GetData(), Bytes.Num());
        Face->LoadingPolicy = bInline ? EFontLoadingPolicy::Inline : EFontLoadingPolicy::LazyLoad; // Latin小字库内联，完整中文LazyLoad；实际首次加载成本另测。
        Face->LayoutMethod = EFontLayoutMethod::Metrics;
        return SaveNewAsset(Face) ? Face : nullptr;
    }

    /** Typeface是所属UFont可序列化字段；两字重都是FontFace强资产引用，Cook无需依赖作者机器路径。 */
    void FillTypeface(FTypeface& Typeface, const TArray<UFontFace*>& Faces)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.FillTypeface"));
        Typeface.Fonts.Reset();
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(WeightNames); ++Index)
        {
            FTypefaceEntry& Entry = Typeface.Fonts.AddDefaulted_GetRef(); // 该UFont拥有的数据，无跨函数裸指针缓存。
            Entry.Name = WeightNames[Index];
            Entry.Font = FFontData(Faces[Index]);
        }
    }

    /** 已有Font仅读CacheType、字重/Face引用和Fallback比例，错误候选原样保留并失败。 */
    bool ValidateComposite(const UFont* Font, const TArray<UFontFace*>& DefaultFaces, const TArray<UFontFace*>& FallbackFaces)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.ValidateComposite"));
        if (!Font || Font->FontCacheType != EFontCacheType::Runtime
            || Font->CompositeFont.DefaultTypeface.Fonts.Num() != 2 || Font->CompositeFont.FallbackTypeface.Typeface.Fonts.Num() != 2
            || !FMath::IsNearlyEqual(Font->CompositeFont.FallbackTypeface.ScalingFactor, 1.f)) return false;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(WeightNames); ++Index)
        {
            const FTypefaceEntry& Main = Font->CompositeFont.DefaultTypeface.Fonts[Index]; // 同步借用Font中的真实持久化条目。
            const FTypefaceEntry& Fallback = Font->CompositeFont.FallbackTypeface.Typeface.Fonts[Index];
            if (Main.Name != WeightNames[Index] || Fallback.Name != WeightNames[Index]
                || Main.Font.GetFontFaceAsset() != DefaultFaces[Index] || Fallback.Font.GetFontFaceAsset() != FallbackFaces[Index]) return false;
        }
        return true;
    }

    /** Name只取固定两复合名，Default/Fallback是已成功保存的两字重Face；已有包不重写。 */
    UFont* CreateComposite(const FString& Name, const TArray<UFontFace*>& DefaultFaces, const TArray<UFontFace*>& FallbackFaces)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.CreateComposite name=%s"), *Name);
        const FString PackageName = AssetRoot / TEXT("Fonts") / Name;
        if (UObject* Existing = FindExisting(PackageName))
        {
            UFont* Font = Cast<UFont>(Existing);
            if (!ValidateComposite(Font, DefaultFaces, FallbackFaces))
            {
                UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS existing composite differs package=%s"), *PackageName);
                return nullptr;
            }
            return Font;
        }
        if (HasUnreadablePackage(PackageName)) return nullptr;
        UPackage* Package = CreatePackage(*PackageName);
        UFont* Font = NewObject<UFont>(Package, *Name, RF_Public | RF_Standalone);
        Font->FontCacheType = EFontCacheType::Runtime;
        FillTypeface(Font->CompositeFont.DefaultTypeface, DefaultFaces);
        FillTypeface(Font->CompositeFont.FallbackTypeface.Typeface, FallbackFaces);
        Font->CompositeFont.FallbackTypeface.ScalingFactor = 1.f;
        Font->CompositeFont.MakeDirty();
        return ValidateComposite(Font, DefaultFaces, FallbackFaces) && SaveNewAsset(Font) ? Font : nullptr;
    }

    /** 返回背景自身纹理采样的确定Shader；不能读取/去色背后的实时SceneColor。 */
    FString BackgroundCode()
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.BackgroundCode"));
        return TEXT("float luma=dot(BackgroundRGB,float3(0.2126,0.7152,0.0722));\nreturn lerp(float3(luma,luma,luma),BackgroundRGB,saturate(Saturation))*max(Brightness,0.0);\n");
    }

    /** 返回单UI层的程序颗粒/暗角；TimeSeconds由本机表现层赋值，ReducedMotion时固定值，不创建RenderTarget。 */
    FString AtmosphereCode()
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.AtmosphereCode"));
        // S0R02修正：先在预乘空间合成黑色暗角和独立极小颗粒，再转直通Alpha。
        // 旧稿将暗角Alpha用于白颗粒，导致边缘被放大成盐噪声；保留旧材质，不覆写历史候选。
        return TEXT("float2 pixel=floor(UV*float2(max(ScreenWidth,1.0),max(ScreenHeight,1.0)));\nfloat t=floor(TimeSeconds*24.0);\nfloat n=frac(sin(dot(pixel+float2(t*17.0,t*29.0),float2(12.9898,78.233)))*43758.5453);\nfloat grain=(n-0.5)*saturate(GrainStrength);\nfloat edge=smoothstep(0.12,0.72,length(UV-0.5));\nfloat grainAlpha=abs(grain);\nfloat vignetteAlpha=edge*saturate(VignetteStrength);\nfloat alpha=grainAlpha+vignetteAlpha*(1.0-grainAlpha);\nfloat3 grainColor=grain>0.0?float3(0.55,0.53,0.50):float3(0.0,0.0,0.0);\nfloat3 color=grainColor*grainAlpha/max(alpha,0.00001);\nreturn float4(color,alpha);\n");
    }

    /** 在Material新建一个命名Scalar节点；输入固定名/初值，不读取全局MPC或修改其他材质。 */
    UMaterialExpressionScalarParameter* AddScalar(UMaterial* Material, const FName Name, float DefaultValue, int32 Y)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.AddScalar name=%s"), *Name.ToString());
        UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionScalarParameter::StaticClass(), -700, Y));
        if (Parameter) { Parameter->ParameterName = Name; Parameter->DefaultValue = DefaultValue; }
        else UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS scalar node failed name=%s"), *Name.ToString());
        return Parameter;
    }

    /** 新Custom输入顺序是本次作者合同；调用者确保Nodes非空，连接失败立即返回false。 */
    bool ConnectCustom(UMaterialExpressionCustom* Custom, const TArray<FName>& Names, const TArray<UMaterialExpression*>& Nodes)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.ConnectCustom"));
        if (!Custom || Names.Num() != Nodes.Num()) return false;
        Custom->Inputs.Reset();
        for (const FName Name : Names) { FCustomInput Input; Input.InputName = Name; Custom->Inputs.Add(Input); } // 当前Custom拥有真实连接描述，不缓存节点到命令结束以后。
        for (int32 Index = 0; Index < Names.Num(); ++Index)
        {
            if (!Nodes[Index] || !UMaterialEditingLibrary::ConnectMaterialExpressions(Nodes[Index], TEXT(""), Custom, Names[Index].ToString()))
            {
                UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS custom input failed name=%s"), *Names[Index].ToString());
                return false;
            }
        }
        return true;
    }

    /** 仅读固定材质实际输出/参数，拒绝人工改动或旧错误候选；不借验证修改图。 */
    bool ValidateMaterial(UMaterial* Material, bool bAtmosphere, UTexture2D* Background)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.ValidateMaterial atmosphere=%d"), bAtmosphere);
        if (!Material || Material->MaterialDomain != MD_UI || Material->BlendMode != BLEND_Translucent) return false;
        UMaterialExpression* ColorOutput = UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_EmissiveColor); // 仅读实际连接，不根据节点标签猜存在。
        UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(ColorOutput);
        if (bAtmosphere)
        {
            const UMaterialExpressionComponentMask* Mask = Cast<UMaterialExpressionComponentMask>(ColorOutput);
            const UMaterialExpressionComponentMask* AlphaMask = Cast<UMaterialExpressionComponentMask>(UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_Opacity));
            if (!Mask || !Mask->R || !Mask->G || !Mask->B || Mask->A || !AlphaMask || AlphaMask->R || AlphaMask->G || AlphaMask->B || !AlphaMask->A
                || AlphaMask->Input.Expression != Mask->Input.Expression) return false;
            Custom = Cast<UMaterialExpressionCustom>(Mask->Input.Expression);
        }
        if (!Custom || Custom->Code != (bAtmosphere ? AtmosphereCode() : BackgroundCode()) || Custom->OutputType != (bAtmosphere ? CMOT_Float4 : CMOT_Float3)) return false;
        const TArray<FName> Names = bAtmosphere
            ? TArray<FName>{TEXT("UV"),TEXT("TimeSeconds"),TEXT("GrainStrength"),TEXT("VignetteStrength"),TEXT("ScreenWidth"),TEXT("ScreenHeight")}
            : TArray<FName>{TEXT("BackgroundRGB"),TEXT("Saturation"),TEXT("Brightness")}; // 同一固定输入合同分别核实际节点顺序。
        if (Custom->Inputs.Num() != Names.Num() || UMaterialEditingLibrary::GetNumMaterialExpressions(Material) != (bAtmosphere ? 9 : 5)) return false;
        for (int32 Index = 0; Index < Names.Num(); ++Index)
        {
            if (Custom->Inputs[Index].InputName != Names[Index] || !Custom->Inputs[Index].Input.Expression) return false;
            if (Index == 0)
            {
                if (bAtmosphere)
                {
                    const UMaterialExpressionTextureCoordinate* UV = Cast<UMaterialExpressionTextureCoordinate>(Custom->Inputs[Index].Input.Expression); // 真UV节点，禁止以任意常数冒充全图坐标。
                    if (!UV || UV->CoordinateIndex != 0) return false;
                }
                else
                {
                    const UMaterialExpressionTextureSampleParameter2D* Sample = Cast<UMaterialExpressionTextureSampleParameter2D>(Custom->Inputs[Index].Input.Expression); // 真背景纹理输入，无SceneColor/其他贴图替换。
                    if (!Sample || Sample->ParameterName != TEXT("BackgroundTexture") || Sample->Texture != Background) return false;
                }
            }
            else
            {
                const UMaterialExpressionScalarParameter* Parameter = Cast<UMaterialExpressionScalarParameter>(Custom->Inputs[Index].Input.Expression); // 核实参数正是连入Custom的节点，非孤立同名节点。
                if (!Parameter || Parameter->ParameterName != Names[Index]) return false;
            }
        }
        const TArray<FName> Scalars = bAtmosphere
            ? TArray<FName>{TEXT("TimeSeconds"),TEXT("GrainStrength"),TEXT("VignetteStrength"),TEXT("ScreenWidth"),TEXT("ScreenHeight")}
            : TArray<FName>{TEXT("Saturation"),TEXT("Brightness")};
        const TArray<float> Values = bAtmosphere ? TArray<float>{0.f,.025f,.22f,1920.f,1080.f} : TArray<float>{.12f,.34f}; // 候选材质默认值，样式板MID只改本机参数。
        for (int32 Index = 0; Index < Scalars.Num(); ++Index)
        {
            float Value = 0.f; // 当前只读参数查询输出。
            if (!Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Scalars[Index]), Value) || !FMath::IsNearlyEqual(Value, Values[Index])) return false;
        }
        if (!bAtmosphere)
        {
            UTexture* Texture = nullptr; // 仅核材质对现有背景的引用，不改背景资源。
            if (!Material->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BackgroundTexture")), Texture) || Texture != Background) return false;
            const UMaterialExpressionConstant* Alpha = Cast<UMaterialExpressionConstant>(UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_Opacity));
            if (!Alpha || !FMath::IsNearlyEqual(Alpha->R, 1.f)) return false;
        }
        return true;
    }

    /** 仅创建两个固定UI材质；背景采样自己的纹理，氛围只叠程序颗粒/暗角，没有SceneColor/RenderTarget。 */
    UMaterial* CreateMaterial(bool bAtmosphere, UTexture2D* Background)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.CreateMaterial atmosphere=%d"), bAtmosphere);
        const FString Name = bAtmosphere ? TEXT("M_UI_R02_Atmosphere_S0R02") : TEXT("M_UI_R02_Background"); // 修正新建独立候选，旧噪声材质保留，背景不变。
        const FString PackageName = AssetRoot / TEXT("Materials") / Name;
        if (UObject* Existing = FindExisting(PackageName))
        {
            UMaterial* Material = Cast<UMaterial>(Existing);
            if (!ValidateMaterial(Material, bAtmosphere, Background))
            {
                UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS existing material differs package=%s"), *PackageName);
                return nullptr;
            }
            return Material;
        }
        if (HasUnreadablePackage(PackageName)) return nullptr;
        UPackage* Package = CreatePackage(*PackageName);
        UMaterial* Material = NewObject<UMaterial>(Package, *Name, RF_Public | RF_Standalone);
        Material->MaterialDomain = MD_UI;
        Material->BlendMode = BLEND_Translucent;
        UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionCustom::StaticClass(), -300, 0));
        if (!Custom) { UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS custom node failed")); return nullptr; }
        Custom->OutputType = bAtmosphere ? CMOT_Float4 : CMOT_Float3;
        Custom->Description = bAtmosphere ? TEXT("R02 UI-only grain/vignette; no scene-color reads") : TEXT("R02 own background texture desaturation only");
        Custom->Code = bAtmosphere ? AtmosphereCode() : BackgroundCode();
        if (bAtmosphere)
        {
            UMaterialExpressionTextureCoordinate* UV = Cast<UMaterialExpressionTextureCoordinate>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureCoordinate::StaticClass(), -700, -180)); // 全图UV，最终像素噪声由ScreenWidth/Height给定。
            const TArray<UMaterialExpression*> Nodes = { UV, AddScalar(Material,TEXT("TimeSeconds"),0.f,-80), AddScalar(Material,TEXT("GrainStrength"),.025f,30), AddScalar(Material,TEXT("VignetteStrength"),.22f,140), AddScalar(Material,TEXT("ScreenWidth"),1920.f,250), AddScalar(Material,TEXT("ScreenHeight"),1080.f,360) };
            if (!ConnectCustom(Custom, {TEXT("UV"),TEXT("TimeSeconds"),TEXT("GrainStrength"),TEXT("VignetteStrength"),TEXT("ScreenWidth"),TEXT("ScreenHeight")}, Nodes)) return nullptr;
            UMaterialExpressionComponentMask* RGB = Cast<UMaterialExpressionComponentMask>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), 0, -60));
            UMaterialExpressionComponentMask* Alpha = Cast<UMaterialExpressionComponentMask>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionComponentMask::StaticClass(), 0, 100));
            if (!RGB || !Alpha) { UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS mask nodes failed")); return nullptr; }
            RGB->R = RGB->G = RGB->B = true; RGB->A = false;
            Alpha->R = Alpha->G = Alpha->B = false; Alpha->A = true;
            if (!UMaterialEditingLibrary::ConnectMaterialExpressions(Custom,TEXT(""),RGB,TEXT("")) || !UMaterialEditingLibrary::ConnectMaterialExpressions(Custom,TEXT(""),Alpha,TEXT(""))
                || !UMaterialEditingLibrary::ConnectMaterialProperty(RGB,TEXT(""),MP_EmissiveColor) || !UMaterialEditingLibrary::ConnectMaterialProperty(Alpha,TEXT(""),MP_Opacity)) return nullptr;
        }
        else
        {
            UMaterialExpressionTextureSampleParameter2D* Texture = Cast<UMaterialExpressionTextureSampleParameter2D>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -700, -150));
            UMaterialExpressionConstant* Alpha = Cast<UMaterialExpressionConstant>(UMaterialEditingLibrary::CreateMaterialExpression(Material, UMaterialExpressionConstant::StaticClass(), 0, 180));
            if (!Texture || !Alpha || !Background) { UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS background input missing")); return nullptr; }
            Texture->ParameterName = TEXT("BackgroundTexture"); Texture->Texture = Background; Texture->SamplerType = SAMPLERTYPE_Color;
            Alpha->R = 1.f;
            if (!ConnectCustom(Custom,{TEXT("BackgroundRGB"),TEXT("Saturation"),TEXT("Brightness")},{Texture,AddScalar(Material,TEXT("Saturation"),.12f,40),AddScalar(Material,TEXT("Brightness"),.34f,160)})
                || !UMaterialEditingLibrary::ConnectMaterialProperty(Custom,TEXT(""),MP_EmissiveColor) || !UMaterialEditingLibrary::ConnectMaterialProperty(Alpha,TEXT(""),MP_Opacity)) return nullptr;
        }
        UMaterialEditingLibrary::RecompileMaterial(Material); // 编译真实UI材质图，不以HLSL字符串存在冒充可渲染。
        if (!ValidateMaterial(Material, bAtmosphere, Background)) { UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS created material audit failed name=%s"), *Name); return nullptr; }
        return SaveNewAsset(Material) ? Material : nullptr;
    }

    /** Theme只持久化本批两字体、既有两Sans与氛围引用；已有资产字段不一致即拒绝，防覆盖美术调优。 */
    bool CreateTheme(UFont* Latin, UFont* Chinese, UMaterial* Atmosphere)
    {
        UE_LOG(LogFPSDemo, Log, TEXT("[CALL] DemoR02S0Authoring.CreateTheme"));
        UFont* Label = LoadObject<UFont>(nullptr,TEXT("/Game/UI/RedesignR04/Fonts/F_BreachText.F_BreachText")); // 既有Sans只读取，不改旧资源。
        UFont* Display = LoadObject<UFont>(nullptr,TEXT("/Game/UI/RedesignR04/Fonts/F_BreachDisplay.F_BreachDisplay"));
        if (!Latin || !Chinese || !Atmosphere || !Label || !Display) { UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS theme dependency missing")); return false; }
        const FString Name = TEXT("DA_R02CinemaTheme_S0R02"); // 引用修正氛围的新候选Theme，不覆盖原S0样式失败证据。
        const FString PackageName = AssetRoot / TEXT("Theme") / Name;
        if (UObject* Existing = FindExisting(PackageName))
        {
            const UDemoUITheme* Theme = Cast<UDemoUITheme>(Existing);
            const bool bValid = Theme && Theme->SerifLatinFont == Latin && Theme->SerifChineseFont == Chinese
                && Theme->LabelFont == Label && Theme->LabelDisplayFont == Display && Theme->AtmosphereMaterial == Atmosphere
                && Theme->Base.Equals(FLinearColor(FColor(13,12,11))) && Theme->Ink.Equals(FLinearColor(FColor(233,227,215)))
                && Theme->Muted.Equals(FLinearColor(FColor(110,115,119))) && Theme->Ember.Equals(FLinearColor(FColor(180,71,47)))
                && Theme->Disabled.Equals(FLinearColor(FColor(233,227,215)).CopyWithNewOpacity(.32f))
                && FMath::IsNearlyEqual(Theme->HairlineOpacity,.23f) && FMath::IsNearlyEqual(Theme->ModalOpacity,.82f)
                && FMath::IsNearlyEqual(Theme->GrainStrength,.025f) && FMath::IsNearlyEqual(Theme->VignetteStrength,.22f)
                && FMath::IsNearlyEqual(Theme->LetterboxAspect,2.39f) && FMath::IsNearlyEqual(Theme->BossLinePhysicalPixels,2.f);
            if (!bValid) UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS existing theme differs; retained without edits"));
            return bValid;
        }
        if (HasUnreadablePackage(PackageName)) return false;
        UPackage* Package = CreatePackage(*PackageName);
        UDemoUITheme* Theme = NewObject<UDemoUITheme>(Package,*Name,RF_Public|RF_Standalone);
        Theme->SerifLatinFont = Latin;
        Theme->SerifChineseFont = Chinese;
        Theme->LabelFont = Label;
        Theme->LabelDisplayFont = Display;
        Theme->AtmosphereMaterial = Atmosphere;
        return SaveNewAsset(Theme);
    }
}

UDemoCreateR02S0AssetsCommandlet::UDemoCreateR02S0AssetsCommandlet()
{
    DEMO_LOG_CALL();
    IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true;
}

int32 UDemoCreateR02S0AssetsCommandlet::Main(const FString& Params)
{
    DEMO_LOG_CALL();
    UE_LOG(LogFPSDemo, Log, TEXT("R02_S0_ASSETS begin; Params ignored, fixed new candidate root only"));
    TArray<UFontFace*> Latin, Chinese; // 命令同步创建，UFont随后建立持久化强引用；无异步/跨线程借用。
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(DemoR02S0Authoring::WeightNames); ++Index)
    {
        const FString Weight = DemoR02S0Authoring::WeightNames[Index];
        Chinese.Add(DemoR02S0Authoring::CreateFontFace(TEXT("R02SC"),TEXT("NotoSerifSC/BreachR02SerifSC-")+Weight+TEXT(".ttf"),Index,false));
        Latin.Add(DemoR02S0Authoring::CreateFontFace(TEXT("R02Latin"),TEXT("EBGaramond/BreachR02SerifLatin-")+Weight+TEXT(".ttf"),Index,true));
        if (!Chinese.Last() || !Latin.Last()) { UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS font source/import failure")); return 1; }
    }
    UFont* Serif = DemoR02S0Authoring::CreateComposite(TEXT("F_R02Serif"),Latin,Chinese); // Latin数字/罗马数优先，中文fallback。
    UFont* SerifText = DemoR02S0Authoring::CreateComposite(TEXT("F_R02SerifText"),Chinese,Latin); // 中文标题优先，两复合权重名称一致。
    UTexture2D* Background = LoadObject<UTexture2D>(nullptr,TEXT("/Game/UI/RedesignR04/Textures/T_LobbyBackground.T_LobbyBackground")); // 当前正式背景素材仅借用。
    if (!Serif || !SerifText || !Background) { UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS composite/background dependency failure")); return 1; }
    UMaterial* BackgroundMaterial = DemoR02S0Authoring::CreateMaterial(false,Background);
    UMaterial* AtmosphereMaterial = DemoR02S0Authoring::CreateMaterial(true,Background);
    const bool bSucceeded = BackgroundMaterial && AtmosphereMaterial && DemoR02S0Authoring::CreateTheme(Serif,SerifText,AtmosphereMaterial);
    if (bSucceeded)
    {
        UE_LOG(LogFPSDemo, Display, TEXT("R02_S0_ASSETS PASS: 4 FontFaces, 2 composite fonts, 2 UI materials, 1 Theme; S0 authoring/readback only, no S1/gameplay acceptance"));
    }
    else
    {
        UE_LOG(LogFPSDemo, Error, TEXT("R02_S0_ASSETS FAIL: partial new candidates retained; no old asset replaced"));
    }
    return bSucceeded ? 0 : 1;
}
