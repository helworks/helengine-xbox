using helengine;
using helengine.editor;

namespace helengine.xbox.shaderfixture;

/// <summary>Configures and verifies a texture on the repository-owned authored fixture.</summary>
internal static class TexturedFixture {
    /// <summary>Identifies the existing checker texture's imported payload, not its distinct hmeta authoring identity.</summary>
    internal const string TextureId = "c373b0ca756d5f1e1eba8965439df3156b7a8ef9e00c841cbb174f52cf420c28";

    /// <summary>Requires the exact raw material texture identity after editor hydration.</summary>
    internal static void Run(string repositoryRoot, bool configure) {
        string project = Path.Combine(Path.GetFullPath(repositoryRoot), ".xbox", "projects", "cube-test-shaders");
        string path = Path.Combine(project, "assets", "materials", "rendering", "cube_test", "CubeTestSolid.hasset");
        if (configure) {
            using ContentManager content = new ContentManager(new HostFileSystemContentStreamSource(Path.Combine(project, "assets")));
            using AssetImportManager importer = new AssetImportManager(project, content) { CurrentPlatformId = "xbox" };
            importer.RegisterTextureImporter(new TextureImporterRegistration("gdi", new GDITextureImporter(), [".png"]));
            using TextureAsset imported = importer.ImportTexture(Path.Combine(project, "assets", "textures", "menu", "helengine-logo-checker.png"));
            if (imported.Id != TextureId || imported.Width <= 1 || imported.Height <= 1)
                throw new InvalidOperationException($"Imported fixture texture identity or extent is invalid: {imported.Id}, {imported.Width}x{imported.Height}.");
            string overridePath = path + ".xbox.hasset";
            MaterialAssetPlatformOverrideDocument document;
            using (FileStream input = File.OpenRead(overridePath))
                document = MaterialAssetPlatformOverrideDocumentBinarySerializer.Deserialize(input);
            if (document.PlatformId != "xbox" || document.Processor.FieldValues["shader-asset-id"] != "XboxFixtureColour")
                throw new InvalidOperationException("Refusing to change a foreign material override.");
            document.Processor.FieldValues["texture-id"] = TextureId;
            using MemoryStream serialized = new MemoryStream();
            MaterialAssetPlatformOverrideDocumentBinarySerializer.Serialize(serialized, document);
            File.WriteAllBytes(overridePath, serialized.ToArray());
        }
        ShaderMaterialAsset material = new MaterialAssetSettingsService(project).LoadMaterialAsset(path, "xbox");
        if (material.DiffuseTextureAssetId != TextureId)
            throw new InvalidOperationException("Authored fixture must preserve the checker texture identity.");
        if (!File.Exists(Path.Combine(project, "cache", TextureId)))
            throw new InvalidOperationException("Fixture texture must be imported before packaging.");
        Console.WriteLine("Authored fixture resolves the expected checker texture through the editor material serializer.");
    }
}
