using helengine.baseplatform.Manifest;
using helengine.xbox.builder;

namespace helengine.xbox.builder.tests;

public sealed class XboxContentStagingPreflightTests : IDisposable {
    readonly string Root = Path.Combine(AppContext.BaseDirectory, "staging-tests", Guid.NewGuid().ToString("N"));

    string Source => Path.Combine(Root, "source");
    string StageRoot => Path.Combine(Root, "staged");

    public XboxContentStagingPreflightTests() {
        Directory.CreateDirectory(Source);
        Directory.CreateDirectory(StageRoot);
        File.WriteAllText(Path.Combine(Source, "scene.heasset"), "scene");
        File.WriteAllText(Path.Combine(StageRoot, "previous.heasset"), "previous");
    }

    [Theory]
    [InlineData("source", "source")]
    [InlineData("source", ".")]
    [InlineData("source", "source/staged")]
    public void OverlappingRootsAreRejectedBeforeDeletingSource(string source, string stage) {
        Assert.Throws<InvalidOperationException>(() => new XboxContentStager().Stage(
            [Artifact("scene.heasset")], [], Path.Combine(Root, source), Path.Combine(Root, stage), CancellationToken.None));
        Assert.Equal("scene", File.ReadAllText(Path.Combine(Source, "scene.heasset")));
        AssertPreviousStage();
    }

    [Fact]
    public void SimilarRootPrefixDoesNotCountAsOverlap() {
        string stage = Path.Combine(Root, "source-other");
        new XboxContentStager().Stage([Artifact("scene.heasset")], [], Source, stage, CancellationToken.None);
        Assert.Equal("scene", File.ReadAllText(Path.Combine(stage, "scene.heasset")));
    }

    [Fact]
    public void MissingLaterBuilderOutputPreservesPreviousStage() {
        Assert.Throws<FileNotFoundException>(() => Stage([Artifact("scene.heasset")], ["missing.heasset"]));
        AssertPreviousStage();
        Assert.False(File.Exists(Path.Combine(StageRoot, "scene.heasset")));
    }

    [Theory]
    [InlineData("nested//scene.heasset")]
    [InlineData("nested\\scene.heasset")]
    [InlineData("NESTED/SCENE.HEASSET")]
    public void AliasedBuilderOutputCollisionsPreservePreviousStage(string alias) {
        Directory.CreateDirectory(Path.Combine(Source, "nested"));
        File.WriteAllText(Path.Combine(Source, "nested", "scene.heasset"), "scene");
        Assert.Throws<InvalidOperationException>(() => Stage([Artifact("nested/scene.heasset")], [alias]));
        AssertPreviousStage();
    }

    [Theory]
    [InlineData("../outside")]
    [InlineData("./scene.heasset")]
    [InlineData("nested/../scene.heasset")]
    [InlineData("scene.heasset.")]
    [InlineData("scene.heasset ")]
    [InlineData("scene.heasset:stream")]
    [InlineData("/absolute")]
    [InlineData("C:/absolute")]
    public void InvalidLaterOutputPreservesPreviousStage(string invalidPath) {
        Assert.Throws<InvalidOperationException>(() => Stage([Artifact("scene.heasset")], [invalidPath]));
        AssertPreviousStage();
    }

    [Fact]
    public void PrecancelledEmptyBuildPreservesPreviousStage() {
        using CancellationTokenSource cancellation = new();
        cancellation.Cancel();
        Assert.Throws<OperationCanceledException>(() => new XboxContentStager().Stage(
            [], [], Source, StageRoot, cancellation.Token));
        AssertPreviousStage();
    }

    [Fact]
    public void ValidBuildReplacesPreviousFilesAndStagesBothKindsInOrder() {
        Directory.CreateDirectory(Path.Combine(Source, "textures"));
        File.WriteAllText(Path.Combine(Source, "textures", "atlas"), "atlas");
        IReadOnlyList<string> staged = Stage([Artifact("scene.heasset")], ["textures\\atlas"]);
        Assert.Equal(new[] { "scene.heasset", "textures/atlas" }, staged);
        Assert.False(File.Exists(Path.Combine(StageRoot, "previous.heasset")));
        Assert.Equal("atlas", File.ReadAllText(Path.Combine(StageRoot, "textures", "atlas")));
    }

    [Fact]
    public void DuplicateSeparatorsAreNormalizedWithoutChangingCase() {
        Assert.Equal("Assets/Texture", XboxContentStager.NormalizeRelativePath("Assets//Texture"));
    }

    [Theory]
    [InlineData("source")]
    [InlineData("stage")]
    [InlineData("source-child")]
    public void JunctionsAreRejectedBeforeChangingSourceOrStage(string location) {
        string junction = location == "source-child" ? Path.Combine(Source, "alias") : Path.Combine(Root, "junction");
        string target = location == "stage" ? StageRoot : Source;
        CreateJunction(junction, target);
        try {
            string source = location == "source" ? junction : Source;
            string stage = location == "stage" ? junction : StageRoot;
            string artifact = location == "source-child" ? "alias/scene.heasset" : "scene.heasset";
            Assert.Throws<InvalidOperationException>(() => new XboxContentStager().Stage(
                [Artifact(artifact)], [], source, stage, CancellationToken.None));
            AssertPreviousStage();
            Assert.Equal("scene", File.ReadAllText(Path.Combine(Source, "scene.heasset")));
        } finally {
            Directory.Delete(junction);
        }
    }

    static void CreateJunction(string path, string target) {
        if (!OperatingSystem.IsWindows()) {
            Directory.CreateSymbolicLink(path, target);
            return;
        }
        System.Diagnostics.ProcessStartInfo start = new("rtk") { UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
        start.ArgumentList.Add("proxy");
        start.ArgumentList.Add("powershell");
        start.ArgumentList.Add("-NoProfile");
        start.ArgumentList.Add("-Command");
        start.ArgumentList.Add($"New-Item -ItemType Junction -Path '{path}' -Target '{target}' -ErrorAction Stop | Out-Null");
        using System.Diagnostics.Process process = System.Diagnostics.Process.Start(start);
        string output = process.StandardOutput.ReadToEnd();
        string error = process.StandardError.ReadToEnd();
        process.WaitForExit();
        Assert.True(process.ExitCode == 0, output + error);
    }

    IReadOnlyList<string> Stage(IReadOnlyList<PlatformBuildArtifact> artifacts, IReadOnlyList<string> cooked) =>
        new XboxContentStager().Stage(artifacts, cooked, Source, StageRoot, CancellationToken.None);

    static PlatformBuildArtifact Artifact(string path) => new(path, path, "hash", "scene", "xbox");

    void AssertPreviousStage() => Assert.Equal("previous", File.ReadAllText(Path.Combine(StageRoot, "previous.heasset")));

    public void Dispose() {
        if (Directory.Exists(Root)) Directory.Delete(Root, true);
    }
}
