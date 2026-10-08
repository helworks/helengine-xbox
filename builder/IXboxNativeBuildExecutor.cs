namespace helengine.xbox.builder;

/// <summary>Invokes the Xbox repository's native build for authored mode.</summary>
public interface IXboxNativeBuildExecutor {
    /// <summary>Builds the authored Xbox image from a generated Core root and a staged content tree.</summary>
    /// <param name="generatedCoreCppRootPath">Generated Core C++ root produced by codegen.</param>
    /// <param name="stagedContentRootPath">Staged content tree packaged onto the disc image.</param>
    /// <param name="outputRootPath">Directory receiving the built image.</param>
    /// <param name="cancellationToken">Cancels a long-running native build.</param>
    void Build(string generatedCoreCppRootPath, string stagedContentRootPath, string outputRootPath, string nativeObjectCacheRootPath, CancellationToken cancellationToken);
}
