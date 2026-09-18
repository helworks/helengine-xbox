using helengine.baseplatform.Manifest;

namespace helengine.xbox.builder;

/// <summary>
/// Stages the cooked files the disc image must carry into the packaging input tree. Both the
/// manifest-declared artifacts and any outputs this builder cooked itself are staged, because the editor
/// leaves builder-owned outputs out of the manifest and staging the manifest alone would ship an image
/// missing them.
/// </summary>
public sealed class XboxContentStager {
    /// <summary>Stages manifest-declared cooked artifacts together with the builder's own cook outputs.</summary>
    /// <param name="artifacts">Manifest-declared cooked artifacts.</param>
    /// <param name="builderCookedRelativePaths">Runtime-relative outputs this builder cooked itself.</param>
    /// <param name="sourceRootPath">Package source root holding every cooked file.</param>
    /// <param name="stagingRootPath">Packaging input tree to rebuild.</param>
    /// <param name="cancellationToken">Token observed between files.</param>
    /// <returns>Runtime-relative paths staged, in staging order.</returns>
    public IReadOnlyList<string> Stage(
        IReadOnlyList<PlatformBuildArtifact> artifacts,
        IReadOnlyList<string> builderCookedRelativePaths,
        string sourceRootPath,
        string stagingRootPath,
        CancellationToken cancellationToken) {
        if (artifacts == null) throw new ArgumentNullException(nameof(artifacts));
        if (builderCookedRelativePaths == null) throw new ArgumentNullException(nameof(builderCookedRelativePaths));
        if (string.IsNullOrWhiteSpace(sourceRootPath)) throw new ArgumentException("Source root is required.", nameof(sourceRootPath));
        if (string.IsNullOrWhiteSpace(stagingRootPath)) throw new ArgumentException("Staging root is required.", nameof(stagingRootPath));

        string sourceRoot = Path.GetFullPath(sourceRootPath);
        string stagingRoot = Path.GetFullPath(stagingRootPath);
        if (string.Equals(
                sourceRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                stagingRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
                StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException("Xbox source and staging roots must be different directories.");
        }

        if (Directory.Exists(stagingRoot)) Directory.Delete(stagingRoot, true);
        Directory.CreateDirectory(stagingRoot);

        HashSet<string> paths = new(StringComparer.OrdinalIgnoreCase);
        List<string> stagedPaths = new(artifacts.Count + builderCookedRelativePaths.Count);
        foreach (PlatformBuildArtifact artifact in artifacts) {
            cancellationToken.ThrowIfCancellationRequested();
            StageOne(artifact.RelativePath, sourceRoot, stagingRoot, paths, stagedPaths);
        }
        foreach (string builderCookedRelativePath in builderCookedRelativePaths) {
            cancellationToken.ThrowIfCancellationRequested();
            StageOne(builderCookedRelativePath, sourceRoot, stagingRoot, paths, stagedPaths);
        }
        return stagedPaths;
    }

    static void StageOne(
        string candidateRelativePath,
        string sourceRoot,
        string stagingRoot,
        HashSet<string> paths,
        List<string> stagedPaths) {
        string relativePath = NormalizeRelativePath(candidateRelativePath);
        if (!paths.Add(relativePath)) {
            throw new InvalidOperationException($"Xbox cooked artifact path collision: '{relativePath}'.");
        }
        string sourcePath = ResolveUnderRoot(sourceRoot, relativePath);
        if (!File.Exists(sourcePath)) {
            throw new FileNotFoundException(
                $"Xbox cooked artifact '{relativePath}' was not found under package source root '{sourceRoot}'.",
                sourcePath);
        }
        string destinationPath = ResolveUnderRoot(stagingRoot, relativePath);
        Directory.CreateDirectory(Path.GetDirectoryName(destinationPath) ?? stagingRoot);
        File.Copy(sourcePath, destinationPath, true);
        stagedPaths.Add(relativePath);
    }

    /// <summary>Normalizes a cooked artifact path and rejects anything that is not disc-relative.</summary>
    public static string NormalizeRelativePath(string path) {
        if (string.IsNullOrWhiteSpace(path)) throw new InvalidOperationException("Xbox cooked artifact path is required.");
        string normalized = path.Replace('\\', '/');
        if (normalized.StartsWith('/') || Path.IsPathRooted(normalized) || normalized.Split('/').Any(segment => segment == "..")) {
            throw new InvalidOperationException($"Xbox cooked artifact path must remain relative: '{path}'.");
        }
        return normalized;
    }

    static string ResolveUnderRoot(string root, string relativePath) {
        string fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;
        string fullPath = Path.GetFullPath(Path.Combine(fullRoot, relativePath.Replace('/', Path.DirectorySeparatorChar)));
        if (!fullPath.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Xbox path escapes staging root: '{relativePath}'.");
        }
        return fullPath;
    }
}
