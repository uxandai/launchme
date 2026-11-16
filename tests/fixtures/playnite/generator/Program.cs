using System;
using System.Collections.Generic;
using System.IO;
using LiteDB;

// Property names and types as Playnite's SDK models (PlayniteSDK/Models); only what launchme reads.
public enum GameActionType { File = 0, URL = 1, Emulator = 2, Script = 3 }
public class GameAction { public GameActionType Type { get; set; } public string Name { get; set; } public string Path { get; set; }
    public string Arguments { get; set; } public string WorkingDir { get; set; } public bool IsPlayAction { get; set; } public Guid EmulatorId { get; set; } }
public class GameRom { public string Name { get; set; } public string Path { get; set; } }
public class Game {
    public Guid Id { get; set; } public string Name { get; set; } public Guid PluginId { get; set; } public string GameId { get; set; }
    public ulong Playtime { get; set; } public ulong PlayCount { get; set; } public DateTime? LastActivity { get; set; } public DateTime? Added { get; set; }
    public bool Favorite { get; set; } public bool Hidden { get; set; } public bool IsInstalled { get; set; } public string InstallDirectory { get; set; }
    public List<Guid> CategoryIds { get; set; } public List<Guid> TagIds { get; set; } public List<Guid> PlatformIds { get; set; }
    public List<GameAction> GameActions { get; set; } public List<GameRom> Roms { get; set; }
    public string CoverImage { get; set; } public string BackgroundImage { get; set; } public string Icon { get; set; } public Guid CompletionStatusId { get; set; }
}
public class Named { public Guid Id { get; set; } public string Name { get; set; } }
public class Platform { public Guid Id { get; set; } public string Name { get; set; } public string SpecificationId { get; set; } }

public static class Program {
    static BsonMapper Mapper() {
        var mapper = new BsonMapper { SerializeNullValues = false, TrimWhitespace = false, EmptyStringToNull = true, IncludeFields = false, IncludeNonPublic = false };
        mapper.Entity<Game>().Id(a => a.Id, false);
        mapper.Entity<Named>().Id(a => a.Id, false);
        mapper.Entity<Platform>().Id(a => a.Id, false);
        return mapper;
    }
    static void Save<T>(string folder, string name, IEnumerable<T> items) {
        var file = Path.Combine(folder, name + ".db");
        if (File.Exists(file)) File.Delete(file);
        using var db = new LiteDatabase($"Filename={file};Mode=Exclusive;Cache Size=0", Mapper());
        var collection = db.GetCollection<T>();
        foreach (var item in items) collection.Insert(item);
    }
    public static void Main(string[] args) {
        var folder = args.Length > 0 ? args[0] : "library";
        Directory.CreateDirectory(folder);
        var steam = Guid.Parse("CB91DFC9-B977-43BF-8E70-55F46E410FAB");
        var gog = Guid.Parse("AEBE8B7C-6DC3-4A66-AF31-E7375C6B5E9E");
        var epic = Guid.Parse("00000002-DBD1-46C6-B5D0-B1BA559D10E4");
        var itch = Guid.Parse("00000001-EBB2-4EEC-ABCB-7C89937A42BB");
        var rpg = Guid.Parse("11111111-2222-3333-4444-555555555555");
        var coop = Guid.Parse("66666666-7777-8888-9999-aaaaaaaaaaaa");
        var snes = Guid.Parse("bbbbbbbb-cccc-dddd-eeee-ffffffffffff");
        var portal = Guid.Parse("01020304-0506-0708-090a-0b0c0d0e0f10");
        var played = new DateTime(2026, 10, 1, 12, 0, 0, DateTimeKind.Utc);
        var added = new DateTime(2024, 1, 2, 3, 4, 5, DateTimeKind.Utc);
        var games = new List<Game> {
            new Game { Id = portal, Name = "Portal 2", PluginId = steam, GameId = "620", Playtime = 36000, PlayCount = 12, LastActivity = played, Added = added,
                Favorite = true, IsInstalled = true, CategoryIds = new List<Guid> { rpg }, TagIds = new List<Guid> { coop },
                CoverImage = portal + @"\cover.png", BackgroundImage = portal + @"\background.jpg" },
            new Game { Id = Guid.NewGuid(), Name = "The Witcher 3", PluginId = gog, GameId = "1207664643", Playtime = 720000, CategoryIds = new List<Guid> { rpg }, Hidden = true },
            new Game { Id = Guid.NewGuid(), Name = "Fortnite", PluginId = epic, GameId = "Fortnite", Playtime = 60 },
            new Game { Id = Guid.NewGuid(), Name = "Itch thing", PluginId = itch, GameId = "12345" },
            new Game { Id = Guid.NewGuid(), Name = "Own Game", PluginId = Guid.Empty, IsInstalled = true, InstallDirectory = @"D:\Games\Own",
                Playtime = 4200, TagIds = new List<Guid> { coop },
                GameActions = new List<GameAction> { new GameAction { Type = GameActionType.File, Name = "Play", Path = @"{InstallDir}\own.exe", Arguments = "-windowed", WorkingDir = "{InstallDir}", IsPlayAction = true } } },
            new Game { Id = Guid.NewGuid(), Name = "Chrono Trigger", PluginId = Guid.Empty, IsInstalled = true, InstallDirectory = @"E:\ROMs\snes",
                PlatformIds = new List<Guid> { snes }, Roms = new List<GameRom> { new GameRom { Name = "Chrono Trigger", Path = @"{InstallDir}\Chrono Trigger.sfc" } },
                GameActions = new List<GameAction> { new GameAction { Type = GameActionType.Emulator, Name = "Play", IsPlayAction = true, EmulatorId = Guid.NewGuid() } } },
            new Game { Id = Guid.NewGuid(), Name = "Web Game", PluginId = Guid.Empty,
                GameActions = new List<GameAction> { new GameAction { Type = GameActionType.URL, Path = "https://example.com", IsPlayAction = true } } },
        };
        Save(folder, "games", games);
        Save(folder, "categories", new[] { new Named { Id = rpg, Name = "RPG" } });
        Save(folder, "tags", new[] { new Named { Id = coop, Name = "Co-op" } });
        Save(folder, "platforms", new[] { new Platform { Id = snes, Name = "Nintendo SNES", SpecificationId = "nintendo_super_nes" } });
        var files = Path.Combine(folder, "files", portal.ToString());
        Directory.CreateDirectory(files);
        // A 1x1 PNG and a tiny JPEG-like file (launchme stores the bytes; validation happens when shown).
        File.WriteAllBytes(Path.Combine(files, "cover.png"), Convert.FromBase64String("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=="));
        File.WriteAllBytes(Path.Combine(files, "background.jpg"), Convert.FromBase64String("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=="));
        Console.WriteLine("fixture written to " + Path.GetFullPath(folder));
    }
}
