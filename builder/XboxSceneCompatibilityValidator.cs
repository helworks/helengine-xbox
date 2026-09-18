using helengine.baseplatform.Manifest;

namespace helengine.xbox.builder;

/// <summary>
/// Rejects manifests the Xbox runtime cannot draw, so an unsupported scene fails during the build rather
/// than booting to a blank screen on the console.
/// </summary>
public sealed class XboxSceneCompatibilityValidator {
    /// <summary>Validates one build manifest against the Xbox runtime contract.</summary>
    /// <param name="manifest">Manifest produced by the editor for this build.</param>
    public void Validate(PlatformBuildManifest manifest) {
        if (manifest == null) throw new ArgumentNullException(nameof(manifest));
        if (manifest.Scenes == null || !manifest.Scenes.Any()) {
            throw new InvalidOperationException("The Xbox build manifest contains no scenes.");
        }
        if (string.IsNullOrWhiteSpace(manifest.StartupSceneId)) {
            throw new InvalidOperationException("The Xbox build manifest declares no startup scene.");
        }
    }
}
