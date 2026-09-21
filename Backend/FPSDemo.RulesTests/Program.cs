using FPSDemo.Api;

// 顶层同步测试入口；矩阵覆盖每种单独事实，防止“先普通再困难”的顺序测试掩盖越级通关缺陷。
Console.WriteLine("[CALL] Weapon unlock rules regression");
(string[] Facts, string[] Expected)[] cases =
[
    ([], ["pistol"]),
    (["easy"], ["pistol", "rifle"]),
    (["normal"], ["pistol", "shotgun"]),
    (["hard"], ["pistol", "shotgun", "sniper"]),
    (["hard_pistol"], ["pistol", "shotgun", "sniper"]),
    (["hell"], ["pistol", "shotgun", "sniper"]),
    (["hard", "hard", "normal"], ["pistol", "shotgun", "sniper"]),
    (["easy", "normal", "hard", "hard_pistol", "hell"], ["pistol", "rifle", "shotgun", "sniper"])
]; // 输入/期望均是独立固定值，不使用生产规则推导期望。
foreach (var sample in cases) // 无异步、无捕获，单项失败立即非零退出。
{
    var original = sample.Facts.ToArray(); // 证明派生权限不会改造通关事实。
    var actual = WeaponUnlockRules.Resolve(sample.Facts); // 执行生产服务GET/同步共用规则。
    if (!actual.SequenceEqual(sample.Expected) || !sample.Facts.SequenceEqual(original)) throw new InvalidOperationException("WEAPON_RULES_FAIL");
    Console.WriteLine($"WEAPON_RULES_PASS facts={string.Join(',', sample.Facts)}");
}
bool rejected = false; // 明确验证未知事实不会回退成高级武器。
try { WeaponUnlockRules.Resolve(["unknown"]); }
catch (ArgumentException) { rejected = true; }
if (!rejected) throw new InvalidOperationException("WEAPON_RULES_FAIL unknown fact accepted");
Console.WriteLine("WEAPON_RULES_SUCCESS: 9 checks, no account or database access");
