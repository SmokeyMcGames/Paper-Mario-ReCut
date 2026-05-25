using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Text.Json;

namespace N64TextureAtlasEditor;

public class Piece
{
    public string FileName = "";
    public string FullPath = "";
    public Bitmap Image = new(1, 1);
    public Rectangle Bounds;
    public bool Included = true;
    public bool Locked = false;
}

public class LayoutFile
{
    public string combined_file { get; set; } = "combined_texture.png";
    public int atlas_width { get; set; }
    public int atlas_height { get; set; }
    public List<LayoutPiece> pieces { get; set; } = new();
    public List<LayoutPiece> removed_pieces { get; set; } = new();
}

public class LayoutPiece
{
    public string filename { get; set; } = "";
    public int x { get; set; }
    public int y { get; set; }
    public int width { get; set; }
    public int height { get; set; }
    public int order { get; set; }
    public bool included { get; set; }
}

public class StatePiece
{
    public string FileName { get; set; } = "";
    public Rectangle Bounds { get; set; }
    public bool Included { get; set; }
    public bool Locked { get; set; }
}

public class EditorState
{
    public List<StatePiece> Pieces { get; set; } = new();
    public List<string> Selected { get; set; } = new();
    public float Zoom { get; set; }
    public PointF Pan { get; set; }
}


public class AppSettings
{
    public string ImageEditorPath { get; set; } = "";
    public string ReplacementsFolder { get; set; } = "";
}

public class MainForm : Form
{
    const string CombinedName = "combined_texture.png";
    const string LayoutName = "n64_texture_layout.json";

    readonly List<Piece> pieces = new();
    readonly HashSet<Piece> selected = new();
    readonly Stack<EditorState> undo = new();
    readonly Stack<EditorState> redo = new();

    bool updatingList = false;

    readonly Color ThemeBg = Color.FromArgb(252, 244, 218);
    readonly Color ThemePanel = Color.FromArgb(255, 250, 231);
    readonly Color ThemeRed = Color.FromArgb(210, 64, 58);
    readonly Color ThemeBlue = Color.FromArgb(62, 143, 206);
    readonly Color ThemeYellow = Color.FromArgb(245, 190, 72);
    readonly Color ThemeGreen = Color.FromArgb(92, 165, 91);
    readonly Color ThemeInk = Color.FromArgb(64, 50, 46);
    readonly Color ThemeBorder = Color.FromArgb(120, 91, 65);

    string folder = "";
    string replacementsFolder = "";
    string imageEditorPath = "";
    readonly string startupFolder;
    readonly string startupReplacementsFolder;
    readonly ContextMenuStrip pieceMenu = new();
    Piece? contextPiece = null;
    string lastSelectedLayoutFolder = "";

    readonly string settingsDir = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "N64TextureAtlasEditor");

    string SettingsPath => Path.Combine(settingsDir, "settings.json");

    int atlasW = 64;
    int atlasH = 64;
    float zoom = 2f;
    PointF pan = new(60, 60);

    bool panning;
    bool dragging;
    bool marquee;
    Point lastMouse;
    Point mouseDown;
    PointF dragStartWorld;
    Rectangle marqueeRect;
    readonly Dictionary<Piece, Rectangle> dragStart = new();

    readonly Panel canvas = new() { Dock = DockStyle.Fill, BackColor = Color.FromArgb(35, 35, 35), TabStop = true };
    readonly ListBox list = new() { Dock = DockStyle.Fill, SelectionMode = SelectionMode.MultiExtended };
    readonly TextBox folderBox = new() { Width = 420 };
    readonly TextBox replacementsBox = new() { Width = 420 };
    readonly NumericUpDown paddingBox = new() { Minimum = 0, Maximum = 128, Value = 0, Width = 55 };
    readonly NumericUpDown snapBox = new() { Minimum = 0, Maximum = 128, Value = 8, Width = 55 };
    readonly NumericUpDown edgeBox = new() { Minimum = 1, Maximum = 100, Value = 18, Width = 55 };
    readonly CheckBox snapCheck = new() { Text = "Snap", Checked = true, AutoSize = true };
    readonly CheckBox scaledSplitCheck = new() { Text = "Scaled Split", Checked = true, AutoSize = true };
    readonly Label status = new() { Dock = DockStyle.Bottom, Height = 28, Padding = new Padding(8, 5, 8, 5) };

    public MainForm(string startupFolder = "", string startupReplacementsFolder = "")
    {
        this.startupFolder = startupFolder;
        this.startupReplacementsFolder = startupReplacementsFolder;

        Text = "N64 Texture Atlas Editor";
        Font = new Font("Segoe UI", 9f);
        Width = 1350;
        Height = 850;
        MinimumSize = new Size(1000, 650);
        KeyPreview = true;
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer, true);

        BuildUi();
        BuildPieceMenu();
        WireEvents();
        LoadSettings();

        ApplyStartupFolders();

        Shown += (_, _) =>
        {
            AskForEditorOnFirstStart();
            if (Directory.Exists(folder))
                LoadPngs();
        };

        SetStatus("Ready. Load a folder of PNG pieces.");
    }

    void ApplyStartupFolders()
    {
        if (Directory.Exists(startupFolder))
        {
            folder = startupFolder;
            folderBox.Text = folder;
        }

        if (!string.IsNullOrWhiteSpace(startupReplacementsFolder))
        {
            replacementsFolder = startupReplacementsFolder;
            replacementsBox.Text = replacementsFolder;
            SaveSettings();
        }
    }

    Button Btn(string text, Action action, Color? color = null) => MakeThemedButton(text, action, color);


    Button MakeThemedButton(string text, Action action, Color? color = null)
    {
        var b = new Button
        {
            Text = text,
            AutoSize = false,
            Height = 30,
            Width = Math.Max(74, text.Length * 8 + 20),
            Margin = new Padding(3),
            FlatStyle = FlatStyle.Flat,
            BackColor = color ?? ThemeYellow,
            ForeColor = Color.White,
            Font = new Font("Segoe UI Semibold", 9f, FontStyle.Bold)
        };

        b.FlatAppearance.BorderColor = ThemeBorder;
        b.FlatAppearance.BorderSize = 1;
        b.Click += (_, _) => action();
        return b;
    }

    Label MakeLabel(string text)
    {
        return new Label
        {
            Text = text,
            AutoSize = true,
            ForeColor = ThemeInk,
            Font = new Font("Segoe UI Semibold", 9f, FontStyle.Bold),
            Padding = new Padding(0, 7, 2, 0),
            Margin = new Padding(6, 2, 2, 2)
        };
    }

    void StyleTextBox(TextBox box)
    {
        box.BorderStyle = BorderStyle.FixedSingle;
        box.BackColor = Color.White;
        box.ForeColor = ThemeInk;
        box.Font = new Font("Segoe UI", 9f);
        box.Height = 26;
        box.Margin = new Padding(3);
    }

    void StyleNumeric(NumericUpDown box)
    {
        box.BackColor = Color.White;
        box.ForeColor = ThemeInk;
        box.Font = new Font("Segoe UI", 9f);
        box.Height = 26;
        box.Margin = new Padding(3);
    }

    void StyleCheck(CheckBox box)
    {
        box.ForeColor = ThemeInk;
        box.Font = new Font("Segoe UI Semibold", 9f, FontStyle.Bold);
        box.BackColor = ThemePanel;
        box.Margin = new Padding(6, 6, 4, 4);
    }


    FlowLayoutPanel MakeOptionGroup(string title)
    {
        var outer = new FlowLayoutPanel
        {
            AutoSize = true,
            WrapContents = false,
            FlowDirection = FlowDirection.LeftToRight,
            Padding = new Padding(6, 4, 6, 4),
            Margin = new Padding(4),
            BackColor = ThemePanel,
            BorderStyle = BorderStyle.FixedSingle
        };

        outer.Controls.Add(new Label
        {
            Text = title,
            AutoSize = true,
            ForeColor = ThemeBorder,
            Font = new Font("Segoe UI Semibold", 9f, FontStyle.Bold),
            Padding = new Padding(0, 7, 6, 0),
            Margin = new Padding(0, 0, 4, 0)
        });

        return outer;
    }

    Panel MakeDivider()
    {
        return new Panel
        {
            Width = 2,
            Height = 30,
            Margin = new Padding(6, 1, 6, 1),
            BackColor = ThemeBorder
        };
    }


    void BuildUi()
    {
        BackColor = ThemeBg;

        var top = new FlowLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            Padding = new Padding(8, 6, 8, 6),
            WrapContents = true,
            BackColor = ThemePanel
        };

        Button Btn(string text, Action action, Color? color = null) => MakeThemedButton(text, action, color);

        StyleTextBox(folderBox);
        StyleTextBox(replacementsBox);
        StyleNumeric(paddingBox);
        StyleNumeric(snapBox);
        StyleNumeric(edgeBox);
        StyleCheck(snapCheck);
        StyleCheck(scaledSplitCheck);

        folderBox.Width = 360;
        replacementsBox.Width = 360;

        var sourceGroup = MakeOptionGroup("Source PNGs");
        sourceGroup.Controls.Add(MakeLabel("Folder:"));
        sourceGroup.Controls.Add(folderBox);
        sourceGroup.Controls.Add(Btn("Browse", Browse, ThemeBlue));
        sourceGroup.Controls.Add(Btn("Load", LoadPngs, ThemeGreen));
        sourceGroup.Controls.Add(Btn("Unload", UnloadPngs, ThemeRed));

        var replacementGroup = MakeOptionGroup("Replacements Folder");
        replacementGroup.Controls.Add(MakeLabel("Folder:"));
        replacementGroup.Controls.Add(replacementsBox);
        replacementGroup.Controls.Add(Btn("Browse Folder", BrowseReplacementsFolder, ThemeBlue));
        replacementGroup.Controls.Add(Btn("Open Folder", OpenReplacementsFolder, ThemeGreen));

        var editorGroup = MakeOptionGroup("Editor");
        editorGroup.Controls.Add(Btn("Change Image Editor", ChooseImageEditor, ThemeBlue));

        var layoutGroup = MakeOptionGroup("Layout Tools");
        layoutGroup.Controls.Add(MakeLabel("Padding:"));
        layoutGroup.Controls.Add(paddingBox);
        layoutGroup.Controls.Add(MakeLabel("Snap px:"));
        layoutGroup.Controls.Add(snapBox);
        layoutGroup.Controls.Add(MakeLabel("Edge:"));
        layoutGroup.Controls.Add(edgeBox);
        layoutGroup.Controls.Add(snapCheck);
        layoutGroup.Controls.Add(scaledSplitCheck);
        layoutGroup.Controls.Add(MakeDivider());
        layoutGroup.Controls.Add(Btn("Auto Pack", () => { PushUndo(); AutoPack(); RefreshEverything(); }, ThemeYellow));
        layoutGroup.Controls.Add(Btn("Auto Edges", AutoEdges, ThemeYellow));

        var pieceGroup = MakeOptionGroup("Piece Actions");
        pieceGroup.Controls.Add(Btn("Remove", RemoveSelected, ThemeRed));
        pieceGroup.Controls.Add(Btn("Restore All", RestoreAll, ThemeGreen));
        pieceGroup.Controls.Add(Btn("Lock", () => { PushUndo(); foreach (var p in selected) p.Locked = true; RefreshEverything(); }, ThemeBlue));
        pieceGroup.Controls.Add(Btn("Unlock", () => { PushUndo(); foreach (var p in selected) p.Locked = false; RefreshEverything(); }, ThemeBlue));

        var outputGroup = MakeOptionGroup("Output");
        outputGroup.Controls.Add(Btn("Save Atlas + Layout", SaveCombined, ThemeGreen));
        outputGroup.Controls.Add(Btn("Split To Replacements Folder", SplitCombined, ThemeGreen));

        var historyGroup = MakeOptionGroup("History");
        historyGroup.Controls.Add(Btn("Undo", Undo, ThemeBlue));
        historyGroup.Controls.Add(Btn("Redo", Redo, ThemeBlue));

        top.Controls.Add(sourceGroup);
        top.Controls.Add(replacementGroup);
        top.Controls.Add(editorGroup);
        top.Controls.Add(layoutGroup);
        top.Controls.Add(pieceGroup);
        top.Controls.Add(outputGroup);
        top.Controls.Add(historyGroup);

        var left = new Panel
        {
            Dock = DockStyle.Left,
            Width = 300,
            Padding = new Padding(8),
            BackColor = ThemePanel
        };

        list.BackColor = Color.FromArgb(255, 253, 243);
        list.ForeColor = ThemeInk;
        list.Font = new Font("Segoe UI", 9f);
        list.BorderStyle = BorderStyle.FixedSingle;
        left.Controls.Add(list);

        canvas.BackColor = Color.FromArgb(37, 33, 42);
        canvas.GetType().GetProperty("DoubleBuffered", System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.NonPublic)?.SetValue(canvas, true);

        status.BackColor = ThemeRed;
        status.ForeColor = Color.White;
        status.Font = new Font("Segoe UI Semibold", 9f, FontStyle.Bold);

        Controls.Add(canvas);
        Controls.Add(left);
        Controls.Add(top);
        Controls.Add(status);
    }

    void BuildPieceMenu()
    {
        ToolStripMenuItem Item(string text, Action action)
        {
            var item = new ToolStripMenuItem(text);
            item.Click += (_, _) => action();
            return item;
        }

        var editItem = Item("Edit With Selected Image Editor", EditContextPiece);
        var reloadItem = Item("Reload Selected PNG From Disk", ReloadContextPiece);
        var copyItem = Item("Copy PNG To Replacements Folder", CopyContextPieceToReplacements);
        var editLayoutItem = Item("Edit Selected Pieces As Combined Layout", EditSelectedPiecesAsCombinedLayout);
        var splitLayoutItem = Item("Split Last Selected Layout To Replacements", SplitLastSelectedLayoutToReplacements);
        var removeItem = Item("Remove Selected Pieces", RemoveSelected);
        var lockItem = Item("Lock Selected Pieces", () =>
        {
            PushUndo();
            foreach (var p in ContextSelection())
                p.Locked = true;
            RefreshEverything();
        });
        var unlockItem = Item("Unlock Selected Pieces", () =>
        {
            PushUndo();
            foreach (var p in ContextSelection())
                p.Locked = false;
            RefreshEverything();
        });

        pieceMenu.Items.AddRange(new ToolStripItem[]
        {
            editItem,
            reloadItem,
            copyItem,
            new ToolStripSeparator(),
            editLayoutItem,
            splitLayoutItem,
            new ToolStripSeparator(),
            removeItem,
            lockItem,
            unlockItem
        });

        pieceMenu.Opening += (_, e) =>
        {
            var selection = ContextSelection();
            bool hasSelection = selection.Count > 0;
            bool hasLastLayout = HasLastSelectedLayout();

            editItem.Enabled = hasSelection;
            reloadItem.Enabled = hasSelection;
            copyItem.Enabled = hasSelection;
            editLayoutItem.Enabled = hasSelection;
            removeItem.Enabled = hasSelection;
            lockItem.Enabled = hasSelection;
            unlockItem.Enabled = hasSelection;
            splitLayoutItem.Enabled = hasLastLayout;

            editLayoutItem.Text = selection.Count == 1
                ? "Edit Selected Piece As Combined Layout"
                : $"Edit {selection.Count} Selected Pieces As Combined Layout";

            if (!hasSelection && !hasLastLayout)
                e.Cancel = true;
        };
    }

    void WireEvents()
    {
        canvas.Paint += PaintCanvas;
        canvas.MouseDown += CanvasDown;
        canvas.MouseMove += CanvasMove;
        canvas.MouseUp += CanvasUp;
        canvas.MouseWheel += CanvasWheel;

        list.SelectedIndexChanged += (_, _) =>
        {
            if (updatingList)
                return;

            var active = Active().ToList();
            selected.Clear();

            foreach (int i in list.SelectedIndices)
                if (i >= 0 && i < active.Count)
                    selected.Add(active[i]);

            canvas.Invalidate();
        };

        list.MouseDown += (_, e) =>
        {
            if (e.Button != MouseButtons.Right)
                return;

            int index = list.IndexFromPoint(e.Location);
            var active = Active().ToList();

            if (index >= 0 && index < active.Count)
            {
                contextPiece = active[index];

                if (!selected.Contains(contextPiece))
                {
                    list.SelectedIndex = index;
                    selected.Clear();
                    selected.Add(contextPiece);
                    RefreshList();
                }

                pieceMenu.Show(list, e.Location);
            }
        };

        KeyDown += KeyHandler;

        replacementsBox.Leave += (_, _) =>
        {
            replacementsFolder = replacementsBox.Text.Trim();
            SaveSettings();
        };
    }

    IEnumerable<Piece> Active() => pieces.Where(p => p.Included);


    static Bitmap LoadBitmapUnlocked(string path)
    {
        // Load into memory and release the file handle immediately.
        // Otherwise external editors complain the PNG is already active/open.
        using var fs = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
        using var temp = new Bitmap(fs);
        return new Bitmap(temp);
    }

    void Browse()
    {
        using var dlg = new FolderBrowserDialog();
        if (dlg.ShowDialog() == DialogResult.OK)
        {
            folder = dlg.SelectedPath;
            folderBox.Text = folder;
        }
    }





    void LoadSettings()
    {
        try
        {
            if (!File.Exists(SettingsPath))
                return;

            var settings = JsonSerializer.Deserialize<AppSettings>(File.ReadAllText(SettingsPath));
            if (settings == null)
                return;

            imageEditorPath = settings.ImageEditorPath ?? "";
            replacementsFolder = settings.ReplacementsFolder ?? "";

            if (!string.IsNullOrWhiteSpace(replacementsFolder))
                replacementsBox.Text = replacementsFolder;

            if (!string.IsNullOrWhiteSpace(imageEditorPath) && File.Exists(imageEditorPath))
                SetStatus("Image editor loaded: " + Path.GetFileName(imageEditorPath));
        }
        catch
        {
            // Settings are convenient, not worth crashing over. Astonishing restraint.
        }
    }

    void SaveSettings()
    {
        try
        {
            Directory.CreateDirectory(settingsDir);

            var settings = new AppSettings
            {
                ImageEditorPath = imageEditorPath,
                ReplacementsFolder = replacementsBox.Text.Trim()
            };

            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(settings, new JsonSerializerOptions { WriteIndented = true }));
        }
        catch
        {
            // Do nothing. If settings fail, the world continues its little spin.
        }
    }

    void AskForEditorOnFirstStart()
    {
        if (!string.IsNullOrWhiteSpace(imageEditorPath) && File.Exists(imageEditorPath))
            return;

        var result = MessageBox.Show(
            "Choose your image editor now?\n\nThis will be saved so right-click editing works without asking every time.",
            "Select Image Editor",
            MessageBoxButtons.YesNo,
            MessageBoxIcon.Question);

        if (result == DialogResult.Yes)
            ChooseImageEditor();
    }

    void BrowseReplacementsFolder()
    {
        using var dlg = new FolderBrowserDialog();

        if (!string.IsNullOrWhiteSpace(replacementsFolder) && Directory.Exists(replacementsFolder))
            dlg.SelectedPath = replacementsFolder;
        else if (!string.IsNullOrWhiteSpace(folder) && Directory.Exists(folder))
            dlg.SelectedPath = folder;

        if (dlg.ShowDialog() == DialogResult.OK)
        {
            replacementsFolder = dlg.SelectedPath;
            replacementsBox.Text = replacementsFolder;
            SaveSettings();
            SetStatus("Replacements Folder set: " + replacementsFolder);
        }
    }

    void OpenReplacementsFolder()
    {
        string target = GetOutputFolder(false);

        if (string.IsNullOrWhiteSpace(target) || !Directory.Exists(target))
        {
            MessageBox.Show("Choose a replacements folder first.");
            return;
        }

        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = target,
                UseShellExecute = true
            });
        }
        catch (Exception ex)
        {
            MessageBox.Show("Could not open folder:\n" + ex.Message);
        }
    }

    string GetOutputFolder(bool create)
    {
        replacementsFolder = replacementsBox.Text.Trim();

        if (!string.IsNullOrWhiteSpace(replacementsFolder))
        {
            if (create)
                Directory.CreateDirectory(replacementsFolder);

            if (Directory.Exists(replacementsFolder))
                return replacementsFolder;
        }

        return folder;
    }

    List<Piece> ContextSelection()
    {
        var active = Active().ToHashSet();
        var result = selected
            .Where(active.Contains)
            .OrderBy(p => pieces.IndexOf(p))
            .ToList();

        if (result.Count == 0 && contextPiece != null && active.Contains(contextPiece))
            result.Add(contextPiece);

        return result;
    }

    bool HasLastSelectedLayout()
    {
        return !string.IsNullOrWhiteSpace(lastSelectedLayoutFolder)
            && File.Exists(Path.Combine(lastSelectedLayoutFolder, CombinedName))
            && File.Exists(Path.Combine(lastSelectedLayoutFolder, LayoutName));
    }

    bool EnsureImageEditor()
    {
        if (!string.IsNullOrWhiteSpace(imageEditorPath) && File.Exists(imageEditorPath))
            return true;

        MessageBox.Show("No saved image editor found. Choose one now.");
        ChooseImageEditor();

        return !string.IsNullOrWhiteSpace(imageEditorPath) && File.Exists(imageEditorPath);
    }

    void CopyContextPieceToReplacements()
    {
        var piece = ContextSelection().FirstOrDefault();

        if (piece == null)
        {
            MessageBox.Show("Select a PNG first.");
            return;
        }

        string output = GetOutputFolder(true);

        if (string.IsNullOrWhiteSpace(output) || !Directory.Exists(output))
        {
            MessageBox.Show("Choose a replacements folder first.");
            return;
        }

        try
        {
            // Save from the in-memory image so this works even if the source file is elsewhere.
            string outPath = Path.Combine(output, piece.FileName);
            piece.Image.Save(outPath, ImageFormat.Png);
            SetStatus("Copied selected PNG to replacements: " + outPath);
        }
        catch (Exception ex)
        {
            MessageBox.Show("Could not copy selected PNG:\n" + ex.Message);
        }
    }

    void ChooseImageEditor()
    {
        using var dlg = new OpenFileDialog
        {
            Title = "Choose Image Editor EXE",
            Filter = "Executable Files (*.exe)|*.exe|All Files (*.*)|*.*"
        };

        if (!string.IsNullOrWhiteSpace(imageEditorPath) && File.Exists(imageEditorPath))
            dlg.FileName = imageEditorPath;

        if (dlg.ShowDialog() == DialogResult.OK)
        {
            imageEditorPath = dlg.FileName;
            SaveSettings();
            SetStatus($"Image editor set and saved: {Path.GetFileName(imageEditorPath)}");
        }
    }

    void EditContextPiece()
    {
        var piece = ContextSelection().FirstOrDefault();

        if (piece == null)
        {
            MessageBox.Show("Select a PNG first.");
            return;
        }

        if (!EnsureImageEditor())
            return;

        try
        {
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = imageEditorPath,
                Arguments = $"\"{piece.FullPath}\"",
                UseShellExecute = true
            });

            SetStatus($"Opened {piece.FileName} in {Path.GetFileName(imageEditorPath)}. Save it there, then use Reload Selected PNG From Disk.");
        }
        catch (Exception ex)
        {
            MessageBox.Show("Could not open editor:\n" + ex.Message);
        }
    }

    void EditSelectedPiecesAsCombinedLayout()
    {
        var selection = ContextSelection();
        if (selection.Count == 0)
        {
            MessageBox.Show("Select one or more PNG pieces first.");
            return;
        }

        if (!EnsureImageEditor())
            return;

        try
        {
            var ordered = Active().Where(selection.Contains).ToList();
            var bounds = Union(ordered.Select(p => p.Bounds));
            int atlasWidth = Math.Max(1, bounds.Width);
            int atlasHeight = Math.Max(1, bounds.Height);

            string root = Path.Combine(Path.GetTempPath(), "PaperAtlasTool", "SelectedLayouts");
            string outputFolder = Path.Combine(root, DateTime.Now.ToString("yyyyMMdd_HHmmss_fff"));
            Directory.CreateDirectory(outputFolder);

            string combinedPath = Path.Combine(outputFolder, CombinedName);
            string layoutPath = Path.Combine(outputFolder, LayoutName);

            using (var output = new Bitmap(atlasWidth, atlasHeight, PixelFormat.Format32bppArgb))
            using (var g = Graphics.FromImage(output))
            {
                g.Clear(Color.Transparent);
                g.InterpolationMode = InterpolationMode.NearestNeighbor;

                foreach (var piece in ordered)
                {
                    var rect = piece.Bounds;
                    rect.Offset(-bounds.Left, -bounds.Top);
                    g.DrawImage(piece.Image, rect);
                }

                output.Save(combinedPath, ImageFormat.Png);
            }

            var layout = new LayoutFile
            {
                atlas_width = atlasWidth,
                atlas_height = atlasHeight,
                pieces = ordered.Select((piece, index) =>
                {
                    var rect = piece.Bounds;
                    rect.Offset(-bounds.Left, -bounds.Top);

                    return new LayoutPiece
                    {
                        filename = piece.FileName,
                        x = rect.X,
                        y = rect.Y,
                        width = rect.Width,
                        height = rect.Height,
                        order = index,
                        included = true
                    };
                }).ToList()
            };

            File.WriteAllText(layoutPath, JsonSerializer.Serialize(layout, new JsonSerializerOptions { WriteIndented = true }));
            lastSelectedLayoutFolder = outputFolder;

            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = imageEditorPath,
                Arguments = $"\"{combinedPath}\"",
                UseShellExecute = true
            });

            SetStatus($"Opened selected-piece atlas with {ordered.Count} piece(s). Save it, then right-click and split last selected layout.");
        }
        catch (Exception ex)
        {
            MessageBox.Show("Could not create selected-piece atlas:\n" + ex.Message);
        }
    }

    void SplitLastSelectedLayoutToReplacements()
    {
        if (!HasLastSelectedLayout())
        {
            MessageBox.Show("No selected-piece layout has been created yet.");
            return;
        }

        string outputFolder = GetOutputFolder(true);
        if (string.IsNullOrWhiteSpace(outputFolder) || !Directory.Exists(outputFolder))
        {
            MessageBox.Show("Choose a valid replacements/output folder.");
            return;
        }

        if (TrySplitLayoutFolder(lastSelectedLayoutFolder, outputFolder, false, out int saved, out bool scaled))
        {
            SetStatus($"Split selected-piece layout to replacements: {saved} piece(s).");
            MessageBox.Show($"Split selected layout complete. Saved {saved} pieces to:\n{outputFolder}\nScaled split: {scaled}");
        }
    }

    void ReloadContextPiece()
    {
        var piece = ContextSelection().FirstOrDefault();

        if (piece == null)
        {
            MessageBox.Show("Select a PNG first.");
            return;
        }

        ReloadPieceFromDisk(piece);
    }

    void ReloadPieceFromDisk(Piece piece)
    {
        if (!File.Exists(piece.FullPath))
        {
            MessageBox.Show("Original PNG path no longer exists:\n" + piece.FullPath);
            return;
        }

        try
        {
            PushUndo();

            var newBmp = LoadBitmapUnlocked(piece.FullPath);

            int oldW = piece.Image.Width;
            int oldH = piece.Image.Height;

            piece.Image.Dispose();
            piece.Image = newBmp;

            var r = piece.Bounds;
            r.Width = newBmp.Width;
            r.Height = newBmp.Height;
            piece.Bounds = r;

            string output = GetOutputFolder(false);
            if (!string.IsNullOrWhiteSpace(replacementsBox.Text.Trim()))
            {
                output = GetOutputFolder(true);
                string outPath = Path.Combine(output, piece.FileName);
                piece.Image.Save(outPath, ImageFormat.Png);
                SetStatus($"Reloaded {piece.FileName} and saved replacement copy: {outPath}");
            }
            else
            {
                SetStatus($"Reloaded {piece.FileName} from disk. Size {oldW}x{oldH} -> {newBmp.Width}x{newBmp.Height}.");
            }

            RefreshEverything();
        }
        catch (Exception ex)
        {
            MessageBox.Show("Could not reload PNG:\n" + ex.Message);
        }
    }

    void UnloadPngs()
    {
        foreach (var p in pieces)
            p.Image.Dispose();

        pieces.Clear();
        selected.Clear();
        contextPiece = null;
        lastSelectedLayoutFolder = "";
        undo.Clear();
        redo.Clear();
        dragStart.Clear();

        dragging = false;
        panning = false;
        marquee = false;

        atlasW = 64;
        atlasH = 64;
        zoom = 2f;
        pan = new PointF(60, 60);

        list.Items.Clear();
        canvas.Invalidate();

        SetStatus("Unloaded all PNGs. Ready for a different folder, because commitment is overrated.");
    }

    void LoadPngs()
    {
        folder = folderBox.Text.Trim();
        if (!Directory.Exists(folder))
        {
            MessageBox.Show("Choose a valid folder first.");
            return;
        }

        foreach (var p in pieces)
            p.Image.Dispose();

        pieces.Clear();
        selected.Clear();
        contextPiece = null;
        lastSelectedLayoutFolder = "";
        undo.Clear();
        redo.Clear();
        dragStart.Clear();

        dragging = false;
        panning = false;
        marquee = false;

        foreach (var file in Directory.GetFiles(folder, "*.png").OrderBy(Path.GetFileName))
        {
            if (Path.GetFileName(file).Equals(CombinedName, StringComparison.OrdinalIgnoreCase)) continue;
            try
            {
                var bmp = LoadBitmapUnlocked(file);
                pieces.Add(new Piece
                {
                    FileName = Path.GetFileName(file),
                    FullPath = file,
                    Image = bmp,
                    Bounds = new Rectangle(0, 0, bmp.Width, bmp.Height)
                });
            }
            catch { }
        }

        AutoPack();
        RefreshEverything();
    }

    void RefreshEverything()
    {
        FitAtlas();
        RefreshList();
        canvas.Invalidate();
        SetStatus($"Active: {pieces.Count(p => p.Included)} | Removed: {pieces.Count(p => !p.Included)} | Zoom: {zoom:0.00}x | Atlas: {atlasW}x{atlasH}");
    }

    void RefreshList()
    {
        updatingList = true;
        try
        {
            var active = Active().ToList();
            list.Items.Clear();

            foreach (var p in active)
            {
                var flag = p.Locked ? " [LOCKED]" : "";
                list.Items.Add($"{p.FileName} ({p.Image.Width}x{p.Image.Height}){flag}");
            }

            list.ClearSelected();

            for (int i = 0; i < active.Count; i++)
            {
                if (selected.Contains(active[i]))
                    list.SetSelected(i, true);
            }
        }
        finally
        {
            updatingList = false;
        }
    }

    void AutoPack()
    {
        int pad = (int)paddingBox.Value;
        int total = Math.Max(1, Active().Sum(p => (p.Image.Width + pad) * (p.Image.Height + pad)));
        int targetW = Math.Max(128, (int)Math.Sqrt(total));

        int x = 0, y = 0, shelf = 0;

        foreach (var p in Active().Where(p => !p.Locked))
        {
            if (x + p.Image.Width > targetW)
            {
                x = 0;
                y += shelf + pad;
                shelf = 0;
            }

            p.Bounds = new Rectangle(x, y, p.Image.Width, p.Image.Height);
            x += p.Image.Width + pad;
            shelf = Math.Max(shelf, p.Image.Height);
        }
    }

    void FitAtlas()
    {
        var active = Active().ToList();
        atlasW = active.Count == 0 ? 64 : Math.Max(64, active.Max(p => p.Bounds.Right));
        atlasH = active.Count == 0 ? 64 : Math.Max(64, active.Max(p => p.Bounds.Bottom));
    }

    RectangleF WorldToScreen(Rectangle r) => new(pan.X + r.X * zoom, pan.Y + r.Y * zoom, r.Width * zoom, r.Height * zoom);
    PointF ScreenToWorld(Point p) => new((p.X - pan.X) / zoom, (p.Y - pan.Y) / zoom);

    void PaintCanvas(object? sender, PaintEventArgs e)
    {
        e.Graphics.Clear(canvas.BackColor);
        e.Graphics.InterpolationMode = InterpolationMode.NearestNeighbor;
        e.Graphics.PixelOffsetMode = PixelOffsetMode.Half;

        var atlas = WorldToScreen(new Rectangle(0, 0, atlasW, atlasH));
        using var bg = new SolidBrush(Color.FromArgb(50, 50, 50));
        e.Graphics.FillRectangle(bg, atlas);
        e.Graphics.DrawRectangle(Pens.DimGray, atlas.X, atlas.Y, atlas.Width, atlas.Height);

        foreach (var p in Active())
        {
            var r = Rectangle.Round(WorldToScreen(p.Bounds));
            e.Graphics.DrawImage(p.Image, r);

            if (selected.Contains(p))
            {
                using var pen = new Pen(p.Locked ? Color.OrangeRed : Color.DeepSkyBlue, 2);
                e.Graphics.DrawRectangle(pen, r);
            }
        }

        if (marquee)
        {
            using var pen = new Pen(Color.DeepSkyBlue, 2) { DashStyle = DashStyle.Dash };
            e.Graphics.DrawRectangle(pen, marqueeRect);
        }
    }

    Piece? Hit(Point pt)
    {
        var w = ScreenToWorld(pt);
        return Active().Reverse().FirstOrDefault(p => p.Bounds.Contains(Point.Round(w)));
    }

    void CanvasDown(object? sender, MouseEventArgs e)
    {
        canvas.Focus();
        lastMouse = e.Location;
        mouseDown = e.Location;

        if (e.Button == MouseButtons.Middle)
        {
            panning = true;
            canvas.Cursor = Cursors.SizeAll;
            return;
        }

        if (e.Button == MouseButtons.Right)
        {
            var rightHit = Hit(e.Location);

            if (rightHit != null)
            {
                contextPiece = rightHit;

                if (!selected.Contains(rightHit))
                {
                    selected.Clear();
                    selected.Add(rightHit);
                    RefreshList();
                    canvas.Invalidate();
                }

                pieceMenu.Show(canvas, e.Location);
                return;
            }

            panning = true;
            canvas.Cursor = Cursors.SizeAll;
            return;
        }

        if (e.Button != MouseButtons.Left) return;

        var hit = Hit(e.Location);

        if (hit == null)
        {
            marquee = true;
            marqueeRect = new Rectangle(e.Location, Size.Empty);
            return;
        }

        if ((ModifierKeys & Keys.Control) == Keys.Control)
        {
            if (selected.Contains(hit)) selected.Remove(hit);
            else selected.Add(hit);
            RefreshList();
            canvas.Invalidate();
            return;
        }

        PushUndo();

        if (!selected.Contains(hit))
        {
            selected.Clear();
            selected.Add(hit);
            RefreshList();
        }

        dragging = true;
        dragStartWorld = ScreenToWorld(e.Location);
        dragStart.Clear();
        foreach (var p in selected.Where(p => !p.Locked)) dragStart[p] = p.Bounds;
    }

    void CanvasMove(object? sender, MouseEventArgs e)
    {
        if (panning)
        {
            pan.X += e.X - lastMouse.X;
            pan.Y += e.Y - lastMouse.Y;
            lastMouse = e.Location;
            canvas.Invalidate();
            return;
        }

        if (dragging)
        {
            var now = ScreenToWorld(e.Location);
            int dx = (int)Math.Round(now.X - dragStartWorld.X);
            int dy = (int)Math.Round(now.Y - dragStartWorld.Y);

            foreach (var kv in dragStart)
            {
                var r = kv.Value;
                r.Offset(dx, dy);
                r.X = Math.Max(0, r.X);
                r.Y = Math.Max(0, r.Y);
                kv.Key.Bounds = r;
            }

            if (snapCheck.Checked)
                SnapGroup();

            // Do NOT rebuild the list while dragging. That causes the canvas to feel frozen
            // and only snap into place on mouse-up. Just repaint the canvas live.
            FitAtlas();
            canvas.Invalidate();
            SetStatus($"Dragging {dragStart.Count} piece(s) | Atlas: {atlasW}x{atlasH}");
            return;
        }

        if (marquee)
        {
            marqueeRect = new Rectangle(Math.Min(mouseDown.X, e.X), Math.Min(mouseDown.Y, e.Y), Math.Abs(mouseDown.X - e.X), Math.Abs(mouseDown.Y - e.Y));
            canvas.Invalidate();
        }
    }


    RectangleF ScreenRectToWorldRect(Rectangle screenRect)
    {
        var p1 = ScreenToWorld(new Point(screenRect.Left, screenRect.Top));
        var p2 = ScreenToWorld(new Point(screenRect.Right, screenRect.Bottom));

        float left = Math.Min(p1.X, p2.X);
        float top = Math.Min(p1.Y, p2.Y);
        float right = Math.Max(p1.X, p2.X);
        float bottom = Math.Max(p1.Y, p2.Y);

        return RectangleF.FromLTRB(left, top, right, bottom);
    }

    void CanvasUp(object? sender, MouseEventArgs e)
    {
        if (panning)
        {
            panning = false;
            canvas.Cursor = Cursors.Default;
            return;
        }

        if (dragging)
        {
            dragging = false;
            RefreshEverything();
            return;
        }

        if (marquee)
        {
            marquee = false;
            selected.Clear();

            var worldMarquee = ScreenRectToWorldRect(marqueeRect);

            foreach (var p in Active())
            {
                var pieceWorld = new RectangleF(p.Bounds.X, p.Bounds.Y, p.Bounds.Width, p.Bounds.Height);
                if (worldMarquee.IntersectsWith(pieceWorld))
                    selected.Add(p);
            }

            RefreshList();
            canvas.Invalidate();
            SetStatus($"Marquee selected {selected.Count} piece(s).");
        }
    }

    void CanvasWheel(object? sender, MouseEventArgs e)
    {
        var before = ScreenToWorld(e.Location);
        zoom *= e.Delta > 0 ? 1.15f : 1f / 1.15f;
        zoom = Math.Clamp(zoom, 0.1f, 32f);
        pan.X = e.X - before.X * zoom;
        pan.Y = e.Y - before.Y * zoom;
        RefreshEverything();
    }

    void SnapGroup()
    {
        int threshold = (int)snapBox.Value;
        if (threshold <= 0 || selected.Count == 0) return;

        var moving = selected.Where(p => !p.Locked).ToList();
        var others = Active().Where(p => !moving.Contains(p)).ToList();
        if (moving.Count == 0 || others.Count == 0) return;

        var group = Union(moving.Select(p => p.Bounds));
        int bestDx = 0, bestDy = 0, bestX = threshold + 1, bestY = threshold + 1;

        foreach (var o in others)
        {
            var b = o.Bounds;
            if (Overlap(group.Top, group.Bottom, b.Top, b.Bottom))
            {
                TrySnap(b.Right - group.Left, ref bestX, ref bestDx, threshold);
                TrySnap(b.Left - group.Right, ref bestX, ref bestDx, threshold);
            }

            if (Overlap(group.Left, group.Right, b.Left, b.Right))
            {
                TrySnap(b.Bottom - group.Top, ref bestY, ref bestDy, threshold);
                TrySnap(b.Top - group.Bottom, ref bestY, ref bestDy, threshold);
            }
        }

        foreach (var p in moving)
        {
            var r = p.Bounds;
            if (bestX <= threshold) r.X += bestDx;
            if (bestY <= threshold) r.Y += bestDy;
            p.Bounds = r;
        }
    }

    static bool Overlap(int a1, int a2, int b1, int b2) => Math.Max(a1, b1) <= Math.Min(a2, b2);

    static void TrySnap(int delta, ref int bestDist, ref int bestDelta, int threshold)
    {
        int d = Math.Abs(delta);
        if (d <= threshold && d < bestDist)
        {
            bestDist = d;
            bestDelta = delta;
        }
    }

    static Rectangle Union(IEnumerable<Rectangle> rects)
    {
        var list = rects.ToList();
        return Rectangle.FromLTRB(list.Min(r => r.Left), list.Min(r => r.Top), list.Max(r => r.Right), list.Max(r => r.Bottom));
    }

    void KeyHandler(object? sender, KeyEventArgs e)
    {
        if (e.Control && e.KeyCode == Keys.Z) { Undo(); return; }
        if (e.Control && e.KeyCode == Keys.Y) { Redo(); return; }
        if (e.KeyCode == Keys.Delete) { RemoveSelected(); return; }
        if (e.KeyCode == Keys.Escape) { selected.Clear(); RefreshList(); canvas.Invalidate(); return; }

        int amount = e.Shift ? 10 : 1;
        int dx = 0, dy = 0;

        if (e.KeyCode == Keys.Left) dx = -amount;
        if (e.KeyCode == Keys.Right) dx = amount;
        if (e.KeyCode == Keys.Up) dy = -amount;
        if (e.KeyCode == Keys.Down) dy = amount;

        if (dx != 0 || dy != 0)
        {
            PushUndo();
            foreach (var p in selected.Where(p => !p.Locked))
            {
                var r = p.Bounds;
                r.Offset(dx, dy);
                r.X = Math.Max(0, r.X);
                r.Y = Math.Max(0, r.Y);
                p.Bounds = r;
            }
            RefreshEverything();
        }
    }

    void RemoveSelected()
    {
        if (selected.Count == 0) return;
        PushUndo();
        foreach (var p in selected) p.Included = false;
        selected.Clear();
        RefreshEverything();
    }

    void RestoreAll()
    {
        PushUndo();
        foreach (var p in pieces) p.Included = true;
        RefreshEverything();
    }

    void AutoEdges()
    {
        PushUndo();
        var active = Active().Where(p => !p.Locked).ToList();
        if (active.Count < 2) return;

        int threshold = (int)edgeBox.Value;
        var candidates = new List<(double score, Piece a, Piece b, string rel)>();

        foreach (var a in active)
        foreach (var b in active)
        {
            if (a == b) continue;
            AddCandidate(candidates, a, b, "right-left", EdgeDiff(a.Image, "right", b.Image, "left"), threshold);
            AddCandidate(candidates, a, b, "bottom-top", EdgeDiff(a.Image, "bottom", b.Image, "top"), threshold);
        }

        candidates = candidates.OrderBy(c => c.score).Take(300).ToList();

        if (candidates.Count == 0)
        {
            MessageBox.Show("No strong edge matches. Raise Edge value.");
            Undo();
            return;
        }

        var placed = new HashSet<Piece> { active[0] };
        active[0].Bounds = new Rectangle(0, 0, active[0].Image.Width, active[0].Image.Height);

        bool changed = true;
        while (changed)
        {
            changed = false;
            foreach (var c in candidates)
            {
                if (placed.Contains(c.a) && !placed.Contains(c.b))
                {
                    if (c.rel == "right-left") c.b.Bounds = new Rectangle(c.a.Bounds.Right, c.a.Bounds.Y, c.b.Image.Width, c.b.Image.Height);
                    if (c.rel == "bottom-top") c.b.Bounds = new Rectangle(c.a.Bounds.X, c.a.Bounds.Bottom, c.b.Image.Width, c.b.Image.Height);
                    placed.Add(c.b);
                    changed = true;
                }
            }
        }

        Normalize();
        RefreshEverything();
        MessageBox.Show($"Auto edge arrange placed {placed.Count} pieces. Clean up anything stupid manually, because algorithms have confidence issues.");
    }

    static void AddCandidate(List<(double score, Piece a, Piece b, string rel)> list, Piece a, Piece b, string rel, double score, int threshold)
    {
        if (score <= threshold) list.Add((score, a, b, rel));
    }

    void Normalize()
    {
        var active = Active().ToList();
        if (active.Count == 0) return;

        int minX = active.Min(p => p.Bounds.X);
        int minY = active.Min(p => p.Bounds.Y);

        foreach (var p in active)
        {
            var r = p.Bounds;
            r.X -= minX;
            r.Y -= minY;
            p.Bounds = r;
        }
    }

    static double EdgeDiff(Bitmap a, string edgeA, Bitmap b, string edgeB)
    {
        var ea = EdgeSamples(a, edgeA, 64);
        var eb = EdgeSamples(b, edgeB, 64);
        double total = 0;
        int useful = 0;

        for (int i = 0; i < ea.Count; i++)
        {
            var ca = ea[i];
            var cb = eb[i];
            if (ca.A == 0 && cb.A == 0) continue;
            total += (Math.Abs(ca.R - cb.R) + Math.Abs(ca.G - cb.G) + Math.Abs(ca.B - cb.B)) / 3.0;
            total += Math.Abs(ca.A - cb.A) * 0.35;
            useful++;
        }

        return useful < 8 ? 999999 : total / useful;
    }

    static List<Color> EdgeSamples(Bitmap bmp, string edge, int count)
    {
        var result = new List<Color>();

        for (int i = 0; i < count; i++)
        {
            float t = count == 1 ? 0 : i / (float)(count - 1);
            int x = 0, y = 0;

            if (edge == "left") { x = 0; y = (int)Math.Round(t * (bmp.Height - 1)); }
            if (edge == "right") { x = bmp.Width - 1; y = (int)Math.Round(t * (bmp.Height - 1)); }
            if (edge == "top") { x = (int)Math.Round(t * (bmp.Width - 1)); y = 0; }
            if (edge == "bottom") { x = (int)Math.Round(t * (bmp.Width - 1)); y = bmp.Height - 1; }

            result.Add(bmp.GetPixel(x, y));
        }

        return result;
    }

    EditorState CaptureState() => new()
    {
        Pieces = pieces.Select(p => new StatePiece { FileName = p.FileName, Bounds = p.Bounds, Included = p.Included, Locked = p.Locked }).ToList(),
        Selected = selected.Select(p => p.FileName).ToList(),
        Zoom = zoom,
        Pan = pan
    };

    void Restore(EditorState s)
    {
        foreach (var sp in s.Pieces)
        {
            var p = pieces.FirstOrDefault(x => x.FileName == sp.FileName);
            if (p == null) continue;
            p.Bounds = sp.Bounds;
            p.Included = sp.Included;
            p.Locked = sp.Locked;
        }

        selected.Clear();
        foreach (var name in s.Selected)
        {
            var p = pieces.FirstOrDefault(x => x.FileName == name);
            if (p != null) selected.Add(p);
        }

        zoom = s.Zoom;
        pan = s.Pan;
        RefreshEverything();
    }

    void PushUndo()
    {
        undo.Push(CaptureState());
        redo.Clear();
    }

    void Undo()
    {
        if (undo.Count == 0) return;
        redo.Push(CaptureState());
        Restore(undo.Pop());
    }

    void Redo()
    {
        if (redo.Count == 0) return;
        undo.Push(CaptureState());
        Restore(redo.Pop());
    }

    void SaveCombined()
    {
        if (!Directory.Exists(folder))
        {
            MessageBox.Show("Choose a folder first.");
            return;
        }

        FitAtlas();

        using var output = new Bitmap(atlasW, atlasH, PixelFormat.Format32bppArgb);
        using (var g = Graphics.FromImage(output))
        {
            g.Clear(Color.Transparent);
            g.InterpolationMode = InterpolationMode.NearestNeighbor;

            foreach (var p in Active())
                g.DrawImage(p.Image, p.Bounds);
        }

        output.Save(Path.Combine(folder, CombinedName), ImageFormat.Png);

        var layout = new LayoutFile
        {
            atlas_width = atlasW,
            atlas_height = atlasH,
            pieces = Active().Select((p, i) => new LayoutPiece
            {
                filename = p.FileName,
                x = p.Bounds.X,
                y = p.Bounds.Y,
                width = p.Bounds.Width,
                height = p.Bounds.Height,
                order = i,
                included = true
            }).ToList(),
            removed_pieces = pieces.Where(p => !p.Included).Select((p, i) => new LayoutPiece
            {
                filename = p.FileName,
                width = p.Bounds.Width,
                height = p.Bounds.Height,
                order = i,
                included = false
            }).ToList()
        };

        File.WriteAllText(Path.Combine(folder, LayoutName), JsonSerializer.Serialize(layout, new JsonSerializerOptions { WriteIndented = true }));
        MessageBox.Show("Saved combined_texture.png and n64_texture_layout.json.");
    }

    void SplitCombined()
    {
        if (!Directory.Exists(folder))
        {
            MessageBox.Show("Choose a folder first.");
            return;
        }

        string outputFolder = GetOutputFolder(true);
        if (string.IsNullOrWhiteSpace(outputFolder) || !Directory.Exists(outputFolder))
        {
            MessageBox.Show("Choose a valid replacements/output folder.");
            return;
        }

        if (TrySplitLayoutFolder(folder, outputFolder, true, out int saved, out bool scaled))
            MessageBox.Show($"Split complete. Saved {saved} pieces to:\n{outputFolder}\nScaled split: {scaled}");
    }

    bool TrySplitLayoutFolder(string sourceFolder, string outputFolder, bool backupOriginals, out int saved, out bool scaled)
    {
        saved = 0;
        scaled = false;

        string combinedPath = Path.Combine(sourceFolder, CombinedName);
        string layoutPath = Path.Combine(sourceFolder, LayoutName);

        if (!File.Exists(combinedPath) || !File.Exists(layoutPath))
        {
            MessageBox.Show("Missing combined_texture.png or n64_texture_layout.json.");
            return false;
        }

        var layout = JsonSerializer.Deserialize<LayoutFile>(File.ReadAllText(layoutPath));
        if (layout == null)
            return false;

        using var combined = new Bitmap(combinedPath);
        double sx = combined.Width / (double)Math.Max(1, layout.atlas_width);
        double sy = combined.Height / (double)Math.Max(1, layout.atlas_height);
        scaled = scaledSplitCheck.Checked && (Math.Abs(sx - 1) > 0.0001 || Math.Abs(sy - 1) > 0.0001);

        string backup = Path.Combine(sourceFolder, "OriginalPiecesBackup");
        if (backupOriginals)
            Directory.CreateDirectory(backup);

        foreach (var p in layout.pieces.Where(p => p.included))
        {
            if (backupOriginals)
            {
                string original = Path.Combine(sourceFolder, p.filename);
                if (File.Exists(original))
                {
                    string b = Path.Combine(backup, p.filename);
                    if (!File.Exists(b)) File.Copy(original, b);
                }
            }

            int x = scaled ? (int)Math.Round(p.x * sx) : p.x;
            int y = scaled ? (int)Math.Round(p.y * sy) : p.y;
            int w = scaled ? Math.Max(1, (int)Math.Round(p.width * sx)) : p.width;
            int h = scaled ? Math.Max(1, (int)Math.Round(p.height * sy)) : p.height;

            var rect = Rectangle.Intersect(new Rectangle(x, y, w, h), new Rectangle(0, 0, combined.Width, combined.Height));
            if (rect.Width <= 0 || rect.Height <= 0) continue;

            using var crop = combined.Clone(rect, PixelFormat.Format32bppArgb);
            crop.Save(Path.Combine(outputFolder, p.filename), ImageFormat.Png);
            saved++;
        }

        return true;
    }

    void SetStatus(string text) => status.Text = text;
}
