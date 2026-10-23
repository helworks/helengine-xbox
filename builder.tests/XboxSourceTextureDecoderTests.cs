using helengine;
using helengine.xbox.builder;

namespace helengine.xbox.builder.tests;

/// <summary>Checks source pixels before platform-specific texture cooking on every supported host.</summary>
public sealed class XboxSourceTextureDecoderTests {
    /// <summary>Decodes a real RGBA PNG without relying on the Windows editor backend.</summary>
    [Fact]
    public void AuthoredPngDecodesToOwnedRgbaPixels() {
        string root = Path.Combine(AppContext.BaseDirectory, "source-decoder");
        Directory.CreateDirectory(root);
        string path = Path.Combine(root, "pixel.png");
        byte[] png = Convert.FromBase64String("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/iZk9HQAAAABJRU5ErkJggg==");
        try {
            File.WriteAllBytes(path, png);
            using TextureAsset decoded = new XboxSourceTextureDecoder().Decode(path);
            Assert.Equal(1, decoded.Width);
            Assert.Equal(1, decoded.Height);
            Assert.Equal(TextureAssetColorFormat.Rgba32, decoded.ColorFormat);
            Assert.Equal(TextureAssetAlphaPrecision.A8, decoded.AlphaPrecision);
            Assert.Equal(new byte[] {255, 0, 0, 255}, decoded.Colors);
            Assert.Equal(png, File.ReadAllBytes(path));
        } finally {
            File.Delete(path);
            Directory.Delete(root);
        }
    }
}
