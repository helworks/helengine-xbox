namespace helengine.xbox.builder;

public static class Program {
    public static int Main(string[] args) {
        if (args.Length == 1 && string.Equals(args[0], "--describe", StringComparison.OrdinalIgnoreCase)) {
            XboxPlatformAssetBuilder builder = new();
            Console.WriteLine(builder.Descriptor.BuilderId);
            Console.WriteLine(builder.Descriptor.TargetPlatformId);
            Console.WriteLine(builder.Definition.DisplayName);
            return 0;
        }

        Console.WriteLine("helengine.xbox.builder --describe");
        return 0;
    }
}
