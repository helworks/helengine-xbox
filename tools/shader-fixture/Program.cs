using helengine;
using helengine.editor;

namespace helengine.xbox.shaderfixture;

/// <summary>Verifies the isolated authored material through the actual editor serializers.</summary>
internal static class Program {
    /// <summary>Runs against the copied project, never the original authoring project.</summary>
    private static int Main(string[] args) {
        if (args.Length == 3 && args[1] == "--cook-sprite") {
            SpriteBatchCookTest.Run(args[0], args[2]);
            return 0;
        }
        if (args.Length == 2 && (args[1] == "--textured" || args[1] == "--set-texture")) {
            TexturedFixture.Run(args[0], args[1] == "--set-texture");
            return 0;
        }
        if (args.Length == 3 && args[1] == "--cook-pair") {
            ShaderPairCookTest.Run(args[0], args[2]);
            return 0;
        }
        if (args.Length == 3 && args[1] == "--cook") {
            ShaderArtifactCookTest.Run(args[0], args[2]);
            return 0;
        }
        if (args.Length == 3 && (args[1] == "--package" || args[1] == "--package-pixel" || args[1] == "--package-texture")) {
            PackagedShaderTest.Run(args[2], args[1] == "--package-pixel", args[1] == "--package-texture");
            return 0;
        }
        if (args.Length == 3 && args[1] == "--plugin") {
            PluginCompilerTest.Run(args[0], args[2]);
            return 0;
        }
        if (args.Length < 1 || args.Length > 2 || (args.Length == 2 && args[1] != "--create"))
            throw new ArgumentException("Expected repository root and optional --create.");
        string project = Path.Combine(Path.GetFullPath(args[0]), ".xbox", "projects", "cube-test-shaders");
        string common = Path.Combine(project, "assets", "materials", "rendering", "cube_test", "CubeTestSolid.hasset");
        string overridePath = common + ".xbox.hasset";
        if (args.Length == 2) CreateOverride(overridePath);
        byte[] overrides = File.Exists(overridePath) ? File.ReadAllBytes(overridePath) : null;
        ShaderMaterialAsset material = new MaterialAssetSettingsService(project).LoadMaterialAssetFromBytes(
            File.ReadAllBytes(common), overrides, "xbox");
        ShaderMaterialAsset loaded = new MaterialAssetSettingsService(project).LoadMaterialAsset(common, "xbox");
        if (loaded.ShaderAssetId != material.ShaderAssetId || loaded.VertexProgram != material.VertexProgram || loaded.Variant != material.Variant)
            throw new InvalidOperationException($"File-backed material selection differs from serialized override: {loaded.ShaderAssetId}/{loaded.VertexProgram}/{loaded.Variant}");
        if (material.ShaderAssetId != "XboxFixtureColour" || material.VertexProgram != "XboxFixtureColour.vs" ||
            material.PixelProgram != "XboxFixtureColour.ps" || material.Variant != "default")
            throw new InvalidOperationException($"Fixture shader selection mismatch: {material.ShaderAssetId}/{material.VertexProgram}/{material.PixelProgram}/{material.Variant}");
        MaterialConstantBufferAsset color = material.ConstantBuffers.Single(buffer => buffer.Name == "BaseColorBuffer");
        if (color.Data.Length != 16 || Math.Abs(BitConverter.ToSingle(color.Data, 0) - 0.2f) > 0.00001f ||
            Math.Abs(BitConverter.ToSingle(color.Data, 4) - 0.8f) > 0.00001f ||
            Math.Abs(BitConverter.ToSingle(color.Data, 8) - 1.0f) > 0.00001f || BitConverter.ToSingle(color.Data, 12) != 1.0f)
            throw new InvalidOperationException("Fixture must hydrate opaque cyan BaseColorBuffer through authored settings.");
        Console.WriteLine("Authored Xbox fixture selection and material constants passed.");
        return 0;
    }

    /// <summary>Creates only the isolated fixture override, refusing to replace an existing file.</summary>
    private static void CreateOverride(string path) {
        MaterialAssetPlatformOverrideDocument document = new MaterialAssetPlatformOverrideDocument { PlatformId = "xbox" };
        document.Processor.HasSchemaIdOverride = true;
        document.Processor.SchemaId = "standard-shader";
        document.Processor.FieldValues["use-custom-shader"] = "true";
        document.Processor.FieldValues["shader-asset-id"] = "XboxFixtureColour";
        document.Processor.FieldValues["vertex-program"] = "XboxFixtureColour.vs";
        document.Processor.FieldValues["pixel-program"] = "XboxFixtureColour.ps";
        document.Processor.FieldValues["base-color"] = "#33CCFFFF";
        document.Processor.FieldValues["casts-shadow"] = "false";
        document.Processor.FieldValues["receives-shadow"] = "false";
        using FileStream stream = new FileStream(path, FileMode.CreateNew, FileAccess.Write);
        MaterialAssetPlatformOverrideDocumentBinarySerializer.Serialize(stream, document);
    }
}
