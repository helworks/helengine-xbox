using helengine;

/// <summary>Rejects unexpected external dependencies in self-contained backend integration fixtures.</summary>
internal sealed class RejectIncludeResolver : IShaderIncludeResolver {
    /// <summary>Fails with the requested include identity instead of manufacturing shader source.</summary>
    public ShaderIncludeResult Resolve(string requestingFile, string includePath) {
        throw new IOException("Unexpected include: " + includePath);
    }
}
