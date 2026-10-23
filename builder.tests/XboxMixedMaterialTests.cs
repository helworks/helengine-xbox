using helengine;
using helengine.baseplatform.Requests;
using helengine.xbox.builder;

namespace helengine.xbox.builder.tests;

/// <summary>Checks classic material cooking while the menu's compiled shader path is enabled.</summary>
public sealed class XboxMixedMaterialTests {
    /// <summary>Checks material payloads use the requested target envelope instead of the tooling host.</summary>
    [Theory]
    [InlineData(16)]
    [InlineData(32)]
    [InlineData(64)]
    public void MaterialPayloadLoadsWithTargetNumericPolicy(int bits) {
        var policy = new NumericPrecisionPolicy(bits, bits);
        var request = new PlatformMaterialCookRequest("material", "materials/sample.hasset", "xbox", "release", "xbox-standard",
            XboxMaterialSchemaIds.StandardSchemaId, new Dictionary<string, string>(), numericPolicy: policy);
        var result = new XboxMaterialCooker().Cook(request);
        using var stream = new MemoryStream(result.CookedMaterialBytes);
        Assert.IsType<PlatformMaterialAsset>(PackagedAssetBinarySerializer.Deserialize(stream, policy));
    }

    /// <summary>Requires the authored colour and classic identity to survive either graphics profile.</summary>
    [Theory]
    [InlineData("xbox-standard")]
    [InlineData("xbox-shader")]
    public void ClassicColourSurvivesShaderProfile(string profile) {
        var request = new PlatformMaterialCookRequest("CubeTestSolid", "materials/cube.hasset", "xbox",
            "release", profile, XboxMaterialSchemaIds.StandardSchemaId,
            new Dictionary<string, string> { ["base-color"] = "#2468ACFF" }, numericPolicy: global::helengine.NumericPrecisionPolicy.Default);
        var result = new XboxMaterialCooker().Cook(request);
        using var stream = new MemoryStream(result.CookedMaterialBytes);
        var material = Assert.IsType<PlatformMaterialAsset>(PackagedAssetBinarySerializer.Deserialize(stream, request.NumericPolicy));
        Assert.Equal("xbox-standard", material.RendererFamilyId);
        Assert.Equal((byte)0x24, material.BaseColorR);
        Assert.Equal((byte)0x68, material.BaseColorG);
        Assert.Equal((byte)0xAC, material.BaseColorB);
        Assert.Equal((byte)255, material.BaseColorA);
    }

    /// <summary>Requires the shader profile to publish its classic cooker without replacing shader codegen.</summary>
    [Fact]
    public void ShaderProfilePublishesClassicSchemaAndRawShaderContract() {
        var definition = XboxPlatformDefinitionFactory.Create(true);
        Assert.Contains(definition.MaterialSchemas, schema => schema.SchemaId == "xbox-standard-v1");
        Assert.Equal("RawShaderBacked", definition.RuntimeGenerationContract.MaterialResolutionMode.ToString());
    }
}
