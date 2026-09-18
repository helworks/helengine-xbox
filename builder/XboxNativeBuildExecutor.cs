using System.Diagnostics;

namespace helengine.xbox.builder;

/// <summary>Invokes the repository's Docker build wrapper for authored mode.</summary>
public sealed class XboxNativeBuildExecutor : IXboxNativeBuildExecutor {
    /// <summary>Environment variable naming the Xbox implementation checkout.</summary>
    public const string RepositoryRootVariable = "HELENGINE_XBOX_REPOSITORY_ROOT";

    public void Build(string generatedCoreCppRootPath, string stagedContentRootPath, string outputRootPath, CancellationToken cancellationToken) {
        if (string.IsNullOrWhiteSpace(generatedCoreCppRootPath)) throw new ArgumentException("Generated Core root is required.", nameof(generatedCoreCppRootPath));
        if (string.IsNullOrWhiteSpace(stagedContentRootPath)) throw new ArgumentException("Staged content root is required.", nameof(stagedContentRootPath));
        if (string.IsNullOrWhiteSpace(outputRootPath)) throw new ArgumentException("Output root is required.", nameof(outputRootPath));
        cancellationToken.ThrowIfCancellationRequested();

        string repositoryRoot = Environment.GetEnvironmentVariable(RepositoryRootVariable) ?? string.Empty;
        if (string.IsNullOrWhiteSpace(repositoryRoot) || !Directory.Exists(repositoryRoot)) {
            throw new InvalidOperationException($"{RepositoryRootVariable} must identify the Xbox implementation checkout.");
        }
        if (!Directory.Exists(generatedCoreCppRootPath)) {
            throw new DirectoryNotFoundException($"Generated Core root '{generatedCoreCppRootPath}' was not found.");
        }
        if (!Directory.Exists(stagedContentRootPath)) {
            throw new DirectoryNotFoundException($"Staged content root '{stagedContentRootPath}' was not found.");
        }

        Directory.CreateDirectory(outputRootPath);

        // build.ps1 refuses to reuse an invocation directory, so each native pass gets a fresh one and the
        // build record it writes stays attributable to exactly one source snapshot.
        string invocationRoot = Path.Combine(
            outputRootPath,
            "native-" + DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fff"));

        ProcessStartInfo info = new() {
            // Windows PowerShell, not pwsh: pwsh is not on PATH in this workspace.
            FileName = "powershell",
            WorkingDirectory = repositoryRoot,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        info.ArgumentList.Add("-NoProfile");
        info.ArgumentList.Add("-ExecutionPolicy");
        info.ArgumentList.Add("Bypass");
        info.ArgumentList.Add("-File");
        info.ArgumentList.Add(Path.Combine(repositoryRoot, "scripts", "build.ps1"));
        info.ArgumentList.Add("-OutputRoot");
        info.ArgumentList.Add(invocationRoot);
        info.ArgumentList.Add("-Configuration");
        info.ArgumentList.Add("Release");
        info.ArgumentList.Add("-Mode");
        info.ArgumentList.Add("Authored");
        info.ArgumentList.Add("-GeneratedCoreRoot");
        info.ArgumentList.Add(generatedCoreCppRootPath);
        info.ArgumentList.Add("-ContentRoot");
        info.ArgumentList.Add(stagedContentRootPath);

        using Process process = Process.Start(info) ?? throw new InvalidOperationException("Could not start the Xbox build wrapper.");
        Task<string> standardOutput = process.StandardOutput.ReadToEndAsync(cancellationToken);
        Task<string> standardError = process.StandardError.ReadToEndAsync(cancellationToken);
        process.WaitForExit();
        cancellationToken.ThrowIfCancellationRequested();
        string output = standardOutput.GetAwaiter().GetResult();
        string error = standardError.GetAwaiter().GetResult();
        if (process.ExitCode != 0) {
            throw new InvalidOperationException(
                $"Xbox native build failed (exit {process.ExitCode}).{Environment.NewLine}{output}{Environment.NewLine}{error}");
        }

        string builtImage = Path.Combine(invocationRoot, "artifacts", "game.iso");
        if (!File.Exists(builtImage)) {
            throw new FileNotFoundException("Xbox native build completed without producing a disc image.", builtImage);
        }
        File.Copy(builtImage, Path.Combine(outputRootPath, "game.iso"), true);

        string builtExecutable = Path.Combine(invocationRoot, "artifacts", "default.xbe");
        if (File.Exists(builtExecutable)) {
            File.Copy(builtExecutable, Path.Combine(outputRootPath, "default.xbe"), true);
        }
    }
}
