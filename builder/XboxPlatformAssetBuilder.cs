using helengine.baseplatform.Builders;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Descriptors;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.xbox.builder;

/// <summary>Platform discovery and build contract for the Original Xbox target.</summary>
public sealed class XboxPlatformAssetBuilder : IPlatformAssetBuilder {
    readonly XboxMaterialCooker MaterialCooker = new();
    readonly IXboxNativeBuildExecutor NativeBuildExecutor;

    public XboxPlatformAssetBuilder()
        : this(new XboxNativeBuildExecutor()) {
    }

    public XboxPlatformAssetBuilder(IXboxNativeBuildExecutor nativeBuildExecutor) {
        NativeBuildExecutor = nativeBuildExecutor ?? throw new ArgumentNullException(nameof(nativeBuildExecutor));
        Descriptor = new PlatformBuilderDescriptor(
            "helengine.xbox.builder",
            "0.1.0",
            "xbox",
            new EngineCompatibilityRange("1.0.0", "999.0.0"),
            new ManifestCompatibilityRange(1, 1),
            ["xbox"],
            ["xbox"]);
        Definition = XboxPlatformDefinitionFactory.Create();
    }

    public PlatformBuilderDescriptor Descriptor { get; }

    public PlatformDefinition Definition { get; }

    public PlatformMaterialCookResult CookMaterial(PlatformMaterialCookRequest request) => MaterialCooker.Cook(request);

    public async Task<PlatformBuildReport> BuildAsync(
        PlatformBuildRequest request,
        IPlatformBuildProgressReporter progressReporter,
        IPlatformBuildDiagnosticReporter diagnosticReporter,
        CancellationToken cancellationToken) {
        if (request == null) throw new ArgumentNullException(nameof(request));
        if (progressReporter == null) throw new ArgumentNullException(nameof(progressReporter));
        if (diagnosticReporter == null) throw new ArgumentNullException(nameof(diagnosticReporter));

        List<PlatformBuildDiagnostic> diagnostics = [];
        string stagingRoot = Path.Combine(request.WorkingRoot, "xbox-disc");
        string outputImagePath = Path.Combine(request.OutputRoot, "game.iso");
        try {
            cancellationToken.ThrowIfCancellationRequested();
            Directory.CreateDirectory(request.OutputRoot);

            // A failed native invocation must not be mistaken for success because an older image is still
            // sitting in the output tree.
            if (File.Exists(outputImagePath)) File.Delete(outputImagePath);

            if (!string.Equals(request.Manifest.PlatformName, "xbox", StringComparison.OrdinalIgnoreCase)) {
                throw new InvalidOperationException($"Xbox builder received manifest for platform '{request.Manifest.PlatformName}'.");
            }

            new XboxSceneCompatibilityValidator().Validate(request.Manifest);
            string packageSourceRoot = ResolvePackageSourceRoot(request.WorkingRoot);

            PlatformCookWorkItem[] cookWorkItems = request.Manifest.PlatformCookWorkItems ?? [];
            if (cookWorkItems.Length > 0) {
                // No builder-owned cook kinds are declared yet, so anything arriving here means the platform
                // definition and this builder have drifted apart. Fail loudly rather than skip the work.
                throw new InvalidOperationException(
                    $"Xbox builder received {cookWorkItems.Length} platform cook work item(s) but declares no builder-owned cook capabilities.");
            }

            progressReporter.Report(new PlatformBuildProgressUpdate("Stage Disc Content", "manifest", 0, 1, "Staging declared Xbox cooked artifacts."));
            IReadOnlyList<string> staged = new XboxContentStager().Stage(
                request.Manifest.CookedArtifacts,
                [],
                packageSourceRoot,
                stagingRoot,
                cancellationToken);
            progressReporter.Report(new PlatformBuildProgressUpdate("Stage Disc Content", "manifest", 1, 1, $"Staged {staged.Count} cooked artifact(s)."));

            progressReporter.Report(new PlatformBuildProgressUpdate("Build Native Image", "game.iso", 0, 1, "Building authored Xbox runtime."));
            NativeBuildExecutor.Build(request.GeneratedCoreCppRootPath, stagingRoot, request.OutputRoot, cancellationToken);
            progressReporter.Report(new PlatformBuildProgressUpdate("Build Native Image", "game.iso", 1, 1, "Built authored Xbox runtime."));
        } catch (OperationCanceledException) {
            throw;
        } catch (Exception exception) {
            PlatformBuildDiagnostic diagnostic = new(
                PlatformBuildDiagnosticSeverity.Error,
                "XBOXBUILD_FAILED",
                exception.Message,
                request.Manifest.StartupSceneId,
                string.Empty,
                "xbox-builder");
            diagnostics.Add(diagnostic);
            diagnosticReporter.Report(diagnostic);
        }

        bool succeeded = diagnostics.Count == 0 && File.Exists(outputImagePath);
        return await Task.FromResult(new PlatformBuildReport(
            succeeded,
            [.. diagnostics],
            request.Manifest.Scenes
                .Select(scene => new PlatformBuildItemOutcome(scene.SceneId, succeeded ? PlatformBuildItemOutcomeKind.Succeeded : PlatformBuildItemOutcomeKind.Failed))
                .ToArray(),
            request.Manifest.LooseAssets
                .Select(asset => new PlatformBuildItemOutcome(asset.AssetId, succeeded ? PlatformBuildItemOutcomeKind.Succeeded : PlatformBuildItemOutcomeKind.Failed))
                .ToArray()));
    }

    static string ResolvePackageSourceRoot(string workingRootPath) {
        if (string.IsNullOrWhiteSpace(workingRootPath)) {
            throw new ArgumentException("Builder working root is required.", nameof(workingRootPath));
        }
        string packageSourceRoot = Path.Combine(Path.GetFullPath(workingRootPath), "package-source");
        if (!Directory.Exists(packageSourceRoot)) {
            throw new DirectoryNotFoundException($"Xbox builder package source root '{packageSourceRoot}' was not found.");
        }
        return packageSourceRoot;
    }
}
