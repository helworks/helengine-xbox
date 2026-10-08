using System.Diagnostics;

namespace helengine.xbox.shaderprobe;

/// <summary>Runs one native shader tool with bounded lifetime and retained diagnostics.</summary>
public static class XboxShaderToolProcess {
    /// <summary>Invokes an executable without a shell and propagates timeout, cancellation and native diagnostics.</summary>
    public static string Run(string executable, IEnumerable<string> arguments, string jobRoot,
                             string name, CancellationToken cancellationToken, int timeoutSeconds = 60) {
        cancellationToken.ThrowIfCancellationRequested();
        ProcessStartInfo info = new(executable) {
            WorkingDirectory = jobRoot, UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true
        };
        foreach (string argument in arguments) info.ArgumentList.Add(argument);
        using Process process = new() { StartInfo = info };
        using CancellationTokenSource timeout = new(TimeSpan.FromSeconds(timeoutSeconds));
        using CancellationTokenSource lifetime = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeout.Token);
        if (!process.Start()) throw new InvalidOperationException("Could not start " + name);
        Task<string> stdout = process.StandardOutput.ReadToEndAsync();
        Task<string> stderr = process.StandardError.ReadToEndAsync();
        try {
            process.WaitForExitAsync(lifetime.Token).GetAwaiter().GetResult();
        } catch (OperationCanceledException) {
            if (!process.HasExited) process.Kill(entireProcessTree: true);
            process.WaitForExit();
            if (cancellationToken.IsCancellationRequested) throw;
            throw new TimeoutException($"{name} exceeded {timeoutSeconds} seconds.");
        } finally {
            File.WriteAllText(Path.Combine(jobRoot, name + ".stdout.log"), stdout.GetAwaiter().GetResult());
            File.WriteAllText(Path.Combine(jobRoot, name + ".stderr.log"), stderr.GetAwaiter().GetResult());
            File.WriteAllText(Path.Combine(jobRoot, name + ".command.json"),
                System.Text.Json.JsonSerializer.Serialize(new { executable, arguments = info.ArgumentList.ToArray() }));
        }
        if (process.ExitCode != 0)
            throw new InvalidOperationException($"{name} exited {process.ExitCode}: {stderr.Result}\n{stdout.Result}");
        return stdout.Result;
    }
}
