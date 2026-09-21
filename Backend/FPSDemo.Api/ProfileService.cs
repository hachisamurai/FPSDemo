using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using MongoDB.Bson;
using MongoDB.Driver;

namespace FPSDemo.Api;

/// <summary>协议失败只包含可公开错误码；不向UE泄漏连接串或数据库异常细节。</summary>
public sealed class ApiFailure : Exception
{
    // HTTP状态与稳定错误码，由统一中间件记录并映射到响应。
    public int Status { get; }
    public string Code { get; }
    public ApiFailure(int status, string code) : base(code)
    {
        Console.WriteLine($"[API] ApiFailure status={status} code={code}");
        Status = status;
        Code = code;
    }
}

/// <summary>每次请求从有效Bearer会话解析出的身份，不接受请求体中的playerId。</summary>
public sealed record Caller(string PlayerId, string InstallationId);

/// <summary>单例MongoClient/仓储；每次同步使用独立session，内部事务不得并发操作。</summary>
public sealed class ProfileService
{
    // 进程共享线程安全连接池；session不会保存在单例字段中。
    private readonly MongoClient client;
    // 项目专用数据库，只操作本文档定义的集合，不执行dropDatabase。
    private readonly IMongoDatabase database;
    // 日志只输出接口、稳定错误码、数量和版本，禁止记录请求体/凭据。
    private readonly ILogger<ProfileService> log;
    // 当前单人离线Demo显式接受离线事实，并标记offline_accepted。
    private readonly bool allowOffline;
    // 规范化JSON用于幂等哈希和响应，字段命名与UE协议一致。
    private static readonly JsonSerializerOptions Json = new(JsonSerializerDefaults.Web);

    /// <summary>配置来自环境变量；缺少URI/专用库名立即拒绝启动。</summary>
    public ProfileService(IConfiguration configuration, ILogger<ProfileService> logger)
    {
        logger.LogInformation("[CALL] ProfileService constructor");
        log = logger;
        var uri = configuration["Mongo:ConnectionString"] ?? throw new InvalidOperationException("Set Mongo__ConnectionString in Backend/.env.local"); // 敏感URI只传官方驱动，不写日志。
        var name = configuration["Mongo:Database"] ?? "fpsdemo_dev"; // 明确命名空间，禁止意外使用管理数据库。
        if (name is "admin" or "local" or "config" || name.Length is < 1 or > 60) throw new InvalidOperationException("Use a dedicated game database");
        var settings = MongoClientSettings.FromConnectionString(uri); // 有界连接时间，服务不可用时客户端保持离线。
        settings.ServerSelectionTimeout = TimeSpan.FromSeconds(15); // Atlas初次DNS/TLS握手预留时间，仍限制不可用等待。
        settings.ConnectTimeout = TimeSpan.FromSeconds(10); // 后端连接池超时，UE请求可先超时并幂等重试。
        client = new MongoClient(settings);
        database = client.GetDatabase(name);
        allowOffline = configuration.GetValue<bool>("Profile:AllowOfflineProgress");
    }

    /// <summary>集合访问只接受源码固定名称，绝不把客户端输入当集合名。</summary>
    private IMongoCollection<BsonDocument> Collection(string name)
    {
        log.LogDebug("[CALL] Collection {Collection}", name);
        return database.GetCollection<BsonDocument>(name);
    }

    /// <summary>创建缺失集合/索引；已有数据不清空。事务所需副本集能力在启动时明确检查。</summary>
    public async Task InitializeAsync(CancellationToken cancellation)
    {
        log.LogInformation("[CALL] InitializeAsync");
        var hello = await database.RunCommandAsync<BsonDocument>(new BsonDocument("hello", 1), cancellationToken: cancellation); // 驱动连接到现有部署，不自动更改副本集。
        if (!hello.Contains("setName") && hello.GetValue("msg", "").AsString != "isdbgrid") throw new InvalidOperationException("MongoDB replica set or Atlas is required for profile transactions");
        foreach (var name in new[] { "players", "player_profiles", "profile_bindings", "campaign_runs", "sync_requests", "auth_sessions" }) // 固定六集合；auth_sessions为不透明令牌的可过期认证状态。
        {
            var schema = new BsonDocument { { "bsonType", "object" }, { "required", new BsonArray { "_id" } }, { "properties", new BsonDocument("_id", new BsonDocument("bsonType", "string")) } }; // 最低类型约束；业务字段另由白名单DTO和服务验证。
            try { await database.CreateCollectionAsync(name, new CreateCollectionOptions<BsonDocument> { Validator = new BsonDocument("$jsonSchema", schema), ValidationAction = DocumentValidationAction.Error }, cancellation); }
            catch (MongoCommandException error) when (error.Code == 48) { log.LogInformation("Collection already exists: {Collection}", name); } // 仅NamespaceExists可忽略，其他错误上抛。
        }
        await UniqueAsync("players", new BsonDocument("installationId", 1), cancellation);
        await UniqueAsync("player_profiles", new BsonDocument("playerId", 1), cancellation);
        await UniqueAsync("profile_bindings", new BsonDocument { { "playerId", 1 }, { "clientProfileId", 1 }, { "epoch", 1 } }, cancellation);
        await UniqueAsync("campaign_runs", new BsonDocument { { "playerId", 1 }, { "runId", 1 } }, cancellation);
        await UniqueAsync("sync_requests", new BsonDocument { { "playerId", 1 }, { "requestId", 1 } }, cancellation);
        await UniqueAsync("auth_sessions", new BsonDocument("tokenHash", 1), cancellation);
        await Collection("auth_sessions").Indexes.CreateOneAsync(new CreateIndexModel<BsonDocument>(new BsonDocument("expiresAt", 1), new CreateIndexOptions { ExpireAfter = TimeSpan.Zero }), cancellationToken: cancellation);
        await Collection("campaign_runs").Indexes.CreateOneAsync(new CreateIndexModel<BsonDocument>(new BsonDocument { { "playerId", 1 }, { "receivedAt", -1 }, { "_id", -1 } }), cancellationToken: cancellation);
        log.LogInformation("DATABASE_READY offlineProgress={Allowed}", allowOffline);
    }

    /// <summary>同名固定唯一索引可重复执行；历史重复数据会阻止启动，不能偷偷删除。</summary>
    private async Task UniqueAsync(string collection, BsonDocument keys, CancellationToken cancellation)
    {
        log.LogInformation("[CALL] UniqueAsync {Collection}", collection);
        await Collection(collection).Indexes.CreateOneAsync(new CreateIndexModel<BsonDocument>(keys, new CreateIndexOptions { Unique = true }), cancellationToken: cancellation);
    }

    /// <summary>SHA256用于随机256位凭据/令牌及规范化请求指纹；不是用户密码散列方案。</summary>
    private static string Hash(string value)
    {
        Console.WriteLine("[CALL] Hash (redacted)");
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(value)));
    }

    /// <summary>只接受标准UUID文本，避免路径/操作符等无关输入进入索引字段。</summary>
    private static string GuidKey(string value)
    {
        Console.WriteLine("[CALL] GuidKey");
        if (!Guid.TryParse(value, out var parsed) || parsed == Guid.Empty) throw new ApiFailure(400, "invalid_id"); // parsed为验证后的值，统一字符串格式。
        return parsed.ToString("D");
    }

    /// <summary>随机安装秘密在首次请求前由UE持久化；丢响应时重发不会创建第二个账号。</summary>
    public async Task<object> SessionAsync(SessionRequest request, CancellationToken cancellation)
    {
        log.LogInformation("[CALL] SessionAsync");
        var installation = GuidKey(request.InstallationId); // 统一身份索引值；不是单独的授权凭据。
        if (request.Secret is null || request.Secret.Length != 64 || !request.Secret.All(Uri.IsHexDigit)) throw new ApiFailure(400, "invalid_secret");
        var digest = Hash(request.Secret); // 只存摘要；不会返回或记录原秘密。
        var now = DateTime.UtcNow; // 服务端可信接收时间。
        var initial = new BsonDocument { { "_id", Guid.NewGuid().ToString("D") }, { "installationId", installation }, { "secretHash", digest }, { "createdAt", now }, { "status", "active" } }; // 首次玩家文档；重试setOnInsert不改既有凭据。
        var player = await Collection("players").FindOneAndUpdateAsync(new BsonDocument("installationId", installation), new BsonDocument("$setOnInsert", initial), new FindOneAndUpdateOptions<BsonDocument> { IsUpsert = true, ReturnDocument = ReturnDocument.After }, cancellation); // 每次认证读取同一账号。
        if (player["status"].AsString != "active" || !CryptographicOperations.FixedTimeEquals(Encoding.ASCII.GetBytes(player["secretHash"].AsString), Encoding.ASCII.GetBytes(digest))) throw new ApiFailure(401, "invalid_credentials");
        var playerId = player["_id"].AsString; // 完全由服务器查得，不接受body指定目标账号。
        var profile = new BsonDocument { { "_id", playerId }, { "playerId", playerId }, { "schemaVersion", 1 }, { "serverRevision", 0L }, { "clearedDifficulties", new BsonArray() }, { "unlockedWeaponIds", new BsonArray { "pistol" } }, { "lastSelectedPrimary", "" }, { "createdAt", now }, { "updatedAt", now } }; // 紧凑云端投影，不嵌入无限通关历史。
        await Collection("player_profiles").UpdateOneAsync(new BsonDocument("_id", playerId), new BsonDocument("$setOnInsert", profile), new UpdateOptions { IsUpsert = true }, cancellation);
        var token = Convert.ToHexString(RandomNumberGenerator.GetBytes(32)); // 两小时不透明Bearer，仅本响应返回；数据库只存摘要。
        await Collection("auth_sessions").InsertOneAsync(new BsonDocument { { "_id", Guid.NewGuid().ToString("D") }, { "playerId", playerId }, { "installationId", installation }, { "tokenHash", Hash(token) }, { "expiresAt", now.AddHours(2) } }, cancellationToken: cancellation);
        return new { playerId, accessToken = token, expiresInSeconds = 7200 };
    }

    /// <summary>每次受保护请求认证；TTL清理有延迟，因此查询本身必须检查expiresAt。</summary>
    public async Task<Caller> AuthenticateAsync(HttpContext context)
    {
        log.LogInformation("[CALL] AuthenticateAsync");
        var header = context.Request.Headers.Authorization.ToString(); // 敏感数据只在认证栈内，不写日志。
        if (!header.StartsWith("Bearer ", StringComparison.Ordinal) || header.Length != 71) throw new ApiFailure(401, "authentication_required");
        var session = await Collection("auth_sessions").Find(new BsonDocument { { "tokenHash", Hash(header[7..]) }, { "expiresAt", new BsonDocument("$gt", DateTime.UtcNow) } }).FirstOrDefaultAsync(context.RequestAborted); // 账号会话不以客户端ID猜测。
        if (session is null) throw new ApiFailure(401, "session_expired");
        var player = await Collection("players").Find(new BsonDocument { { "_id", session["playerId"] }, { "status", "active" } }).FirstOrDefaultAsync(context.RequestAborted); // 禁用账号立即阻止尚未过期令牌。
        if (player is null) throw new ApiFailure(401, "account_disabled");
        return new Caller(session["playerId"].AsString, session["installationId"].AsString);
    }

    /// <summary>按当前认证账号创建幂等绑定；新的本地档案/旧备份恢复需新的epoch。</summary>
    public async Task<object> CreateBindingAsync(Caller caller, BindingRequest request, CancellationToken cancellation)
    {
        log.LogInformation("[CALL] CreateBindingAsync"); // 不使用BindAsync名称，避免触发ASP.NET Minimal API自定义参数绑定约定。
        var profileId = GuidKey(request.ClientProfileId); // 本地SaveGame GUID，无授权能力。
        var epoch = GuidKey(request.Epoch); // 绑定代号，隔离本地回退后的修订序列。
        var filter = new BsonDocument { { "playerId", caller.PlayerId }, { "clientProfileId", profileId }, { "epoch", epoch } }; // 账号始终来自Bearer。
        var document = new BsonDocument { { "_id", Guid.NewGuid().ToString("D") }, { "playerId", caller.PlayerId }, { "clientProfileId", profileId }, { "epoch", epoch }, { "installationId", caller.InstallationId }, { "lastAcceptedLocalRevision", 0 }, { "createdAt", DateTime.UtcNow } }; // 重试只返回已有绑定。
        var binding = await Collection("profile_bindings").FindOneAndUpdateAsync(filter, new BsonDocument("$setOnInsert", document), new FindOneAndUpdateOptions<BsonDocument> { IsUpsert = true, ReturnDocument = ReturnDocument.After }, cancellation); // 独立原子upsert。
        return new { bindingId = binding["_id"].AsString, lastAcceptedLocalRevision = binding["lastAcceptedLocalRevision"].AsInt32 };
    }

    /// <summary>只返回协议字段，过滤内部数据库/认证数据；BSON Int64显式序列化为字符串。</summary>
    private object Snapshot(BsonDocument profile)
    {
        log.LogDebug("[CALL] Snapshot");
        // 三个有界检查点嵌入同一投影，使GET得到同一版本的永久进度和存档槽。
        var slots = JsonSerializer.Deserialize<JsonElement>(profile.GetValue("slots", new BsonArray()).ToJson(new MongoDB.Bson.IO.JsonWriterSettings { OutputMode = MongoDB.Bson.IO.JsonOutputMode.RelaxedExtendedJson })); // 业务JSON内版本为字符串，避免BSON Int64扩展语法。
        var unlocked = WeaponUnlockRules.Resolve(profile["clearedDifficulties"].AsBsonArray.Select(value => value.AsString)); // 同步Lambda无捕获，只投影当前文档事实；旧hard档GET立即派生散弹权限，不写库或伪增修订。
        return new { serverProfileId = profile["_id"].AsString, serverRevision = profile["serverRevision"].AsInt64.ToString(CultureInfo.InvariantCulture), clearedDifficulties = profile["clearedDifficulties"].AsBsonArray.Select(value => value.AsString).ToArray(), unlockedWeaponIds = unlocked, lastSelectedPrimary = profile["lastSelectedPrimary"].AsString, slots }; // 同步LINQ不捕获外部状态，仅转换字段值。
    }

    /// <summary>按认证账号读取当前紧凑投影；不存在时返回明确错误，不读取任意用户文档。</summary>
    public async Task<object> GetProfileAsync(Caller caller, CancellationToken cancellation)
    {
        log.LogInformation("[CALL] GetProfileAsync");
        var profile = await Collection("player_profiles").Find(new BsonDocument("_id", caller.PlayerId)).FirstOrDefaultAsync(cancellation); // 单文档原子快照。
        if (profile is null) throw new ApiFailure(404, "profile_missing");
        return Snapshot(profile);
    }

    /// <summary>白名单验证与规范化；完成时间为客户端事实，接收时间由数据库写入流程生成。</summary>
    private long ValidateSync(SyncRequest request)
    {
        log.LogInformation("[CALL] ValidateSync");
        request.RequestId = GuidKey(request.RequestId);
        request.BindingId = GuidKey(request.BindingId);
        if (request.ProtocolVersion != 2 || request.LocalRevision <= 0 || request.Clears is null || request.Clears.Count > 64
            || !long.TryParse(request.BaseServerRevision, NumberStyles.None, CultureInfo.InvariantCulture, out var revision) || revision < 0) throw new ApiFailure(400, "invalid_sync"); // revision为期望云端版本，不按客户端时间排序。
        var seen = new HashSet<string>(); // 本请求内通关ID去重，重复字段不能增加计数。
        foreach (var claim in request.Clears) // 值DTO，只规范化当前请求，不修改共享游戏状态。
        {
            if (claim is null) throw new ApiFailure(400, "invalid_clear");
            claim.RunId = GuidKey(claim.RunId);
            if (!seen.Add(claim.RunId) || claim.DifficultyId is not ("easy" or "normal" or "hard" or "hard_pistol" or "hell")
                || !DateTimeOffset.TryParse(claim.CompletedUtc, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out var completed)) throw new ApiFailure(400, "invalid_clear"); // completed校验日期，不把客户端日期当服务器证明。
            claim.CompletedUtc = completed.UtcDateTime.ToString("O", CultureInfo.InvariantCulture);
        }
        if (request.LastSelectedPrimary is not ("" or "rifle" or "shotgun" or "sniper")) throw new ApiFailure(400, "invalid_weapon");
        if (!allowOffline && request.Clears.Count > 0) throw new ApiFailure(403, "offline_progress_disabled");
        if (request.Slots is null || request.Slots.Count > 3) throw new ApiFailure(400, "invalid_slots");
        var indices = new HashSet<int>(); // 单请求每槽最多一个快照，不允许后写覆盖前写。
        foreach (var slot in request.Slots) // 请求私有DTO，校验在开启事务前完成。
        {
            if (slot is null || slot.SlotIndex is < 0 or > 2 || !indices.Add(slot.SlotIndex)
                || !long.TryParse(slot.BaseSlotRevision, NumberStyles.None, CultureInfo.InvariantCulture, out var slotRevision) || slotRevision < 0) throw new ApiFailure(400, "invalid_slot"); // 每槽CAS版本独立于全局版本。
            ValidateCheckpoint(slot.Snapshot);
        }
        return revision;
    }

    /// <summary>与UE Validate保持同一边界；这里验证结构而非声称验证离线战斗的真实性。</summary>
    private void ValidateCheckpoint(CheckpointData data)
    {
        log.LogInformation("[CALL] ValidateCheckpoint");
        if (data is null) throw new ApiFailure(400, "invalid_checkpoint");
        data.RunId = GuidKey(data.RunId);
        // 同时接受V1..V5；保留上传原格式，客户端显式迁移，避免规范化哈希改变。
        if (data.Version is not (1 or 2 or 3 or 4 or 5) || !DateTime.TryParse(data.CreatedLocal, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out _)
            || !DateTimeOffset.TryParse(data.SavedUtc, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out _)
            || data.Difficulty is not ("easy" or "normal" or "hard" or "hell") || data.Coins is < 0 or > 100000000 || data.Kills is < 0 or > 10000000 || data.Purchases is < 0 or > 100000
            || data.SilverCoins is < 0 or > 100000000 || data.GoldPurchases is < 0 or > 100000 || data.SilverPurchases is < 0 or > 100000
            || data.Version >= 2 && data.Purchases != data.GoldPurchases + data.SilverPurchases
            || data.MaxHealth is < 100 or > 10000000 || data.Health <= 0 || data.Health > data.MaxHealth || data.DamageBonus is < 0 or > 10000 || data.MagazineBonus is < 0 or > 10000
            || data.HealAmount is < 35 or > 100000 || data.DashSpeed is < 1300 or > 100000 || data.ActiveSlot is not (1 or 2)
            || data.Weapons is null || data.Weapons.Count > 4 || data.PrimaryId is not ("" or "rifle" or "shotgun" or "sniper")) throw new ApiFailure(400, "invalid_checkpoint");
        if (data.Version >= 3) ValidateUpgradeProgress(data); // 新字段必须与既有总属性/次数一致，不能吞字段后上传成功。
        // V5模式和挑战元数据有界；旧DTO默认值不能伪装新模式或新资格。
        if (data.PistolChallenge is < 0 or > 2 || data.BestEndlessLevel < 0 || data.BEndless && (data.Difficulty != "hell" || data.BestEndlessLevel < data.CompletedLevel)
            || data.Version < 5 && (data.BEndless || data.BestEndlessLevel != 0 || data.PistolChallenge != 0 || data.Difficulty == "hell")) throw new ApiFailure(400,"invalid_challenge_state");
        if (!(data.Phase == "Hub" && data.CompletedLevel == 0 || data.Phase is "Reward" or "Intermission" && data.CompletedLevel >= 1 && (data.BEndless ? data.CompletedLevel < int.MaxValue : data.CompletedLevel <= 9) || data.Phase == "Victory" && data.CompletedLevel == 10 && !data.BEndless)) throw new ApiFailure(400, "invalid_checkpoint_phase");
        if (data.Version >= 4)
        {
            var ammo = data.UnlockedAmmoIds; // 与钱包作为同一槽CAS快照保存，不单独合并集合。
            if (ammo is null || ammo.Count is < 1 or > 4 || ammo.Distinct().Count() != ammo.Count
                || ammo.Any(id => id is not ("normal" or "fire" or "frost" or "piercing")) // Lambda仅验证本请求字符串，同步执行，无跨线程捕获。
                || !ammo.Contains("normal") || data.SelectedAmmoId is null || !ammo.Contains(data.SelectedAmmoId)) throw new ApiFailure(400, "invalid_checkpoint_ammo");
        }
        var seen = new HashSet<string>(); // 武器目录去重，不接受任意装备键。
        foreach (var weapon in data.Weapons) // 已持有武器的临时快照，解锁校验在事务内使用最新投影。
            if (weapon is null || weapon.Id is not ("pistol" or "rifle" or "shotgun" or "sniper") || !seen.Add(weapon.Id) || weapon.Ammo is < 0 or > 11000 || weapon.Reserve is < 0 or > 100000) throw new ApiFailure(400, "invalid_checkpoint_weapon");
        if (data.PrimaryId.Length > 0 && !seen.Contains(data.PrimaryId) || data.ActiveSlot == 1 && data.PrimaryId.Length == 0) throw new ApiFailure(400, "invalid_checkpoint_loadout");
    }

    /// <summary>data为本请求已验证基本范围的快照；校验永久来源/逐项次数，不修改输入或产生数据库副作用。</summary>
    private void ValidateUpgradeProgress(CheckpointData data)
    {
        log.LogInformation("[CALL] ValidateUpgradeProgress");
        var progress = data.UpgradeProgress; // 只读本请求账本，旧格式不进入本校验。
        if (progress is null || progress.GoldLevels is null || progress.SilverLevels is null
            || progress.GoldLevels.Count != 3 || progress.SilverLevels.Count != 3) throw new ApiFailure(400, "invalid_upgrade_progress");
        int gold = 0, silver = 0; // 先限制每项再求和，避免恶意大值溢出。
        for (int choice = 0; choice < 3; ++choice) // 固定目录顺序，允许不同项目独立价格。
        {
            if (progress.GoldLevels[choice] is < 0 or > 100000 || progress.SilverLevels[choice] is < 0 or > 100000) throw new ApiFailure(400, "invalid_upgrade_level");
            gold += progress.GoldLevels[choice]; silver += progress.SilverLevels[choice];
        }
        if (gold != data.GoldPurchases || silver != data.SilverPurchases
            || !double.IsFinite(progress.PermanentDamage) || progress.PermanentDamage < 0 || progress.PermanentDamage > data.DamageBonus
            || !double.IsFinite(progress.PermanentHealth) || progress.PermanentHealth < 0 || progress.PermanentHealth > data.MaxHealth - 100
            || !double.IsFinite(progress.PermanentMagazine) || progress.PermanentMagazine < 0 || progress.PermanentMagazine > data.MagazineBonus)
            throw new ApiFailure(400, "inconsistent_upgrade_progress");
    }

    /// <summary>一个事务提交通关/解锁/绑定高水位/回执；传入caller与request为本请求快照。</summary>
    public async Task<string> SyncAsync(Caller caller, SyncRequest request, CancellationToken cancellation)
    {
        log.LogInformation("[CALL] SyncAsync");
        var expected = ValidateSync(request); // 在事务前做确定性输入验证。
        var hash = Hash(JsonSerializer.Serialize(request, Json)); // 规范化请求体哈希；相同ID不同内容永远拒绝。
        using var session = await client.StartSessionAsync(cancellationToken: cancellation); // 本请求独享，所有仓储操作顺序执行。
        // Lambda捕获当前service/caller/request/hash/expected；驱动可能重试整个回调，因此仅包含同session数据库操作，无外部副作用。
        return await session.WithTransactionAsync(async (transaction, token) =>
        {
            log.LogInformation("[CALL] Sync transaction request={RequestId}", request.RequestId);
            var receiptFilter = new BsonDocument { { "playerId", caller.PlayerId }, { "requestId", request.RequestId } }; // 幂等查询必须先于版本检查。
            var receipt = await Collection("sync_requests").Find(transaction, receiptFilter).FirstOrDefaultAsync(token); // 成功回执与数据在同次事务提交。
            if (receipt is not null)
            {
                if (receipt["payloadHash"].AsString != hash) throw new ApiFailure(409, "request_id_reused");
                return receipt["responseJson"].AsString;
            }
            var binding = await Collection("profile_bindings").Find(transaction, new BsonDocument { { "_id", request.BindingId }, { "playerId", caller.PlayerId }, { "installationId", caller.InstallationId } }).FirstOrDefaultAsync(token); // 防止跨账号绑定使用。
            if (binding is null) throw new ApiFailure(403, "binding_invalid");
            if (binding["lastAcceptedLocalRevision"].AsInt32 > request.LocalRevision) throw new ApiFailure(409, "binding_stale");
            var profile = await Collection("player_profiles").Find(transaction, new BsonDocument("_id", caller.PlayerId)).FirstOrDefaultAsync(token); // 当前事务内投影，不借用另一查询时刻。
            if (profile is null) throw new ApiFailure(404, "profile_missing");
            if (profile["serverRevision"].AsInt64 != expected) throw new ApiFailure(409, "revision_conflict");
            var difficulties = profile["clearedDifficulties"].AsBsonArray; // 在事务私有文档中更新，永不接受上传的解锁列表。
            foreach (var claim in request.Clears) // 顺序写入，MongoDB事务session不支持并行操作。
            {
                var runFilter = new BsonDocument { { "playerId", caller.PlayerId }, { "runId", claim.RunId } }; // 跨请求/设备的通关唯一键。
                var existing = await Collection("campaign_runs").Find(transaction, runFilter).FirstOrDefaultAsync(token); // 同一通关旧数据不可被覆盖。
                var claimHash = Hash(JsonSerializer.Serialize(claim, Json)); // 与此次上传其他字段无关的事实指纹。
                if (existing is not null && existing["claimHash"].AsString != claimHash) throw new ApiFailure(409, "run_id_reused");
                if (existing is null) await Collection("campaign_runs").InsertOneAsync(transaction, new BsonDocument { { "_id", Guid.NewGuid().ToString("D") }, { "playerId", caller.PlayerId }, { "runId", claim.RunId }, { "difficultyId", claim.DifficultyId }, { "clientCompletedAt", DateTime.Parse(claim.CompletedUtc, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind) }, { "receivedAt", DateTime.UtcNow }, { "completedLevels", 10 }, { "rulesVersion", "campaign-10-v1" }, { "acceptance", "offline_accepted" }, { "claimHash", claimHash } }, cancellationToken: token);
                // 地狱通关必须建立在已接受的困难手枪凭据上；离线事实仍不是防作弊服务器战斗认证。
                if (claim.DifficultyId == "hell" && !difficulties.Contains("hard_pistol")) throw new ApiFailure(400,"hell_locked");
                if (!difficulties.Contains(claim.DifficultyId)) difficulties.Add(claim.DifficultyId);
            }
            var unlocked = new BsonArray(WeaponUnlockRules.Resolve(difficulties.Select(value => value.AsString))); // 同步Lambda无捕获，按同一规则验证装备并更新投影；困难可以同时解锁散弹/狙击。
            var slots = profile.GetValue("slots", new BsonArray()).AsBsonArray; // 有界三个槽，在当前事务内做版本校验和替换。
            foreach (var change in request.Slots) // 按独立slotRevision拒绝重叠的跨设备修改。
            {
                // 同步谓词捕获本次change，仅Find期间读取，不跨事务逃逸。
                var previous = slots.FirstOrDefault(value => value["slotIndex"].AsInt32 == change.SlotIndex); // 已存在的该槽或null。
                var slotRevision = previous is null ? "0" : previous["serverRevision"].AsString; // 字符串Int64是对客户端的稳定协议。
                if (slotRevision != change.BaseSlotRevision) throw new ApiFailure(409, "slot_conflict");
                // 同一请求先合并通关事实再检查模式，离线解锁与检查点可原子提交。
                if (change.Snapshot.Difficulty == "hell" && !difficulties.Contains("hard_pistol") || change.Snapshot.BEndless && !difficulties.Contains("hell")) throw new ApiFailure(400,"checkpoint_mode_locked");
                foreach (var weapon in change.Snapshot.Weapons) // 上传检查点不能绕过永久武器解锁。
                    if (!unlocked.Contains(weapon.Id)) throw new ApiFailure(400, "checkpoint_weapon_locked");
                var replacement = new BsonDocument { { "slotIndex", change.SlotIndex }, { "serverRevision", checked(long.Parse(slotRevision, CultureInfo.InvariantCulture) + 1).ToString(CultureInfo.InvariantCulture) }, { "snapshot", BsonDocument.Parse(JsonSerializer.Serialize(change.Snapshot, Json)) } }; // DTO白名单序列化，禁止客户端Mongo操作符。
                if (previous is not null) slots.Remove(previous);
                slots.Add(replacement);
            }
            profile["slots"] = slots;
            if (request.HasPreferenceChange)
            {
                if (request.LastSelectedPrimary.Length > 0 && !unlocked.Contains(request.LastSelectedPrimary)) throw new ApiFailure(400, "weapon_locked");
                profile["lastSelectedPrimary"] = request.LastSelectedPrimary;
            }
            profile["unlockedWeaponIds"] = unlocked;
            profile["serverRevision"] = checked(expected + 1);
            profile["updatedAt"] = DateTime.UtcNow;
            var update = await Collection("player_profiles").ReplaceOneAsync(transaction, new BsonDocument { { "_id", caller.PlayerId }, { "serverRevision", expected } }, profile, cancellationToken: token); // CAS与事务共同阻止丢失更新。
            if (update.MatchedCount != 1) throw new ApiFailure(409, "revision_conflict");
            await Collection("profile_bindings").UpdateOneAsync(transaction, new BsonDocument("_id", request.BindingId), new BsonDocument("$max", new BsonDocument("lastAcceptedLocalRevision", request.LocalRevision)), cancellationToken: token);
            var response = JsonSerializer.Serialize(new { requestId = request.RequestId, bindingId = request.BindingId, acceptedLocalRevision = request.LocalRevision, acceptedRunIds = request.Clears.Select(claim => claim.RunId).ToArray(), profile = Snapshot(profile) }, Json); // 同步LINQ只读取当前请求，不捕获异步可变状态。
            var record = receiptFilter.DeepClone().AsBsonDocument; // 成功回执副本，包含足以重放确认的结果，不含凭据。
            record.Add("_id", Guid.NewGuid().ToString("D"));
            record.Add("payloadHash", hash);
            record.Add("responseJson", response);
            record.Add("createdAt", DateTime.UtcNow);
            await Collection("sync_requests").InsertOneAsync(transaction, record, cancellationToken: token);
            return response;
        }, new TransactionOptions(ReadConcern.Snapshot, ReadPreference.Primary, WriteConcern.WMajority), cancellation);
    }

    /// <summary>健康检查实际读取数据库；服务进程存活不等于存档可保存。</summary>
    public async Task PingAsync(CancellationToken cancellation)
    {
        log.LogDebug("[CALL] PingAsync");
        await database.RunCommandAsync<BsonDocument>(new BsonDocument("ping", 1), cancellationToken: cancellation);
    }
}
