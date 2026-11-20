#pragma once

namespace DemoNetworkSystemSnapshot
{
    /** 游戏线程按低频/手工请求同步读取系统表；内部API不发送ARP、ICMP、DNS或HTTP，失败仅记录状态码。 */
    void Capture();
}
