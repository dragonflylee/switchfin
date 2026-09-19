#pragma once

// Native HTTP and WebSocket clients. Enforce the nonblocking contract curl expects without
// relying solely on the socket creation flag. No ownership, logging or TLS policy.
#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace ps5_native_socket_mode {
struct Observation {
    int before=-1, beforeError=0, setResult=-1, setError=0, after=-1, afterError=0;
    bool accepted=false;
};
struct System {
    static int flags(int fd) noexcept { return ::fcntl(fd,F_GETFL,0); }
    static int nonblock(int fd) noexcept { int enabled=1;return ::ioctl(fd,FIONBIO,&enabled); }
};
template<class Operations=System>
inline Observation establish(int fd) noexcept {
    const int saved=errno;
    Observation result;
    errno=0;
    const int before=Operations::flags(fd);
    result.beforeError=before<0?errno:0;
    if(before>=0)result.before=(before&O_NONBLOCK)?1:0;
    if(result.before==1) {
        // The required mode is already positively observed. Do not issue a
        // redundant setter (which the native API can deny). -2 means skipped;
        // after stays unknown because no post-set query was performed.
        result.setResult=-2;
        result.accepted=true;
        errno=saved;
        return result;
    }
    errno=0;
    result.setResult=Operations::nonblock(fd);
    result.setError=result.setResult<0?errno:0;
    errno=0;
    const int after=Operations::flags(fd);
    result.afterError=after<0?errno:0;
    if(after>=0)result.after=(after&O_NONBLOCK)?1:0;
    // A successful ioctl is authoritative if flag queries are unsupported.
    // A contradictory readable flag or any failed setter rejects the socket;
    // curl retains ownership and performs its usual close/error processing.
    result.accepted=result.setResult==0 && result.after!=0;
    errno=saved;
    return result;
}
// libcurl's connect timeout is never reached on this platform. A sign-in
// ran 75.000016 s and only then returned CURLE_OPERATION_TIMEDOUT, with
// CURLOPT_TIMEOUT_MS and CURLOPT_CONNECTTIMEOUT_MS both 3000. 75 s is
// net.inet.tcp.keepinit, the stack's own limit on establishing a connection, so
// the wait is below libcurl and nothing libcurl is told can shorten it.
// TCP_KEEPINIT sets that limit per socket, in seconds, and the curl socket
// callback runs after the descriptor exists and before connect -- the only
// moment it can be set.
struct ConnectBound {
    int seconds=0;   // what was asked for; 0 means no request was made
    int result=-1;   // setsockopt result
    int error=0;     // errno when it failed
    bool applied=false;
};
struct ConnectSystem {
    static int keepinit(int fd,int seconds) noexcept {
        return ::setsockopt(fd,IPPROTO_TCP,TCP_KEEPINIT,&seconds,sizeof(seconds));
    }
};
// Never fatal, and never a reason to reject the socket: curl aborts the whole
// transfer on a rejected socket, which is worse than an unbounded connect.
template<class Operations=ConnectSystem>
inline ConnectBound bindConnect(int fd,long timeoutMs) noexcept {
    constexpr long floorSeconds=2,ceilingSeconds=15;
    const int saved=errno;
    ConnectBound bound;
    if(timeoutMs<=0) { errno=saved;return bound; }
    long seconds=(timeoutMs+999)/1000;
    if(seconds<floorSeconds)seconds=floorSeconds;
    if(seconds>ceilingSeconds)seconds=ceilingSeconds;
    bound.seconds=static_cast<int>(seconds);
    errno=0;
    bound.result=Operations::keepinit(fd,bound.seconds);
    bound.error=bound.result<0?errno:0;
    bound.applied=bound.result==0;
    errno=saved;
    return bound;
}
} // namespace ps5_native_socket_mode
