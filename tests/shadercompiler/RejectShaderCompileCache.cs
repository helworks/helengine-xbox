using helengine;

/// <summary>Proves the shared compile service respects the Xbox backend's active-dependency cache policy.</summary>
internal sealed class RejectShaderCompileCache : IShaderCompileCache {
    /// <summary>Rejects root-only cache reads because they cannot account for includes and native tools.</summary>
    public bool TryGet(ShaderCompileCacheKey key, out ShaderCompileResult result) {
        throw new InvalidOperationException("Xbox incorrectly used the shared cache.");
    }
    /// <summary>Rejects root-only cache publication for the same dependency identity reason.</summary>
    public void Store(ShaderCompileCacheKey key, ShaderCompileResult result) {
        throw new InvalidOperationException("Xbox incorrectly published into the shared cache.");
    }
}
