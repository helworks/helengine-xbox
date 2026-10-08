using System.Runtime.InteropServices;
using System.Text;
using Silk.NET.Shaderc;

namespace helengine.xbox.shaderprobe;

/// <summary>Owns synchronous shaderc callbacks and include allocations for one translation.</summary>
internal sealed unsafe class XboxShaderIncludeSession : IDisposable {
    /// <summary>Resolves only directives selected by shaderc preprocessing.</summary>
    readonly Func<string, string, XboxShaderSource> Resolver;
    /// <summary>Retains native blocks until shaderc releases them or the session ends.</summary>
    readonly HashSet<IntPtr> Allocations = [];
    /// <summary>Retains exact contents to reject inconsistent repeated dependency identities.</summary>
    readonly Dictionary<string, string> ResolvedSources = new(StringComparer.Ordinal);
    /// <summary>Roots the managed resolve delegate for native calls.</summary>
    readonly IncludeResolveFn ResolveDelegate;
    /// <summary>Roots the managed release delegate for native calls.</summary>
    readonly IncludeResultReleaseFn ReleaseDelegate;
    /// <summary>Supplies a preallocated native failure result when a callback throws.</summary>
    readonly IntPtr ErrorResult;
    /// <summary>Initializes rooted callbacks and a fallback that requires no allocation in exception paths.</summary>
    public XboxShaderIncludeSession(XboxShaderSourceRequest request) {
        Resolver = request.IncludeResolver;
        ResolvedSources.Add(request.Source.Path, request.Source.Text);
        ErrorResult = Allocate(string.Empty, "Xbox include resolution failed; see the managed include diagnostic.");
        ResolveDelegate = Resolve;
        ReleaseDelegate = Release;
        try {
            ResolveCallback = PfnIncludeResolveFn.From(ResolveDelegate);
            ReleaseCallback = PfnIncludeResultReleaseFn.From(ReleaseDelegate);
        } catch {
            Dispose();
            throw;
        }
    }
    /// <summary>Gets the native resolve handle kept alive for the compiler options.</summary>
    public PfnIncludeResolveFn ResolveCallback { get; }
    /// <summary>Gets the native release handle kept alive until compilation returns.</summary>
    public PfnIncludeResultReleaseFn ReleaseCallback { get; }
    /// <summary>Gets the first managed callback error after native compilation has returned.</summary>
    public Exception Failure { get; private set; }
    /// <summary>Gets the root and active dependency contents used by this translation.</summary>
    public IReadOnlyDictionary<string, string> Sources => ResolvedSources;
    /// <summary>Resolves a native callback while preventing managed exceptions from crossing the ABI.</summary>
    IncludeResult* Resolve(void* user, byte* requested, int type, byte* requesting, nuint depth) {
        try {
            if (Failure != null) return (IncludeResult*)ErrorResult;
            if (depth > 64) throw new InvalidOperationException("Xbox include nesting exceeds 64 levels.");
            string requestedPath = Marshal.PtrToStringUTF8((IntPtr)requested);
            string requestingPath = Marshal.PtrToStringUTF8((IntPtr)requesting);
            XboxShaderSource source = Resolver(requestingPath, requestedPath)
                ?? throw new InvalidOperationException("Include resolver returned no source.");
            if (ResolvedSources.TryGetValue(source.Path, out string previous) && previous != source.Text)
                throw new InvalidOperationException("Include contents changed within one compilation: " + source.Path);
            ResolvedSources[source.Path] = source.Text;
            return (IncludeResult*)Allocate(source.Path, source.Text);
        } catch (Exception error) {
            Failure ??= error;
            return (IncludeResult*)ErrorResult;
        }
    }
    /// <summary>Allocates a complete include structure and its two UTF-8 strings in one owned block.</summary>
    IntPtr Allocate(string name, string text) {
        byte[] nameBytes = Encoding.UTF8.GetBytes(name);
        byte[] textBytes = Encoding.UTF8.GetBytes(text);
        IntPtr block = Marshal.AllocHGlobal(checked(sizeof(IncludeResult) + nameBytes.Length + textBytes.Length + 2));
        try {
            byte* namePointer = (byte*)block + sizeof(IncludeResult);
            byte* textPointer = namePointer + nameBytes.Length + 1;
            Marshal.Copy(nameBytes, 0, (IntPtr)namePointer, nameBytes.Length);
            Marshal.Copy(textBytes, 0, (IntPtr)textPointer, textBytes.Length);
            namePointer[nameBytes.Length] = 0;
            textPointer[textBytes.Length] = 0;
            *(IncludeResult*)block = new IncludeResult { SourceName = namePointer, SourceNameLength = (nuint)nameBytes.Length,
                Content = textPointer, ContentLength = (nuint)textBytes.Length, UserData = null };
            Allocations.Add(block);
            return block;
        } catch {
            Marshal.FreeHGlobal(block);
            throw;
        }
    }
    /// <summary>Releases one callback result, retaining the emergency block until disposal.</summary>
    void Release(void* user, IncludeResult* result) {
        IntPtr block = (IntPtr)result;
        if (block != ErrorResult && Allocations.Remove(block)) Marshal.FreeHGlobal(block);
    }
    /// <summary>Releases leftovers and rooted native handles after compiler options/results have been freed.</summary>
    public void Dispose() {
        foreach (IntPtr block in Allocations) Marshal.FreeHGlobal(block);
        Allocations.Clear();
        ResolveCallback.Dispose();
        ReleaseCallback.Dispose();
    }
}
