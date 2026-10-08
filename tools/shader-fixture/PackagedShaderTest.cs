using helengine;

namespace helengine.xbox.shaderfixture;

/// <summary>Checks actual cooked bytes rather than trusting a successful native build.</summary>
internal static class PackagedShaderTest {
    /// <summary>Requires the selected material and both Xbox programs on the staged disc.</summary>
    internal static void Run(string discRoot, bool requirePixelUniform = false, bool requireTexture = false) {
        string root = Path.GetFullPath(discRoot);
        using FileStream materialStream = File.OpenRead(Path.Combine(root, "cooked/materials/rendering/cube_test/cubetestsolid.hasset"));
        ShaderMaterialAsset material = ShaderMaterialAssetBinarySerializer.Deserialize(materialStream);
        if (material.ShaderAssetId != "XboxFixtureColour" || material.VertexProgram != "XboxFixtureColour.vs" ||
            material.PixelProgram != "XboxFixtureColour.ps" || material.Variant != "default")
            throw new InvalidOperationException($"Cooked material selected unexpected shader: {material.ShaderAssetId}/{material.VertexProgram}/{material.PixelProgram}/{material.Variant}");
        Console.WriteLine("Cooked material retains the authored Xbox shader selection.");
        if (requireTexture) {
            if (material.DiffuseTextureAssetId != TexturedFixture.TextureId)
                throw new InvalidOperationException("Cooked material lost its authored texture identity.");
            string texturePath = Path.Combine(root, "cooked", "imported", material.DiffuseTextureAssetId);
            if (!File.Exists(texturePath) || new FileInfo(texturePath).Length <= 128)
                throw new InvalidOperationException("Cooked checker texture is missing or empty.");
            using FileStream textureStream = File.OpenRead(texturePath);
            using TextureAsset texture = AssetSerializer.Deserialize(textureStream) as TextureAsset
                ?? throw new InvalidOperationException("Cooked texture cannot be read by the runtime asset serializer.");
            if (texture.Width <= 1 || texture.Height <= 1 || texture.Colors == null ||
                texture.Colors.Length <= 4 || texture.Colors.Distinct().Take(2).Count() < 2)
                throw new InvalidOperationException("Cooked texture is a placeholder or has no varying pixel payload.");
            Console.WriteLine($"Cooked authored texture decodes as {texture.Width}x{texture.Height}, {texture.ColorFormat}.");
        }
        bool found = false;
        foreach (string path in Directory.EnumerateFiles(Path.Combine(root, "cooked/shaders"), "*.hasset", SearchOption.AllDirectories)) {
            using FileStream shaderStream = File.OpenRead(path);
            ShaderAsset shader = ShaderAssetBinarySerializer.Deserialize(shaderStream);
            if (shader.TargetName != "xbox") throw new InvalidOperationException($"Foreign shader target '{shader.TargetName}' staged for Xbox: {path}");
            if (shader.Id != material.ShaderAssetId) continue;
            if (found) throw new InvalidOperationException("Duplicate fixture shader package.");
            found = true;
            foreach (ShaderStage stage in new[] { ShaderStage.Vertex, ShaderStage.Pixel }) {
                string name = stage == ShaderStage.Vertex ? material.VertexProgram : material.PixelProgram;
                ShaderBinaryAsset binary = shader.Binaries.Single(item => item.ProgramName == name && item.Stage == stage && item.Variant == "default");
                if (binary.TargetName != "xbox" || binary.Bytecode.Length <= 128 || BitConverter.ToUInt32(binary.Bytecode, 0) != 0x31535848 ||
                    BitConverter.ToUInt32(binary.Bytecode, 8) != (stage == ShaderStage.Vertex ? 1u : 2u))
                    throw new InvalidOperationException("Cooked shader does not contain the selected Xbox stage.");
                if (stage == ShaderStage.Pixel && requirePixelUniform) {
                    ShaderProgramAsset definition = shader.Programs.Single(item => item.Name == name && item.Stage == stage);
                    int bindingOffset = checked(128 + (int)BitConverter.ToUInt32(binary.Bytecode, 12) * 4);
                    if (!definition.Bindings.Any(item => item.Name == "BaseColorBuffer" && item.Type == ShaderResourceType.ConstantBuffer && item.Size == 16) ||
                        (BitConverter.ToUInt32(binary.Bytecode, 4) != 3 && BitConverter.ToUInt32(binary.Bytecode, 4) != 6) || BitConverter.ToUInt32(binary.Bytecode, 120) != 1 ||
                        binary.Bytecode.Length != bindingOffset + 72 + BitConverter.ToUInt32(binary.Bytecode, 124) * 80 ||
                        System.Text.Encoding.ASCII.GetString(binary.Bytecode, bindingOffset, 64).TrimEnd('\0') != "BaseColorBuffer" ||
                        BitConverter.ToUInt32(binary.Bytecode, bindingOffset + 64) != 0 || BitConverter.ToUInt32(binary.Bytecode, bindingOffset + 68) != 0)
                        throw new InvalidOperationException("Cooked fixture has no physical pixel-colour uniform binding.");
                    Console.WriteLine("Cooked pixel colour uses BaseColorBuffer at global combiner factor0.");
                }
                if (stage == ShaderStage.Pixel && requireTexture) {
                    ShaderProgramAsset definition = shader.Programs.Single(item => item.Name == name && item.Stage == stage);
                    int resources = checked(128 + (int)BitConverter.ToUInt32(binary.Bytecode, 12) * 4 +
                        (int)BitConverter.ToUInt32(binary.Bytecode, 120) * 72);
                    if ((BitConverter.ToUInt32(binary.Bytecode, 4) != 4 && BitConverter.ToUInt32(binary.Bytecode, 4) != 6) || BitConverter.ToUInt32(binary.Bytecode, 124) < 2 ||
                        binary.Bytecode.Length != resources + BitConverter.ToUInt32(binary.Bytecode, 124) * 80 ||
                        !definition.Bindings.Any(item => item.Name == "DiffuseTexture" && item.Type == ShaderResourceType.Texture2D) ||
                        !definition.Bindings.Any(item => item.Name == "DiffuseSampler" && item.Type == ShaderResourceType.Sampler))
                        throw new InvalidOperationException("Cooked shader is missing physical texture resources.");
                    string[] expectedNames = { "DiffuseTexture", "DiffuseSampler" };
                    for (int index = 0; index < 2; index++) {
                        int offset = resources + index * 80;
                        if (BitConverter.ToUInt32(binary.Bytecode, offset) != index + 1 ||
                            System.Text.Encoding.ASCII.GetString(binary.Bytecode, offset + 4, 64).TrimEnd('\0') != expectedNames[index] ||
                            BitConverter.ToUInt32(binary.Bytecode, offset + 68) != 0)
                            throw new InvalidOperationException("Cooked physical resource mapping differs from the authored fixture.");
                    }
                    Console.WriteLine("Cooked texture and sampler map to stage0 with their authored names intact.");
                }
            }
        }
        if (!found) throw new InvalidOperationException("Authored Xbox shader package was not staged.");
        Console.WriteLine("Staged Xbox fixture contains its authored material and both compiled shader stages.");
    }
}
