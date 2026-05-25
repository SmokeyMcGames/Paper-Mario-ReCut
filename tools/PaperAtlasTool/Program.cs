using System.Text;

namespace N64TextureAtlasEditor;

internal static class Program
{
    static readonly string LogDir = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "N64TextureAtlasEditor");

    static readonly string LogPath = Path.Combine(LogDir, "startup_crash_log.txt");

    [STAThread]
    static void Main(string[] args)
    {
        try
        {
            Directory.CreateDirectory(LogDir);

            Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);

            Application.ThreadException += (_, e) =>
            {
                LogException("Application.ThreadException", e.Exception);
                MessageBox.Show(
                    "The app hit an error:\n\n" + e.Exception.Message + "\n\nCrash log:\n" + LogPath,
                    "N64 Texture Atlas Editor Error",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
            };

            AppDomain.CurrentDomain.UnhandledException += (_, e) =>
            {
                if (e.ExceptionObject is Exception ex)
                    LogException("AppDomain.UnhandledException", ex);
            };

            ApplicationConfiguration.Initialize();
            Application.Run(new MainForm(ParseOption(args, "--source"), ParseOption(args, "--replacements")));
        }
        catch (Exception ex)
        {
            LogException("Startup crash", ex);

            MessageBox.Show(
                "The app failed to start:\n\n" + ex.Message + "\n\nCrash log:\n" + LogPath,
                "N64 Texture Atlas Editor Startup Error",
                MessageBoxButtons.OK,
                MessageBoxIcon.Error);
        }
    }

    static string ParseOption(string[] args, string name)
    {
        for (int i = 0; i < args.Length; i++)
        {
            if (!string.Equals(args[i], name, StringComparison.OrdinalIgnoreCase))
                continue;

            if (i + 1 < args.Length)
                return args[i + 1];
        }

        return "";
    }

    static void LogException(string title, Exception ex)
    {
        try
        {
            Directory.CreateDirectory(LogDir);

            var sb = new StringBuilder();
            sb.AppendLine("==== " + title + " ====");
            sb.AppendLine(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss"));
            sb.AppendLine(ex.ToString());
            sb.AppendLine();

            File.AppendAllText(LogPath, sb.ToString());
        }
        catch
        {
        }
    }
}
