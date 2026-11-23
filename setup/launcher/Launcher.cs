using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Interop;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using Forms = System.Windows.Forms;

[assembly: AssemblyTitle("Gagen's Playable Alien")]
[assembly: AssemblyProduct("Gagen's Playable Alien")]
[assembly: AssemblyDescription("Gagen's Playable Alien installer")]

internal static class Program
{
    internal const string Name = "Gagen's Playable Alien";

    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length != 0)
        {
            try { return Worker.Run(args); }
            catch (Exception error) { MessageBox.Show(error.ToString(), Name); return 5; }
        }

        var app = new Application();
        try
        {
            using (var layout = Assembly.GetExecutingAssembly().GetManifestResourceStream("Launcher.xaml"))
            {
                var window = (Window)XamlReader.Load(layout);
                new Launcher(window).Bind();
                return app.Run(window);
            }
        }
        catch (Exception error)
        {
            MessageBox.Show(error.ToString(), Name, MessageBoxButton.OK, MessageBoxImage.Error);
            return 5;
        }
    }
}

internal static class Worker
{
    internal static readonly string Root = AppDomain.CurrentDomain.BaseDirectory;
    private static readonly string Executable = Path.Combine(Root, "AlienModSetupWorker.exe");

    private static string Quote(string value)
    {
        return "\"" + Regex.Replace(value, "(\\\\*)(\"|$)", match =>
            new string('\\', match.Groups[1].Length * 2 + (match.Groups[2].Length == 0 ? 0 : 1)) +
            match.Groups[2].Value) + "\"";
    }

    internal static int Run(string[] args)
    {
        var info = new ProcessStartInfo(Executable, string.Join(" ", args.Select(Quote)))
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            WorkingDirectory = Root
        };
        using (var process = Process.Start(info))
        {
            process.WaitForExit();
            return process.ExitCode;
        }
    }

    internal static Task<Reply> Request(params string[] args)
    {
        return Task.Run(() =>
        {
            var temporary = Path.GetTempFileName();
            try
            {
                var discover = args[0] == "--find";
                var command = discover ? new[] { "--find", temporary } :
                    args.Concat(new[] { "--report", temporary }).ToArray();
                int code = Run(command);
                return new Reply(code, File.ReadAllText(temporary, discover ? Encoding.UTF8 : Encoding.Unicode));
            }
            finally { File.Delete(temporary); }
        });
    }

    internal sealed class Reply
    {
        internal readonly int Code;
        internal readonly string Text;
        internal Reply(int code, string text) { Code = code; Text = text; }
    }
}

internal sealed class GameStatus
{
    internal readonly bool Valid, Installed, Backup, Restore, Levels, Different;
    internal readonly bool Observed, PackageKnown, TreeSource, Library, Injector;
    internal readonly string Folder;
    internal bool HasRequiredFiles { get { return TreeSource && Library && Injector; } }

    internal GameStatus() { Folder = ""; }

    internal GameStatus(Worker.Reply reply)
    {
        var fields = reply.Text.Split('\n');
        if (reply.Code != 0 || (fields.Length != 7 && fields.Length != 10))
            throw new IOException("AlienModSetupWorker.exe (" + reply.Code + ")\n" + reply.Text);
        Observed = true;
        Folder = fields[0];
        Valid = fields[1] == "1";
        Installed = fields[2] == "1";
        Backup = fields[3] == "1";
        Restore = fields[4] == "1";
        Levels = fields[5] == "1";
        Different = fields[6] == "0";
        PackageKnown = fields.Length == 10;
        if (PackageKnown)
        {
            TreeSource = fields[7] == "1";
            Library = fields[8] == "1";
            Injector = fields[9] == "1";
        }
    }
}

internal sealed class Launcher
{
    private enum Page { Home, Setup, Logs, Links }

    private static readonly string[] Languages = { "English", "Русский", "Español", "日本語", "简体中文", "Français" };
    private static readonly string[] ThemeNames = { "Amber", "Terminal", "Ice" };
    private static readonly string[] PaletteKeys = { "Canvas", "Rail", "CardFill", "ControlFill", "FieldFill", "SelectedFill", "Line", "Accent" };
    private static readonly string[][] Palettes =
    {
        new[] { "#111615", "#171C19", "#1D2421", "#252D29", "#131917", "#36392F", "#45483D", "#FFB77D" },
        new[] { "#0C1915", "#10211B", "#132A22", "#19372A", "#0D2118", "#234436", "#35604E", "#91E5A8" },
        new[] { "#101722", "#141E2A", "#192432", "#213041", "#131D2A", "#25394C", "#364A5E", "#A1D8FF" }
    };
    private static readonly Dictionary<string, string> Links = new Dictionary<string, string>
    {
        { "NexusLink", "https://www.nexusmods.com/alienisolation/mods/181" },
        { "RepoLink", "https://github.com/KaratiNOZ/AI-Playable-Alien" },
        { "ReleasesLink", "https://github.com/KaratiNOZ/AI-Playable-Alien/releases" },
        { "DiscordLink", "https://discord.gg/tNUWtQ6m9m" },
        { "VideoLink", "https://www.youtube.com/@pirlet8014" },
        { "PhotoLink", "https://unsplash.com/photos/a-dark-tunnel-with-a-light-at-the-end-YUf4j-ktQJg" },
        { "FontLink", "https://github.com/jpt/barlow" }
    };
    private readonly Dictionary<string, string[]> strings = ReadStrings();
    private readonly string settings = Path.Combine(Worker.Root, "AlienModUI.ini");
    private readonly string logPath;
    private readonly Window window;
    private readonly DispatcherTimer pathDelay = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(350) };
    private readonly StringBuilder errors = new StringBuilder();
    private readonly Radar radar = new Radar();
    private GameStatus game = new GameStatus();
    private Page page;
    private int language, theme, stateRevision;
    private bool busy, checking, updatingPath, componentsInitialized, showErrors;
    private string statusKey = "Finding";
    private readonly string version;
    private bool? verified;
    private string verifiedFolder;
    private int verifiedComponents;

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern uint GetPrivateProfileString(string section, string key, string fallback,
        StringBuilder value, uint size, string file);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern bool WritePrivateProfileString(string section, string key, string value, string file);

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int size);

    internal Launcher(Window window)
    {
        this.window = window;
        var localized = Array.IndexOf(new[] { "en", "ru", "es", "ja", "zh", "fr" },
            CultureInfo.CurrentUICulture.TwoLetterISOLanguageName);
        if (!int.TryParse(ReadSetting("language"), out language) || language < 0 || language >= Languages.Length)
            language = Math.Max(0, localized);
        theme = Array.IndexOf(ThemeNames, ReadSetting("theme"));
        if (theme < 0) theme = 2;
        logPath = FindLog();
        var versionPath = Path.Combine(Worker.Root, "data", "setup-version.txt");
        version = File.Exists(versionPath) ? File.ReadAllText(versionPath).Trim() : "";
    }

    private T Element<T>(string name) where T : class { return (T)window.FindName(name); }
    private Button Button(string name) { return Element<Button>(name); }
    private TextBox PathBox { get { return Element<TextBox>("GamePath"); } }
    private string Text(string key) { return strings[key][language]; }

    private static Dictionary<string, string[]> ReadStrings()
    {
        var result = new Dictionary<string, string[]>();
        using (var reader = new StreamReader(Assembly.GetExecutingAssembly().GetManifestResourceStream("strings.h")))
        {
            string row;
            while ((row = reader.ReadLine()) != null)
            {
                int suffix = row.LastIndexOf("// ", StringComparison.Ordinal);
                if (suffix < 0) continue;
                var values = Regex.Matches(row.Substring(0, suffix), "L\"((?:\\\\.|[^\"\\\\])*)\"");
                if (values.Count == Languages.Length)
                    result.Add(row.Substring(suffix + 3).Trim(),
                        values.Cast<Match>().Select(value => Regex.Unescape(value.Groups[1].Value)).ToArray());
            }
        }
        using (var reader = new StreamReader(Assembly.GetExecutingAssembly().GetManifestResourceStream("ui.tsv")))
        {
            string row;
            while ((row = reader.ReadLine()) != null)
            {
                if (row.Length == 0) continue;
                var columns = row.Split('\t');
                if (columns.Length != Languages.Length + 1)
                    throw new FormatException("Invalid UI string: " + columns[0]);
                result.Add(columns[0], columns.Skip(1).ToArray());
            }
        }
        return result;
    }

    internal void Bind()
    {
        var pack = new Uri("pack://application:,,,/AlienModSetup;component/");
        var display = new FontFamily(pack, "./assets/#Barlow Semi Condensed");
        window.Icon = BitmapFrame.Create(new Uri(pack, "assets/launcher.ico"));
        Element<TextBlock>("HeroTitle").FontFamily = display;
        Element<TextBlock>("Brand").FontFamily = display;
        Element<ImageBrush>("HeroPhoto").ImageSource = new BitmapImage(new Uri(pack, "assets/tunnel.jpg"));
        Element<ContentControl>("Radar").Content = radar;
        var selector = Element<ComboBox>("Language");
        selector.ItemsSource = Languages;
        selector.SelectedIndex = language;
        selector.SelectionChanged += (sender, args) =>
        {
            language = selector.SelectedIndex;
            if (language < 0) return;
            try { SaveSetting("language", language.ToString()); }
            catch (Exception error) { AddError(error.ToString()); }
            Relabel();
        };
        Button("NavHome").Click += (sender, args) => Show(Page.Home);
        Button("NavSetup").Click += (sender, args) => Show(Page.Setup);
        Button("NavLogs").Click += (sender, args) => Show(Page.Logs);
        Button("NavLinks").Click += (sender, args) => Show(Page.Links);
        foreach (var link in Links)
        {
            var button = Button(link.Key);
            button.ToolTip = link.Value;
            button.Click += (sender, args) => Open(link.Value);
        }
        Element<ComboBox>("Theme").SelectionChanged += (sender, args) =>
        {
            int selected = Element<ComboBox>("Theme").SelectedIndex;
            if (selected < 0 || selected == theme) return;
            theme = selected;
            ApplyTheme();
            try { SaveSetting("theme", ThemeNames[theme]); }
            catch (Exception error) { AddError(error.ToString()); }
        };
        Button("Primary").Click += async (sender, args) =>
        {
            if (page == Page.Home && !game.Installed) Show(Page.Setup);
            else await Perform(page == Page.Home ? "--play" : "--install");
        };
        Button("Find").Click += async (sender, args) => await Discover();
        Button("Browse").Click += (sender, args) =>
        {
            try { Browse(); }
            catch (Exception error) { AddError(error.ToString()); }
        };
        Button("Verify").Click += async (sender, args) => await Perform("--verify");
        Button("Restore").Click += async (sender, args) => await Perform("--uninstall");
        Button("Play").Click += async (sender, args) => await Perform("--play");
        Button("OpenLogs").Click += (sender, args) => Open(Worker.Root);
        Button("OpenInstallerLog").Click += (sender, args) => Open(logPath);
        Button("ToggleErrors").Click += (sender, args) => { showErrors = !showErrors; RefreshDetails(); };
        Button("Copy").Click += (sender, args) =>
        {
            try { Clipboard.SetText(BuildReport(true)); SetStatus("Copied"); }
            catch (Exception error) { AddError(error.ToString()); }
        };
        foreach (var key in new[] { "Tree", "Levels" })
        {
            var option = Element<CheckBox>(key);
            option.Checked += (sender, args) => { UpdateControls(); RefreshDetails(); };
            option.Unchecked += (sender, args) => { UpdateControls(); RefreshDetails(); };
        }
        PathBox.TextChanged += (sender, args) =>
        {
            if (updatingPath) return;
            ++stateRevision;
            checking = true;
            pathDelay.Stop();
            pathDelay.Start();
            UpdateControls();
        };
        pathDelay.Tick += async (sender, args) => { pathDelay.Stop(); await Inspect(PathBox.Text, true, false); };
        window.Loaded += async (sender, args) =>
        {
            UpdateRadar();
            var saved = ReadSetting("game");
            if (saved.Length == 0) { await Discover(); return; }
            int check = stateRevision + 1;
            if (!await Inspect(saved, true, true) && check == stateRevision && errors.Length == 0)
                await Discover();
        };
        window.StateChanged += (sender, args) => UpdateRadar();
        window.SourceInitialized += (sender, args) => ApplyCaption();
        window.SizeChanged += (sender, args) => ResizeComponents();
        window.Closing += (sender, args) => { if (busy) args.Cancel = true; };
        window.Closed += (sender, args) => { radar.Active = false; pathDelay.Stop(); };
        ApplyTheme();
        Relabel();
        Show(Page.Home);
        ResizeComponents();
    }

    private void ResizeComponents()
    {
        Element<UniformGrid>("ComponentGrid").Columns = window.ActualWidth >= 1030 ? 3 : 1;
    }

    private void Relabel()
    {
        window.Title = Program.Name;
        Element<Label>("LanguageLabel").Content = Text("Language");
        Element<Label>("ThemeLabel").Content = Text("Theme");
        var themes = Element<ComboBox>("Theme");
        themes.ItemsSource = ThemeNames.Select(Text).ToArray();
        themes.SelectedIndex = theme;
        Element<Label>("GameLabel").Content = Text("Game");
        Element<TextBlock>("Version").Text = version.Length == 0 ? Text("Local") : version;
        Element<TextBlock>("SetupTitle").Text = Text("Actions");
        Element<TextBlock>("LogsTitle").Text = Text("Logs");
        Element<TextBlock>("LinksTitle").Text = Text("Links");
        Element<TextBlock>("CreditsTitle").Text = Text("Credits");
        Element<TextBlock>("ComponentTitle").Text = Text("Components");
        foreach (var key in new[] { "Tree", "Levels", "Shortcut" })
            Element<TextBlock>(key + "Title").Text = Text(key);
        var labels = new Dictionary<string, string>
        {
            { "HomeIntro", "HeroDescription" }, { "HomeGameLabel", "GameDetection" },
            { "HomeModLabel", "ModState" }, { "HomeFilesLabel", "FileHealth" }, { "HomeVersionLabel", "ModVersion" },
            { "TreeDesc", "TreeAbout" }, { "LevelsDesc", "LevelsAbout" }, { "ShortcutDesc", "ShortcutAbout" },
            { "MaintenanceTitle", "Maintenance" }, { "MaintenanceIntro", "MaintenanceAbout" },
            { "LogsIntro", "DiagnosticsAbout" }, { "DiagGameLabel", "GameDetection" }, { "DiagPathLabel", "Game" },
            { "DiagTreeLabel", "ModState" }, { "DiagFilesLabel", "PackageFiles" },
            { "DiagHealthLabel", "FileHealth" }, { "ReportTitle", "DiagnosticsReport" }
        };
        foreach (var label in labels) Element<TextBlock>(label.Key).Text = Text(label.Value);
        Button("NavHome").Content = Text("Home");
        Button("NavSetup").Content = Text("Actions");
        Button("NavLogs").Content = Text("Diagnostics");
        Button("NavLinks").Content = Text("Links");
        Button("ReleasesLink").Content = Text("Releases");
        foreach (var item in new[] { "Nexus", "Repo", "Release", "Discord", "Video", "Photo", "Font" })
            Button((item == "Release" ? "Releases" : item) + "Link").Tag = Text(item + "About");
        Button("Find").Content = Text("Detect");
        Button("OpenLogs").Content = Text("Open");
        Button("OpenInstallerLog").Content = Text("InstallerLog");
        foreach (var key in new[] { "Browse", "Verify", "Restore", "Play" })
            Button(key).Content = Text(key);
        Button("Copy").Content = Text("CopyReport");
        RefreshDetails();
        SetStatus(statusKey);
        UpdateControls();
    }

    private void Show(Page target)
    {
        bool changed = page != target;
        page = target;
        foreach (var item in new[] { "Home", "Setup", "Logs", "Links" })
        {
            bool selected = item == target.ToString();
            Element<FrameworkElement>(item + "Page").Visibility = selected ? Visibility.Visible : Visibility.Collapsed;
            Button("Nav" + item).Tag = selected ? "Selected" : null;
        }
        if (changed && window.IsLoaded && SystemParameters.ClientAreaAnimation)
            Element<FrameworkElement>(target + "Page").BeginAnimation(UIElement.OpacityProperty,
                new DoubleAnimation(0.82, 1, TimeSpan.FromMilliseconds(140)) { FillBehavior = FillBehavior.Stop });
        Element<ScrollViewer>("PageScroll").ScrollToTop();
        UpdateControls();
        if (page == Page.Logs) RefreshDetails();
        UpdateRadar();
    }

    private void UpdateRadar()
    {
        radar.Active = page == Page.Home && window.WindowState != WindowState.Minimized && window.IsLoaded;
    }

    private void ApplyTheme()
    {
        var colors = Palettes[theme];
        for (int i = 0; i < PaletteKeys.Length; ++i)
        {
            var brush = new SolidColorBrush((Color)ColorConverter.ConvertFromString(colors[i]));
            brush.Freeze();
            window.Resources[PaletteKeys[i]] = brush;
        }
        var backdrop = ((SolidColorBrush)window.Resources["CardFill"]).Color;
        backdrop.A = 218;
        var radarFill = new SolidColorBrush(backdrop);
        radarFill.Freeze();
        window.Resources["RadarFill"] = radarFill;
        radar.Accent = ((SolidColorBrush)window.Resources["Accent"]).Color;
        ApplyCaption();
    }

    private void ApplyCaption()
    {
        var handle = new WindowInteropHelper(window).Handle;
        if (handle == IntPtr.Zero) return;
        int dark = 1;
        if (DwmSetWindowAttribute(handle, 20, ref dark, sizeof(int)) != 0)
            DwmSetWindowAttribute(handle, 19, ref dark, sizeof(int));
        var rail = ((SolidColorBrush)window.Resources["Rail"]).Color;
        int color = rail.R | (rail.G << 8) | (rail.B << 16);
        DwmSetWindowAttribute(handle, 35, ref color, sizeof(int));
    }

    private void UpdateControls()
    {
        bool ready = !busy && !checking && game.Valid;
        bool components = Components() != 0;
        bool runtimeAvailable = !game.PackageKnown || (game.Library && game.Injector);
        Button("Primary").Visibility = page == Page.Home || page == Page.Setup ? Visibility.Visible : Visibility.Collapsed;
        Button("Primary").Content = Text(page == Page.Home ? (game.Installed ? "Play" : "Actions") :
            (game.Installed || game.Backup ? "Update" : "Install"));
        Button("Primary").IsEnabled = page == Page.Home ?
            !busy && (!game.Installed || (ready && runtimeAvailable)) : ready && components && runtimeAvailable;
        Button("Verify").IsEnabled = ready && components;
        Button("Restore").IsEnabled = ready && game.Restore;
        Button("Play").IsEnabled = ready;
        Button("Find").IsEnabled = !busy;
        Button("Browse").IsEnabled = !busy;
        Element<ComboBox>("Language").IsEnabled = !busy;
        PathBox.IsEnabled = !busy;
        Element<CheckBox>("Tree").IsEnabled = !busy && game.Observed && (!game.PackageKnown || game.TreeSource);
        Element<CheckBox>("Shortcut").IsEnabled = !busy && game.Observed && (!game.PackageKnown || game.Injector);
        Element<CheckBox>("Levels").IsEnabled = !busy && game.Levels;
        Element<CheckBox>("Tree").Tag = game.PackageKnown && !game.TreeSource ? "Unavailable" : null;
        Element<CheckBox>("Shortcut").Tag = game.PackageKnown && !game.Injector ? "Unavailable" : null;
        Element<CheckBox>("Levels").Tag = game.Observed && !game.Levels ? "Unavailable" : null;
        Element<ProgressBar>("Progress").Visibility = busy ? Visibility.Visible : Visibility.Collapsed;
        if (!busy) Element<TextBlock>("ProgressText").Visibility = Visibility.Collapsed;
        UpdateGameViews();
    }

    private int Components()
    {
        return ((!game.PackageKnown || game.TreeSource) && Element<CheckBox>("Tree").IsChecked == true ? 1 : 0) |
            (game.Levels && Element<CheckBox>("Levels").IsChecked == true ? 2 : 0);
    }

    private string GameState()
    {
        return checking || (busy && statusKey == "Finding") ? "Finding" :
            !game.Observed ? "Unknown" : game.Valid ? "Found" : "NotFound";
    }

    private string InstallationState()
    {
        return checking || !game.Valid ? "Unknown" : game.Installed ? "TreeInstalled" : "NotInstalled";
    }

    private string FileState()
    {
        if (game.PackageKnown && !game.HasRequiredFiles) return "MissingRequired";
        if (checking || verified == null || verifiedFolder != game.Folder || verifiedComponents != Components())
            return "NotChecked";
        return verified.Value ? "FilesOk" : "FilesBad";
    }

    private string PathState()
    {
        return checking || !game.Observed ? "Unknown" : game.Valid ? "ValidPath" : "InvalidPath";
    }

    private string PackageState()
    {
        return !game.PackageKnown ? "Unknown" : game.HasRequiredFiles ? "Present" : "MissingRequired";
    }

    private void SetIndicator(string element, string key)
    {
        string tone;
        switch (key)
        {
            case "Found": case "ValidPath": case "TreeInstalled": case "Present":
            case "FilesOk": case "ReadyToSelect": tone = "Good"; break;
            case "NotFound": case "InvalidPath": case "FilesBad": case "MissingRequired":
            case "TreeMissing": case "MissingLevels": case "Absent": tone = "Warning"; break;
            default: tone = "Muted"; break;
        }
        var field = Element<TextBlock>(element);
        field.Text = Text(key);
        field.SetResourceReference(TextBlock.ForegroundProperty, tone);
    }

    private void UpdateGameViews()
    {
        SetIndicator("HomeGame", GameState());
        SetIndicator("SetupGameState", GameState());
        SetIndicator("HomeMod", InstallationState());
        SetIndicator("HomeFiles", FileState());
        Element<TextBlock>("HomeVersion").Text = version.Length == 0 ? Text("Local") : version;
        SetIndicator("DiagGame", GameState());
        SetIndicator("DiagPath", PathState());
        SetIndicator("DiagTree", InstallationState());
        SetIndicator("DiagFiles", PackageState());
        SetIndicator("DiagHealth", FileState());
        var hint = Element<TextBlock>("GamePathHint");
        hint.Visibility = game.Observed && !checking && (!game.Valid || game.Different)
            ? Visibility.Visible : Visibility.Collapsed;
        hint.Text = Text(game.Different ? "Different" : "MissingGame");
        SetIndicator("TreeAvailability", !game.PackageKnown ? "Unknown" :
            game.TreeSource ? "ReadyToSelect" : "TreeMissing");
        SetIndicator("LevelNote", !game.Observed ? "Unknown" :
            game.Levels ? "ReadyToSelect" : "MissingLevels");
        SetIndicator("ShortcutAvailability", !game.PackageKnown ? "Unknown" :
            game.Injector ? "ReadyToSelect" : "Absent");
    }

    private void SetStatus(string key)
    {
        statusKey = key;
        var label = Element<TextBlock>("Status");
        label.Text = Text(key);
        if (key == "Failed") label.Foreground = Brushes.LightSalmon;
        else label.SetResourceReference(TextBlock.ForegroundProperty,
            key == "MissingGame" || key == "Different" || key == "MissingRequired" ? "Warning" : "Muted");
    }

    private async Task<bool> Inspect(string path, bool reportLocation, bool normalize)
    {
        int revision = ++stateRevision;
        checking = true;
        UpdateControls();
        try
        {
            var found = new GameStatus(await Worker.Request("--status", path));
            if (revision != stateRevision) return false;
            if (!string.Equals(found.Folder, game.Folder, StringComparison.OrdinalIgnoreCase) ||
                found.Installed != game.Installed || found.Levels != game.Levels ||
                found.Valid != game.Valid || found.HasRequiredFiles != game.HasRequiredFiles)
                verified = null;
            game = found;
            if (!componentsInitialized)
            {
                componentsInitialized = true;
                Element<CheckBox>("Levels").IsChecked = game.Levels;
            }
            if (normalize)
            {
                updatingPath = true;
                try { PathBox.Text = game.Folder; }
                finally { updatingPath = false; }
            }
            if (reportLocation)
                SetStatus(!game.Valid ? "MissingGame" : game.PackageKnown && !game.HasRequiredFiles ? "MissingRequired" :
                    game.Different ? "Different" : game.Installed ? "Installed" : "Ready");
            return game.Valid;
        }
        catch (Exception error)
        {
            if (revision == stateRevision)
            {
                game = new GameStatus();
                AddError(error.ToString());
            }
            return false;
        }
        finally
        {
            if (revision == stateRevision) { checking = false; UpdateControls(); RefreshDetails(); }
        }
    }

    private async Task Discover()
    {
        if (busy) return;
        busy = true;
        pathDelay.Stop();
        ++stateRevision;
        SetStatus("Finding");
        Element<ProgressBar>("Progress").IsIndeterminate = true;
        UpdateControls();
        try
        {
            var result = await Worker.Request("--find");
            await Inspect(result.Text, true, true);
        }
        catch (Exception error) { AddError(error.ToString()); }
        finally { busy = false; UpdateControls(); }
    }

    private void Browse()
    {
        using (var dialog = new Forms.FolderBrowserDialog
        {
            Description = Text("Pick"),
            SelectedPath = game.Valid ? game.Folder : "",
            ShowNewFolderButton = false
        })
        {
            if (dialog.ShowDialog() == Forms.DialogResult.OK) PathBox.Text = dialog.SelectedPath;
        }
    }

    private async Task Perform(string action)
    {
        if (busy || checking || !game.Valid) return;
        if (action == "--uninstall" || (action == "--install" && game.Different))
        {
            string message = Text(action == "--uninstall" ? "Restore" : "Different") + "\n\n" + Text("ConfirmContinue");
            if (MessageBox.Show(window, message, window.Title, MessageBoxButton.YesNo, MessageBoxImage.Warning,
                MessageBoxResult.No) != MessageBoxResult.Yes) return;
        }
        busy = true;
        bool writing = action == "--install" || action == "--uninstall";
        bool verifying = action == "--verify";
        bool updating = action == "--install" && (game.Installed || game.Backup);
        string fallback = action == "--uninstall" ? "Restoring" : action == "--install" ?
            (updating ? "Updating" : "Writing") : verifying ? "Checking" : "Launching";
        SetStatus(fallback);
        var progress = Element<ProgressBar>("Progress");
        progress.IsIndeterminate = true;
        progress.Value = 0;
        string progressPath = null;
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(85) };
        UpdateControls();
        try
        {
            string path = game.Folder;
            int selection = Components();
            bool shortcut = (!game.PackageKnown || game.Injector) && Element<CheckBox>("Shortcut").IsChecked == true;
            var args = new List<string> { action, path, "--lang", language.ToString(),
                "--components", (action == "--uninstall" ? 3 : selection).ToString(), "--managed", "1",
                "--shortcut", shortcut ? "1" : "0" };
            if (writing || verifying)
            {
                progressPath = Path.GetTempFileName();
                args.AddRange(new[] { "--progress", progressPath });
                timer.Tick += (sender, eventArgs) => ReadProgress(progressPath, updating, progress);
                timer.Start();
            }
            var reply = await Worker.Request(args.ToArray());
            timer.Stop();
            bool valid = await Inspect(game.Folder, action == "--play", true);
            if (writing) verified = null;
            if (verifying)
            {
                verified = reply.Code == 0;
                verifiedFolder = path;
                verifiedComponents = selection;
            }
            if (reply.Code != 0)
                AddError(reply.Text.Length > 0 ? reply.Text : "AlienModSetupWorker.exe (" + reply.Code + ")",
                    reply.Text.Length > 0);
            else if (valid && action != "--play")
                SetStatus(action == "--uninstall" ? "Restored" : action == "--verify" ? "Verified" : "Done");
        }
        catch (Exception error) { AddError(error.ToString()); }
        finally
        {
            timer.Stop();
            if (progressPath != null)
            {
                try { File.Delete(progressPath); }
                catch (IOException) { }
                catch (UnauthorizedAccessException) { }
            }
            busy = false;
            UpdateControls();
            RefreshDetails();
        }
    }

    private void ReadProgress(string path, bool updating, ProgressBar progress)
    {
        try
        {
            string line = null;
            using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            using (var reader = new StreamReader(stream))
                while (!reader.EndOfStream) line = reader.ReadLine();
            if (string.IsNullOrEmpty(line)) return;
            var fields = line.Split(' ');
            int position;
            if (fields.Length != 2 || !int.TryParse(fields[1], out position) || !strings.ContainsKey(fields[0]))
                return;
            SetStatus(fields[0] == "Writing" && updating ? "Updating" : fields[0]);
            progress.IsIndeterminate = position == 0;
            progress.Value = position;
            var percent = Element<TextBlock>("ProgressText");
            percent.Text = (position / 100.0).ToString("P0", CultureInfo.CurrentCulture);
            percent.Visibility = position > 0 ? Visibility.Visible : Visibility.Collapsed;
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }

    private void RefreshDetails()
    {
        Button("ToggleErrors").Content = Text(showErrors ? "HideErrors" : "ShowErrors");
        Button("ToggleErrors").IsEnabled = errors.Length > 0;
        Element<TextBox>("Details").Text = showErrors ? errors.ToString() : BuildReport(false);
    }

    private string BuildReport(bool includeErrors)
    {
        var report = new StringBuilder();
        report.AppendLine(Program.Name);
        report.AppendLine(Text("ModVersion") + ": " + (version.Length == 0 ? Text("Local") : version));
        report.AppendLine(Text("GameDetection") + ": " + Text(GameState()));
        report.AppendLine(Text("Game") + ": " + Text(PathState()));
        if (game.Folder.Length != 0) report.AppendLine(game.Folder);
        report.AppendLine(Text("ModState") + ": " + Text(InstallationState()));
        report.AppendLine(Text("FileHealth") + ": " + Text(FileState()));
        report.AppendLine();
        report.AppendLine(Text("PackageFiles") + ": " + Text(PackageState()));
        report.AppendLine("DATA\\BINARY_BEHAVIOR\\_DIRECTORY_CONTENTS.BML — " + FilePresence(game.TreeSource));
        report.AppendLine("AlienModUI.dll — " + FilePresence(game.Library));
        report.AppendLine("AlienInjectorUI.exe — " + FilePresence(game.Injector));
        report.AppendLine();
        report.AppendLine(Text("LogGuide"));
        report.AppendLine(Text("LogPath") + " — " + Worker.Root);
        report.AppendLine(Text("InstallerLog") + " — " + logPath);
        if (includeErrors && errors.Length != 0) report.AppendLine().Append(errors);
        return report.ToString();
    }

    private string FilePresence(bool present)
    {
        return Text(!game.PackageKnown ? "Unknown" : present ? "Present" : "Absent");
    }

    private void AddError(string details, bool logged = false)
    {
        if (errors.Length != 0) errors.Append("\n\n────────────────────────\n\n");
        errors.Append(details);
        if (!logged)
        {
            try { File.AppendAllText(logPath, DateTime.Now.ToString("s") + "\n" + details + "\n\n", Encoding.UTF8); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
        showErrors = true;
        SetStatus("Failed");
        RefreshDetails();
        Show(Page.Logs);
    }

    private void Open(string path)
    {
        try { Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
        catch (Exception error) { AddError(Text("OpenError") + "\n" + path + "\n" + error); }
    }

    private string ReadSetting(string key)
    {
        var value = new StringBuilder(4096);
        GetPrivateProfileString("setup", key, "", value, (uint)value.Capacity, settings);
        return value.ToString();
    }

    private void SaveSetting(string key, string value)
    {
        if (!File.Exists(settings))
        {
            try { using (var file = new FileStream(settings, FileMode.CreateNew)) { file.WriteByte(255); file.WriteByte(254); } }
            catch (IOException) { if (!File.Exists(settings)) throw; }
        }
        if (!WritePrivateProfileString("setup", key, value, settings))
            throw new IOException(settings);
    }

    private static string FindLog()
    {
        var local = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "AlienMod");
        foreach (var directory in new[] { Worker.Root, local })
        {
            var path = Path.Combine(directory, "AlienModSetup.log");
            try { Directory.CreateDirectory(directory); using (File.Open(path, FileMode.Append)) { } return path; }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
        return Path.Combine(Worker.Root, "AlienModSetup.log");
    }
}
