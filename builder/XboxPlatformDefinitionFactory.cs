using helengine.baseplatform.Definitions;
using helengine.baseplatform.Profiles;

namespace helengine.xbox.builder;

/// <summary>Creates the editor-facing Original Xbox platform contract.</summary>
public static class XboxPlatformDefinitionFactory {
    // The legacy runtime disables shader and text passes. The opted-in shader runtime supports shared
    // text batching, while debug overlay remains unavailable in both profiles.
    // render2d and sprites are deliberately left enabled, matching N64: forcing render2d off also strips the
    // std types the generated Core is built on.
    const string ForcedDisabledFeatures = "debug_overlay;shaders;text2d";

    /// <summary>Codegen option id selecting <c>std::string</c> as the generated Core's string storage.</summary>
    public const string UseStdStringSettingId = "codegen-use-std-string";

    /// <summary>Codegen option id selecting <c>std::vector</c> as the generated Core's dynamic list storage.</summary>
    public const string UseStdVectorSettingId = "codegen-use-std-vector";

    /// <summary>Codegen option id selecting <c>std::unordered_map</c> as the generated Core's dictionary storage.</summary>
    public const string UseStdUnorderedMapSettingId = "codegen-use-std-unordered-map";

    /// <summary>Codegen option id selecting <c>std::unordered_set</c> as the generated Core's hash-set storage.</summary>
    public const string UseStdUnorderedSetSettingId = "codegen-use-std-unordered-set";

    /// <summary>Codegen option id selecting <c>std::function</c> as the generated Core's delegate storage.</summary>
    public const string UseStdFunctionSettingId = "codegen-use-std-function";

    /// <summary>Codegen option id selecting <c>std::chrono</c> as the generated Core's monotonic clock source.</summary>
    public const string UseStdChronoSettingId = "codegen-use-std-chrono";

    /// <summary>Codegen option id selecting <c>std::shared_ptr</c> as the generated Core's shared span ownership.</summary>
    public const string UseStdSharedPtrSettingId = "codegen-use-std-shared-ptr";

    /// <summary>Codegen option id selecting the standard C++ math library for the generated Core's numeric helpers.</summary>
    public const string UseStdMathSettingId = "codegen-use-std-math";

    /// <summary>Codegen option id selecting the hosted file-stream implementation.</summary>
    public const string UseHostedFileSystemSettingId = "codegen-use-hosted-file-system";

    /// <summary>Codegen option id selecting C++ exception unwinding for the generated Core.</summary>
    public const string UseExceptionsSettingId = "codegen-use-exceptions";

    /// <summary>Codegen option id selecting compiler RTTI for the generated Core's casts and generic dispatch.</summary>
    public const string UseRttiSettingId = "codegen-use-rtti";

    /// <summary>Creates either the unchanged legacy contract or the explicitly opted-in experimental shader runtime contract.</summary>
    public static PlatformDefinition Create(bool compiledShaders = false) {
        string graphicsProfile = compiledShaders ? "xbox-shader" : XboxMaterialSchemaIds.StandardGraphicsProfileId;
        PlatformSettingDefinition[] buildSettings = [
            new("game-name", "Game Name", PlatformSettingKind.Text, "helengine", true, []),
            new("enable-native-runtime-diagnostics", "Enable Native Runtime Diagnostics", PlatformSettingKind.Boolean, "false", true, [])
        ];

        PlatformCodegenProfileDefinition codegen = new(
            "default",
            "Default",
            "Original Xbox C# to C++ generated runtime profile",
            PlatformCodegenLanguage.Cpp,
            PlatformSerializationEndianness.LittleEndian,
            [
                new(PlatformCodegenSettingIds.ForcedDisabledFeatures, "Forced Disabled Features", PlatformSettingKind.Text,
                    compiledShaders ? "debug_overlay" : ForcedDisabledFeatures, true, []),
                // The native renderer builds its clip matrix as Projection * View * Model against column
                // vectors, matching the engine row-vector convention used by the desktop and GameCube targets.
                new("generated-math-convention", "Generated Math Convention", PlatformSettingKind.Text, "engine-row-vector", true, []),
                // 32-bit x86.
                new("pointer-size-bytes", "Pointer Size (Bytes)", PlatformSettingKind.Text, "4", true, []),
                new("type-remaps", "Type Remaps", PlatformSettingKind.Text, "System.Numerics.Vector2=helengine.float2;System.Numerics.Vector3=helengine.float3;System.Numerics.Vector4=helengine.float4;System.Numerics.Quaternion=helengine.float4", true, []),
                new("native-file-system-header", "Native File System Header", PlatformSettingKind.Text, "\"platform/xbox/XboxContentStreamSource.hpp\"", true, []),
                new("native-file-system-type", "Native File System Type", PlatformSettingKind.Text, "helengine::xbox::XboxContentStreamSource", true, []),

                // All eleven codegen-use-* knobs are declared explicitly. Anything left undeclared falls through
                // to whichever runtime profile the generator's preset carries, and a shift in that default
                // silently regenerates a Core this repository cannot build. The values below were each verified
                // against the nxdk toolchain rather than copied from another console.
                //
                // The standard containers, delegates, clock, shared ownership and math are all available:
                // nxdk bundles libc++, so the generated Core compiles against the same std types the desktop
                // build uses.
                new(UseStdStringSettingId, "Use std::string", PlatformSettingKind.Boolean, "true", true, []),
                new(UseStdVectorSettingId, "Use std::vector", PlatformSettingKind.Boolean, "true", true, []),
                new(UseStdUnorderedMapSettingId, "Use std::unordered_map", PlatformSettingKind.Boolean, "true", true, []),
                new(UseStdUnorderedSetSettingId, "Use std::unordered_set", PlatformSettingKind.Boolean, "true", true, []),
                new(UseStdFunctionSettingId, "Use std::function", PlatformSettingKind.Boolean, "true", true, []),
                new(UseStdChronoSettingId, "Use std::chrono", PlatformSettingKind.Boolean, "true", true, []),
                new(UseStdSharedPtrSettingId, "Use std::shared_ptr", PlatformSettingKind.Boolean, "true", true, []),
                new(UseStdMathSettingId, "Use Standard Math", PlatformSettingKind.Boolean, "true", true, []),

                // Unlike N64, the hosted file stream is off. nxdk's C library has no <sys/stat.h>, and with the
                // hosted flag on, system/io/directory.cpp includes it and the generated Core fails to compile.
                // Content reaches the runtime through the native file system type declared above instead.
                new(UseHostedFileSystemSettingId, "Use Hosted File System", PlatformSettingKind.Boolean, "false", true, []),

                // nxdk's compiler wrapper hardcodes -fno-exceptions and the SDK defines none of the MSVC
                // exception ABI entry points, so the Core is generated to route failures through the runtime's
                // fatal hook rather than throwing. Verified: with exceptions on, the link is missing
                // __CxxThrowException, __CxxFrameHandler3 and __std_terminate.
                new(UseExceptionsSettingId, "Use C++ Exceptions", PlatformSettingKind.Boolean, "false", true, []),

                // RTTI stays on: generic implementation dispatch selects concrete generated types through
                // dynamic_cast and cannot be lowered without it (codegen reports CPP1001). nxdk supplies no
                // __RTDynamicCast, so this repository implements it in src/platform/xbox/XboxCxxAbi.cpp.
                new(UseRttiSettingId, "Use C++ RTTI", PlatformSettingKind.Boolean, "true", true, []),
                .. (compiledShaders ? new PlatformSettingDefinition[] {
                    new(PlatformCodegenSettingIds.EnabledFeatures, "Enabled Shader Runtime", PlatformSettingKind.Text, "shaders", true, [])
                } : Array.Empty<PlatformSettingDefinition>())
            ]);

        PlatformMaterialSchemaDefinition materialSchema = new(
            XboxMaterialSchemaIds.StandardSchemaId,
            "Xbox Opaque Unlit",
            compiledShaders ? [XboxMaterialSchemaIds.StandardGraphicsProfileId, "xbox-shader"] : [XboxMaterialSchemaIds.StandardGraphicsProfileId],
            [
                new(XboxMaterialSchemaIds.BaseColorFieldId, "Base Color", PlatformMaterialFieldKind.Color, "#FFFFFFFF", true, []),
                new(XboxMaterialSchemaIds.TextureRelativePathFieldId, "Texture", PlatformMaterialFieldKind.Text, string.Empty, false, []),
                new(XboxMaterialSchemaIds.LightingModeFieldId, "Lighting", PlatformMaterialFieldKind.Choice, XboxMaterialSchemaIds.LightingUnlitValue, true, [XboxMaterialSchemaIds.LightingUnlitValue]),
                new(XboxMaterialSchemaIds.TransparencyFieldId, "Transparency", PlatformMaterialFieldKind.Choice, XboxMaterialSchemaIds.TransparencyOpaqueValue, true, [XboxMaterialSchemaIds.TransparencyOpaqueValue])
            ]);

        PlatformBuildProfileDefinition[] profiles = [
            new("debug", "Debug", "Xbox development build", graphicsProfile, "default", buildSettings),
            new("release", "Release", "Xbox release build", graphicsProfile, "default", buildSettings)
        ];

        return new PlatformDefinition(
            "xbox",
            "Original Xbox",
            profiles,
            [new PlatformGraphicsProfileDefinition(graphicsProfile, compiledShaders ? "Xbox Shader (Experimental)" : "Xbox Standard",
                compiledShaders ? "Explicit HLSL/Cg shader packages; unsupported programs fail without legacy fallback" : "NV2A opaque unlit renderer with depth testing and back-face culling", [])],
            [new PlatformAssetRequirementDefinition("scene", "Scene", true, ["helen"])],
            // Shader batches and classic authored meshes coexist. Publishing the classic
            // schema keeps their authored colour in the cooked payload instead of serializing
            // an empty shader material for the raw-shader runtime resolver.
            [materialSchema],
            [
                new PlatformComponentSupportRule("helengine.meshcomponent", PlatformComponentSupportKind.Transform, "One static triangle-list mesh is supported.", "Remove unsupported mesh components."),
                new PlatformComponentSupportRule("helengine.cameracomponent", PlatformComponentSupportKind.Transform, "One perspective camera is supported.", "Keep one authored camera."),
                // Lights are accepted and ignored: the first milestone renders unlit, and rejecting the
                // authored sun would block scenes that are otherwise entirely supported.
                new PlatformComponentSupportRule("helengine.DirectionalLightComponent", PlatformComponentSupportKind.Transform, "Accepted but ignored: Xbox rendering is unlit in this milestone.", string.Empty),
                new PlatformComponentSupportRule("helengine.AmbientLightComponent", PlatformComponentSupportKind.Transform, "Accepted but ignored: Xbox rendering is unlit in this milestone.", string.Empty),
                new PlatformComponentSupportRule("helengine.PointLightComponent", PlatformComponentSupportKind.Unsupported, "Xbox rendering is unlit in this milestone.", "Remove point lights or exclude them on Xbox."),
                new PlatformComponentSupportRule("helengine.SpotLightComponent", PlatformComponentSupportKind.Unsupported, "Xbox rendering is unlit in this milestone.", "Remove spot lights or exclude them on Xbox.")
            ],
            [codegen],
            [new PlatformStorageProfileDefinition("xbox-xiso-package", "XISO Package", PlatformStorageProfileKind.LooseFiles, "xbox-xiso-package", false)],
            [new PlatformMediaProfileDefinition("xbox-dvd", "Xbox DVD Image", PlatformMediaLayoutKind.InstallTree, false, true)],
            new RuntimeGenerationContract(compiledShaders ? RuntimeMaterialResolutionMode.RawShaderBacked : RuntimeMaterialResolutionMode.CookedPlatformOwned,
                true, PackagedPathPolicy.ContentRelativeOnly, ["HELENGINE_CODEGEN_FATAL_ONLY"]),
            assetCookCapabilities: CreateAssetCookCapabilities());
    }

    /// <summary>Publishes identical native texture choices for authored textures and full-resolution font atlases.</summary>
    static PlatformAssetCookCapabilityDefinition[] CreateAssetCookCapabilities() {
        string defaults = XboxTextureCookSettingsSerializer.Serialize(XboxTextureCookSettingsSerializer.CreateDefaults());
        PlatformTextureFormatCapabilityDefinition formats = CreateTextureFormatCapabilities();
        return [
            new("texture", "runtime-texture", PlatformAssetCookOwnershipKind.BuilderOwned, "xbox-texture", defaults, formats),
            new("font-atlas-texture", "runtime-texture", PlatformAssetCookOwnershipKind.BuilderOwned, "xbox-font-atlas-texture", defaults, formats)
        ];
    }

    /// <summary>Derives every format/alpha combination from the same catalog used by the native encoder.</summary>
    static PlatformTextureFormatCapabilityDefinition CreateTextureFormatCapabilities() {
        List<PlatformTextureFormatCombinationDefinition> combinations = [];
        List<string> formatIds = [];
        HashSet<TextureAssetAlphaPrecision> alphaPrecisions = [];
        foreach (XboxNativeTextureFormatDefinition format in XboxNativeTextureFormatCatalog.Formats) {
            formatIds.Add(format.Id);
            foreach (TextureAssetAlphaPrecision alpha in format.SupportedAlphaPrecisions) {
                alphaPrecisions.Add(alpha);
                combinations.Add(new PlatformTextureFormatCombinationDefinition(format.Id, alpha));
            }
        }
        foreach (TextureAssetColorFormat format in Enum.GetValues<TextureAssetColorFormat>()) {
            if (!helengine.editor.TextureAssetProcessorSettings.TryResolveGenericColorFormat(format.ToString(), out _)) continue;
            formatIds.Add(format.ToString());
            foreach (TextureAssetAlphaPrecision alpha in Enum.GetValues<TextureAssetAlphaPrecision>()) {
                if (!TextureAssetPixelCodec.IsAlphaPrecisionSupported(format, alpha)) continue;
                alphaPrecisions.Add(alpha);
                combinations.Add(new PlatformTextureFormatCombinationDefinition(format.ToString(), alpha));
            }
        }
        return new PlatformTextureFormatCapabilityDefinition(
            formatIds,
            alphaPrecisions.OrderBy(alpha => alpha).ToArray(), combinations);
    }
}
