using System.Text.Json;
using System.Text.Json.Serialization;
using FPSDemo.Api;
using MongoDB.Driver;

Console.WriteLine("[CALL] Backend Main");
// builder只属于后端进程，环境变量由启动脚本加载；配置秘密不进入UE工程。
var builder = WebApplication.CreateBuilder(args);
// 本项目业务函数默认包括Debug入口日志；不启用Mongo驱动请求正文或凭据日志。
builder.Logging.AddFilter("FPSDemo.Api", LogLevel.Debug);
// Lambda不捕获外部状态，在启动时一次配置请求体上限，防止无限存档上传。
builder.WebHost.ConfigureKestrel(options => { Console.WriteLine("[CALL] ConfigureKestrel"); options.Limits.MaxRequestBodySize = 256 * 1024; });
// Lambda不捕获外部状态，拒绝未知DTO字段，不能上传UnlockedWeaponIds直接授予武器。
builder.Services.ConfigureHttpJsonOptions(options => { Console.WriteLine("[CALL] ConfigureJson"); options.SerializerOptions.UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow; });
builder.Services.AddSingleton<ProfileService>();
// app持有后端服务生命周期；此进程必须独立于游戏运行。
var app = builder.Build();
// 此中间件捕获app的日志器，随进程存活；只记录错误类型/协议码，不记录异常Message或敏感请求体。
app.Use(async (context, next) =>
{
    app.Logger.LogInformation("[CALL] HTTP {Method} {Path}", context.Request.Method, context.Request.Path);
    context.Response.Headers.CacheControl = "no-store";
    try { await next(context); }
    catch (ApiFailure failure) { context.Response.StatusCode = failure.Status; await context.Response.WriteAsJsonAsync(new { code = failure.Code }); }
    catch (JsonException) { context.Response.StatusCode = 400; await context.Response.WriteAsJsonAsync(new { code = "invalid_json" }); }
    catch (BadHttpRequestException) { context.Response.StatusCode = 400; await context.Response.WriteAsJsonAsync(new { code = "invalid_request" }); }
    catch (MongoException error) { app.Logger.LogError("Database request failed type={Type}", error.GetType().Name); context.Response.StatusCode = 503; await context.Response.WriteAsJsonAsync(new { code = "database_unavailable" }); }
    catch (Exception error) { app.Logger.LogError("Request failed type={Type}", error.GetType().Name); context.Response.StatusCode = 500; await context.Response.WriteAsJsonAsync(new { code = "server_error" }); }
});
app.MapGet("/health", Endpoints.Health);
app.MapPost("/v1/auth/session", Endpoints.Session);
app.MapPost("/v1/me/profile-bindings", Endpoints.Bind);
app.MapGet("/v1/me/profile", Endpoints.Profile);
app.MapPost("/v1/me/profile/sync", Endpoints.Sync);
// 启动失败只输出类型，驱动的原始异常可能包含连接地址，不将其转储到日志。
try
{
    await app.Services.GetRequiredService<ProfileService>().InitializeAsync(CancellationToken.None);
    await app.RunAsync();
}
catch (Exception startupError) // 只用于启动/停机边界；请求失败由上方中间件处理。
{
    app.Logger.LogError("BACKEND_START_FAILED type={Type}; check local URI, database permissions, Atlas IP access and replica set", startupError.GetType().Name);
    Environment.ExitCode = 1;
}

/// <summary>HTTP层只负责输入/认证/响应，解锁规则和事务集中在ProfileService。</summary>
internal static class Endpoints
{
    /// <summary>context为单次HTTP请求，service为DI单例；数据库ping通过才返回ok。</summary>
    public static async Task<IResult> Health(HttpContext context, ProfileService service)
    {
        Console.WriteLine("[CALL] Health endpoint");
        await service.PingAsync(context.RequestAborted);
        // 返回运行中程序集的标识与检查点能力，区分“源码已更新”和“服务仍运行旧DLL”；不包含连接配置或玩家数据。
        return Results.Ok(new { status = "ok", protocolVersion = 2, checkpointVersions = new[] { 1, 2, 3, 4 }, serviceBuild = typeof(Endpoints).Assembly.ManifestModule.ModuleVersionId.ToString("N") });
    }
    /// <summary>安装身份+随机秘密的匿名账号登录；不根据昵称或明文玩家ID授权。</summary>
    public static async Task<IResult> Session(HttpContext context, ProfileService service)
    {
        Console.WriteLine("[CALL] Session endpoint");
        var body = await context.Request.ReadFromJsonAsync<SessionRequest>(context.RequestAborted) ?? throw new ApiFailure(400, "missing_body"); // 请求临时DTO，绝不记录Secret。
        return Results.Ok(await service.SessionAsync(body, context.RequestAborted));
    }
    /// <summary>认证后创建本地档案绑定，账号来源只使用Bearer会话。</summary>
    public static async Task<IResult> Bind(HttpContext context, ProfileService service)
    {
        Console.WriteLine("[CALL] Bind endpoint");
        var caller = await service.AuthenticateAsync(context); // 当前请求的已认证账号。
        var body = await context.Request.ReadFromJsonAsync<BindingRequest>(context.RequestAborted) ?? throw new ApiFailure(400, "missing_body"); // 值DTO不逃逸当前请求。
        return Results.Ok(await service.CreateBindingAsync(caller, body, context.RequestAborted)); // 服务仓储方法不与框架BindAsync约定重名。
    }
    /// <summary>下载当前档案投影，用于重启恢复和409冲突后的重新合并。</summary>
    public static async Task<IResult> Profile(HttpContext context, ProfileService service)
    {
        Console.WriteLine("[CALL] Profile endpoint");
        return Results.Ok(await service.GetProfileAsync(await service.AuthenticateAsync(context), context.RequestAborted));
    }
    /// <summary>同步完整事务返回固定JSON回执，超时重试不重复结算。</summary>
    public static async Task<IResult> Sync(HttpContext context, ProfileService service)
    {
        Console.WriteLine("[CALL] Sync endpoint");
        var caller = await service.AuthenticateAsync(context); // 不允许请求体选择账号。
        var body = await context.Request.ReadFromJsonAsync<SyncRequest>(context.RequestAborted) ?? throw new ApiFailure(400, "missing_body"); // 最多64条，服务层进一步验证。
        return Results.Content(await service.SyncAsync(caller, body, context.RequestAborted), "application/json");
    }
}
