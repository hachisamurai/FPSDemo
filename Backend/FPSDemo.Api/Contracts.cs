namespace FPSDemo.Api;

// JSON DTO均为独立协议值；不接收MongoDB操作符、玩家ID授权或客户端解锁数组。
public sealed class SessionRequest
{
    // 客户端安装身份GUID；认证仍必须验证下面的256位随机秘密。
    public string InstallationId { get; set; } = "";
    // 持久凭据，客户端本机加密保存；后端只保存SHA256，日志禁止打印。
    public string Secret { get; set; } = "";
}
public sealed class BindingRequest
{
    // 本地SaveGame身份，用来隔离删除/恢复存档后的版本序列。
    public string ClientProfileId { get; set; } = "";
    // 本地同步状态的GUID代号；旧备份回退后需要新epoch。
    public string Epoch { get; set; } = "";
}
public sealed class ClearClaim
{
    // 一次完整十关通关GUID，同账号跨设备重复上传只能结算一次。
    public string RunId { get; set; } = "";
    // 固定easy/normal/hard，不接受数值索引或中文作为协议键。
    public string DifficultyId { get; set; } = "";
    // 客户端UTC ISO8601，仅作为离线记录展示，不当作可信服务器时间。
    public string CompletedUtc { get; set; } = "";
}
public sealed class SyncRequest
{
    // 协议2包含显式绑定和不可变上传操作；旧V1全量文件不得直接覆盖数据库。
    public int ProtocolVersion { get; set; } = 2;
    // 客户端在发出前持久化的随机GUID；重试必须使用相同ID和相同请求体。
    public string RequestId { get; set; } = "";
    // 由服务器创建的、属于当前登录玩家的绑定。
    public string BindingId { get; set; } = "";
    // 本次本地内容快照版本；批量分块可相同，旧于高水位则拒绝。
    public int LocalRevision { get; set; }
    // 云端Int64版本以字符串传输，避免JSON整数精度损失。
    public string BaseServerRevision { get; set; } = "0";
    // 单批最多64条离线通关事实；后端按runId唯一索引去重。
    public List<ClearClaim> Clears { get; set; } = [];
    // false表示本请求未修改偏好；不能用旧值覆盖另一设备的新选择。
    public bool HasPreferenceChange { get; set; }
    // 仅已解锁主枪ID或空字符串；不会自动生成客户端装备。
    public string LastSelectedPrimary { get; set; } = "";
    // 最多三个被本机修改的检查点；每槽有独立CAS，跨设备同槽修改不能静默覆盖。
    public List<SlotChange> Slots { get; set; } = [];
}

/// <summary>局内检查点值协议，不接受UE对象路径/字节码/任意sav文件。</summary>
public sealed class CheckpointData
{
    public int Version { get; set; } = 1; // 独立检查点协议版本，未来需显式迁移。
    public string CreatedLocal { get; set; } = ""; // 仅展示的原始本地创建时间，不参与冲突排序。
    public string SavedUtc { get; set; } = ""; // 客户端检查点UTC时间，不当作权威时钟。
    public string RunId { get; set; } = ""; // 同一战役恢复后保持，避免重复通关奖励。
    public string Phase { get; set; } = "Hub"; // Hub/Reward/Intermission/Victory，战斗中恢复入口检查点。
    public string Difficulty { get; set; } = "normal"; // 稳定难度ID，独立于UE枚举序号。
    public int CompletedLevel { get; set; } // 已完成0..10关，与Phase一致。
    public int Coins { get; set; } // 安全区金币0..1亿，通关/死亡保留；字段名兼容V1。
    // 零值省略让历史V1请求的规范化JSON/幂等哈希保持不变；UE缺失字段默认0。
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingDefault)]
    public int SilverCoins { get; set; } // V2击杀银币0..1亿，通关/死亡清空。
    public int Kills { get; set; } // 局内击杀，0..10000000。
    public int Purchases { get; set; } // 购买总次数0..100000；V2等于两种币的购买次数之和。
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingDefault)]
    public int GoldPurchases { get; set; } // V2安全区金币购买次数，独立定价；V1缺失为0，省略以兼容旧请求哈希。
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingDefault)]
    public int SilverPurchases { get; set; } // V2关间银币购买次数，独立定价；V1缺失为0，省略以兼容旧请求哈希。
    // V3永久来源与逐项价格；旧请求缺失为null并省略，保持历史幂等哈希不变。
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public UpgradeProgressData? UpgradeProgress { get; set; }
    // V4同槽金币交易的解锁/装配；旧请求null时省略以保持历史幂等哈希。
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public List<string>? UnlockedAmmoIds { get; set; }
    [System.Text.Json.Serialization.JsonIgnore(Condition = System.Text.Json.Serialization.JsonIgnoreCondition.WhenWritingNull)]
    public string? SelectedAmmoId { get; set; } // 必须属于该快照已解锁集合，不跨槽共享。
    public double MaxHealth { get; set; } = 100; // HP，100..10000000。
    public double Health { get; set; } = 100; // 检查点必须存活，0<HP<=MaxHealth。
    public double DamageBonus { get; set; } // 局内伤害加值，0..10000。
    public double MagazineBonus { get; set; } // 局内弹匣加值，0..10000。
    public double HealAmount { get; set; } = 35; // 治疗HP，35..100000。
    public double DashSpeed { get; set; } = 1300; // 冲刺cm/s，1300..100000。
    public List<CheckpointWeapon> Weapons { get; set; } = []; // 已持有值快照，最多四种无重复。
    public string PrimaryId { get; set; } = ""; // 未装备主枪为空，装备时必须在Weapons内。
    public int ActiveSlot { get; set; } = 2; // 1主枪/2手枪；主枪为空不能选1。
}
/// <summary>V3栏位成长账本；顺序0伤害/1生命/2弹匣，服务端保存值，不重放GAS。</summary>
public sealed class UpgradeProgressData
{
    public List<int> GoldLevels { get; set; } = []; // 必须恰好三项，各0..100000，跨挑战保留。
    public List<int> SilverLevels { get; set; } = []; // 必须恰好三项，本轮价格；结束挑战清零。
    public double PermanentDamage { get; set; } // 永久伤害加值0..10000，不能大于合计伤害加值。
    public double PermanentHealth { get; set; } // 永久额外生命HP，0..9999900，不含基础100。
    public double PermanentMagazine { get; set; } // 永久弹匣加值0..10000，不能大于合计容量加值。
}
public sealed class CheckpointWeapon
{
    public string Id { get; set; } = ""; // 目录稳定ID，必须已解锁。
    public int Ammo { get; set; } // 弹匣发数，恢复仍由武器容量钳制。
    public int Reserve { get; set; } // 备用发数，0..100000。
}
public sealed class SlotChange
{
    public int SlotIndex { get; set; } // UI三个槽的0..2索引。
    public string BaseSlotRevision { get; set; } = "0"; // 上次已读取的该槽版本，防跨设备覆盖。
    public CheckpointData Snapshot { get; set; } = new(); // 已验证的完整检查点；不支持隐式删除。
}
