#include "Diagnostics/DemoNetworkSystemSnapshot.h"
#include "Debug/DemoNetworkDiagnostics.h"
#include "Debug/DemoLog.h"

#if PLATFORM_WINDOWS
// IP Helper只查询系统已有的链路/路由/邻居缓存；不初始化Winsock、不加载OSS、不调用ResolveIpNetEntry2。
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <winsock2.h>
// SDK 10.0.22621要求先提供IPv4/IPv6扩展类型：仅winsock2.h不会定义_WS2IPDEF_，
// 此时iphlpapi.h引入的netioapi.h会跳过MIB_*_TABLE2/Get*Table2声明；不能在其后补头文件或伪造保护宏。
#include <ws2ipdef.h>
#include <iphlpapi.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif

void DemoNetworkSystemSnapshot::Capture()
{
    UE_LOG(LogFPSDemo, VeryVerbose, TEXT("[CALL] DemoNetworkSystemSnapshot::Capture"));
    if (!DemoNetworkDiagnostics::IsEnabled()) return;
#if PLATFORM_WINDOWS
    const double Started = FPlatformTime::Seconds(); // 仅测量本次同步系统查询耗时；日志出现较大值可识别诊断自身卡顿。
    MIB_IF_TABLE2* Interfaces = nullptr; // IP Helper分配的只读快照，函数末FreeMibTable释放，不跨帧保留句柄。
    MIB_IPFORWARD_TABLE2* Routes = nullptr; // IPv4/IPv6路由快照，只输出默认路由的接口和指标，不输出IP地址。
    MIB_IPNET_TABLE2* Neighbors = nullptr; // 现有邻居缓存；不会主动解析或刷新网关，缺失不能直接判断断网。
    DEMO_NET_DIAG(TEXT("Windows.Sample.Begin"), nullptr, TEXT("passive=1"));
    const ULONG InterfaceResult = GetIfTable2(&Interfaces); // Windows状态码，0成功；错误也保留，避免把查询失败写成链路Down。
    const ULONG RouteResult = GetIpForwardTable2(AF_UNSPEC, &Routes); // 读取两个地址族，不改变路由优先级。
    const ULONG NeighborResult = GetIpNetTable2(AF_UNSPEC, &Neighbors); // 只读缓存，禁止替换为带解析/探测副作用的API。
    DEMO_NET_DIAG(TEXT("Windows.Tables"), nullptr, FString::Printf(TEXT("ifResult=%lu routeResult=%lu neighborResult=%lu ifCount=%u routeCount=%u neighborCount=%u"),
        InterfaceResult, RouteResult, NeighborResult, Interfaces ? Interfaces->NumEntries : 0, Routes ? Routes->NumEntries : 0, Neighbors ? Neighbors->NumEntries : 0));
    if (InterfaceResult == NO_ERROR && Interfaces)
    {
        const ULONG Count = FMath::Min<ULONG>(Interfaces->NumEntries, 64); // 最多64个接口；异常数量明确截断，避免插件虚拟网卡放大日志。
        for (ULONG Index = 0; Index < Count; ++Index) // 当前系统表的行号，不作为跨快照身份。
        {
            const MIB_IF_ROW2& Row = Interfaces->Table[Index]; // 只借用到本次Capture结束；InterfaceIndex与路由/邻居行关联。
            DEMO_NET_DIAG(TEXT("Windows.Interface"), nullptr, FString::Printf(TEXT("if=%u luid=%llu type=%u hardware=%d admin=%u oper=%u media=%u txMbps=%llu rxMbps=%llu rxBytes=%llu txBytes=%llu rxErrors=%llu txErrors=%llu rxDiscards=%llu txDiscards=%llu"),
                Row.InterfaceIndex, Row.InterfaceLuid.Value, Row.Type, Row.InterfaceAndOperStatusFlags.HardwareInterface,
                static_cast<uint32>(Row.AdminStatus), static_cast<uint32>(Row.OperStatus), static_cast<uint32>(Row.MediaConnectState),
                Row.TransmitLinkSpeed / 1000000, Row.ReceiveLinkSpeed / 1000000, Row.InOctets, Row.OutOctets,
                Row.InErrors, Row.OutErrors, Row.InDiscards, Row.OutDiscards)); // 不记录MAC、SSID、接口别名、主机名和地址。
        }
        if (Interfaces->NumEntries > Count) DEMO_NET_DIAG(TEXT("Windows.Interfaces.Truncated"), nullptr, FString::Printf(TEXT("kept=%u"), Count));
    }
    if (RouteResult == NO_ERROR && Routes)
    {
        uint32 Emitted = 0; // 每次最多64条默认路由，不逐条输出局域网主机路由。
        for (ULONG Index = 0; Index < Routes->NumEntries; ++Index) // 快照内部游标，访问后不保留引用。
        {
            const MIB_IPFORWARD_ROW2& Route = Routes->Table[Index]; // 用接口+地址族+下一跳匹配邻居，地址只用于内存比较。
            if (Route.DestinationPrefix.PrefixLength != 0) continue;
            if (Emitted++ >= 64) { DEMO_NET_DIAG(TEXT("Windows.Routes.Truncated"), nullptr, TEXT("kept=64")); break; }
            int32 NeighborState = -1; // -1表示缓存未找到或读取失败；Stale也不是断网，具体Windows枚举见模块文档。
            if (NeighborResult == NO_ERROR && Neighbors)
            {
                for (ULONG NeighborIndex = 0; NeighborIndex < Neighbors->NumEntries; ++NeighborIndex) // 只比较同一接口的默认网关，不输出其他设备。
                {
                    const MIB_IPNET_ROW2& Neighbor = Neighbors->Table[NeighborIndex]; // 借用系统快照，不主动探测该地址。
                    if (Neighbor.InterfaceIndex != Route.InterfaceIndex || Neighbor.Address.si_family != Route.NextHop.si_family) continue;
                    const bool bSameAddress = Route.NextHop.si_family == AF_INET
                        ? Neighbor.Address.Ipv4.sin_addr.S_un.S_addr == Route.NextHop.Ipv4.sin_addr.S_un.S_addr
                        : FMemory::Memcmp(&Neighbor.Address.Ipv6.sin6_addr, &Route.NextHop.Ipv6.sin6_addr, sizeof(IN6_ADDR)) == 0; // IPv6链路范围由已匹配接口限定。
                    if (bSameAddress) { NeighborState = static_cast<int32>(Neighbor.State); break; }
                }
            }
            DEMO_NET_DIAG(TEXT("Windows.DefaultRoute"), nullptr, FString::Printf(TEXT("if=%u family=%u routeMetric=%u protocol=%u gatewayNeighbor=%d"),
                Route.InterfaceIndex, static_cast<uint32>(Route.NextHop.si_family), Route.Metric, static_cast<uint32>(Route.Protocol), NeighborState)); // routeMetric不含接口Metric，不据此推断最终选路。
        }
        DEMO_NET_DIAG(TEXT("Windows.DefaultRoute.Count"), nullptr, FString::Printf(TEXT("emitted=%u"), FMath::Min(Emitted, 64u))); // 0条路由也必须明确记录。
    }
    if (Neighbors) FreeMibTable(Neighbors);
    if (Routes) FreeMibTable(Routes);
    if (Interfaces) FreeMibTable(Interfaces);
    DEMO_NET_DIAG(TEXT("Windows.Sample.End"), nullptr, FString::Printf(TEXT("durationMs=%.3f"), (FPlatformTime::Seconds() - Started) * 1000.0));
#else
    DEMO_NET_DIAG(TEXT("System.Sample.Unsupported"), nullptr, TEXT("windowsOnly=1"));
#endif
}
