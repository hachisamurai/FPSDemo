namespace FPSDemo.Api;

/// <summary>由真实通关事实派生武器权限；GET与同步校验共用，不补造低难度记录或奖励。</summary>
public static class WeaponUnlockRules
{
    /// <summary>difficulties为已接受事实的只读序列；返回稳定顺序的权限数组，未知事实拒绝，不访问账号或数据库。</summary>
    public static string[] Resolve(IEnumerable<string> difficulties)
    {
        Console.WriteLine("[CALL] WeaponUnlockRules.Resolve");
        var facts = new HashSet<string>(difficulties, StringComparer.Ordinal); // 同步值集合，去掉重复事实，不修改调用方记录。
        foreach (var fact in facts) // 只借用当前字符串，不保存身份/请求正文。
            if (fact is not ("easy" or "normal" or "hard" or "hard_pistol" or "hell"))
            { Console.WriteLine("WEAPON_UNLOCK rejected unknown completion fact"); throw new ArgumentException("Unknown completion fact", nameof(difficulties)); }
        var result = new List<string> { "pistol" }; // 新档仍只默认手枪；步枪规则独立。
        var hardOrHigher = facts.Contains("hard") || facts.Contains("hard_pistol") || facts.Contains("hell"); // 手枪困难/地狱均满足散弹和狙击条件。
        if (facts.Contains("easy")) result.Add("rifle");
        if (facts.Contains("normal") || hardOrHigher) result.Add("shotgun");
        if (hardOrHigher) result.Add("sniper");
        return result.ToArray();
    }
}
