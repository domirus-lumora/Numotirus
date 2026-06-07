// core/p2p/p2p.c
// P2P network layer implementation. P2P 网络层实现。
//
// Changelog:
//   [Fix-1] recvfrom 阻塞问题：用 select() 100ms 超时轮询，确保线程能响应
//   running=0 安全退出。 [Fix-2] KCP 接入：收发流程从裸 UDP 切换为 KCP
//   可靠传输，增加 kcp_update 定时线程。

#include "p2p.h"
#include "../crypto/crypto_c.h"
#include <ikcp.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSE closesocket
#define INVALID_SOCK INVALID_SOCKET
// MSVC / MinGW 没有 gettimeofday，用 GetTickCount64 代替。
static IUINT32 iclock(void) { return (IUINT32)GetTickCount64(); }
// Windows 毫秒级睡眠。
static void isleep_ms(DWORD ms) { Sleep(ms); }
#else
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE close
#define INVALID_SOCK (-1)
// 返回自 epoch 起的毫秒时间戳，供 KCP 使用。
static IUINT32 iclock(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (IUINT32)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}
static void isleep_ms(unsigned int ms) {
  struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}
#endif

#define PK_SIZE 32           // 公钥/私钥大小 / Public/secret key size
#define KCP_CONV 0x11223344u // 固定会话 ID（单会话 P2P 足够）
#define KCP_UPDATE_MS 10     // KCP update 间隔（毫秒）
#define SELECT_TIMEOUT_MS                                                      \
  100 // [Fix-1] recvfrom 超时，让线程每 100ms 检查一次 running 标志

// -------------------------------------------------------------------------
// KCP 输出回调：把 KCP 组装好的 UDP 段发送到对端。
// KCP output callback: send a KCP-assembled segment to the remote peer.
// -------------------------------------------------------------------------
typedef struct {
  socket_t sock;
  struct sockaddr_in peer_addr; // 最近一次设置的对端地址（send 时更新）
  int peer_addr_valid;
} KcpCtx;

static int kcp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
  (void)kcp;
  KcpCtx *ctx = (KcpCtx *)user;
  if (!ctx->peer_addr_valid)
    return 0;
  sendto(ctx->sock, buf, len, 0, (struct sockaddr *)&ctx->peer_addr,
         sizeof(ctx->peer_addr));
  return 0;
}

// -------------------------------------------------------------------------
// P2P node structure. P2P 节点结构。
// -------------------------------------------------------------------------
struct P2PNode {
  socket_t sock;        // UDP socket. UDP 套接字。
  volatile int running; // Running flag（volatile，跨线程可见）. 运行标志。

  uint8_t pub[PK_SIZE];  // Own public key. 自己的公钥。
  uint8_t sec[PK_SIZE];  // Own secret key. 自己的私钥。
  uint8_t peer[PK_SIZE]; // Peer's public key. 对方的公钥。
  int peer_ready;        // Whether peer key is set. 是否已设置对方公钥。

  void (*on_message)(const char *ip, uint16_t port, const uint8_t *data,
                     size_t len); // Message callback. 消息回调。

  // [Fix-2] KCP 相关字段。
  ikcpcb *kcp;    // KCP 控制块（非线程安全，用 kcp_mutex 保护）
  KcpCtx kcp_ctx; // KCP 输出回调上下文

  // 保存最近收到包的来源地址，供回调上报。
  // Last received packet source address, for the message callback.
  char last_peer_ip[INET_ADDRSTRLEN];
  uint16_t last_peer_port;

#ifdef _WIN32
  HANDLE th;                  // Receive thread handle. 接收线程句柄。
  HANDLE kcp_th;              // KCP update thread handle. KCP 定时线程句柄。
  CRITICAL_SECTION kcp_mutex; // KCP 互斥锁（Windows）
#else
  pthread_t th;
  pthread_t kcp_th;
  pthread_mutex_t kcp_mutex; // KCP 互斥锁（POSIX）
#endif
};

// -------------------------------------------------------------------------
// 互斥锁封装，屏蔽平台差异。
// Mutex wrappers to hide platform differences.
// -------------------------------------------------------------------------
static void kcp_lock(P2PNode *n) {
#ifdef _WIN32
  EnterCriticalSection(&n->kcp_mutex);
#else
  pthread_mutex_lock(&n->kcp_mutex);
#endif
}
static void kcp_unlock(P2PNode *n) {
#ifdef _WIN32
  LeaveCriticalSection(&n->kcp_mutex);
#else
  pthread_mutex_unlock(&n->kcp_mutex);
#endif
}

// -------------------------------------------------------------------------
// [Fix-2] KCP update 定时线程：每 KCP_UPDATE_MS 毫秒驱动一次 KCP 状态机。
// KCP update timer thread: tick the KCP state machine every KCP_UPDATE_MS ms.
// -------------------------------------------------------------------------
#ifdef _WIN32
static DWORD WINAPI kcp_update_loop(LPVOID arg) {
#else
static void *kcp_update_loop(void *arg) {
#endif
  P2PNode *n = (P2PNode *)arg;
  while (n->running) {
    isleep_ms(KCP_UPDATE_MS);
    kcp_lock(n);
    if (n->kcp)
      ikcp_update(n->kcp, iclock());
    kcp_unlock(n);
  }
  return 0;
}

// -------------------------------------------------------------------------
// [Fix-1 + Fix-2] 接收线程：
//   - 用 select() 100ms 超时取代裸 recvfrom 阻塞 → 解决无法安全退出的 Bug。
//   - 收到 UDP 包后先喂给 ikcp_input，再从 ikcp_recv 取完整消息解密。
//
// Receive thread:
//   - Uses select() with 100 ms timeout instead of blocking recvfrom → fixes
//     the safe-exit bug.
//   - Feeds received UDP packets into ikcp_input, then reads complete messages
//     via ikcp_recv for decryption.
// -------------------------------------------------------------------------
#ifdef _WIN32
static DWORD WINAPI recv_loop(LPVOID arg) {
#else
static void *recv_loop(void *arg) {
#endif
  P2PNode *n = (P2PNode *)arg;
  uint8_t udp_buf[4096];

  while (n->running) {
    // [Fix-1] 用 select() 设置 100ms 超时，不再永久阻塞在 recvfrom。
    // Use select() with 100 ms timeout so the thread wakes up periodically
    // and can check n->running instead of blocking forever.
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(n->sock, &rset);
    struct timeval tv = {0, SELECT_TIMEOUT_MS * 1000}; // 100 ms
    int sel = select((int)n->sock + 1, &rset, NULL, NULL, &tv);
    if (sel <= 0)
      continue; // 超时或出错，回到循环顶部检查 running。

    struct sockaddr_in from;
#ifdef _WIN32
    int fromlen = sizeof(from);
#else
    socklen_t fromlen = sizeof(from);
#endif
    int udp_len = recvfrom(n->sock, (char *)udp_buf, sizeof(udp_buf), 0,
                           (struct sockaddr *)&from, &fromlen);
    if (udp_len <= 0)
      continue;

    // 记录来源地址，供消息回调使用。
    // Record source address for the message callback.
    inet_ntop(AF_INET, &from.sin_addr, n->last_peer_ip,
              sizeof(n->last_peer_ip));
    n->last_peer_port = ntohs(from.sin_port);

    // [Fix-2] 把原始 UDP 负载喂给 KCP 解封装。
    // Feed the raw UDP payload into KCP for reassembly.
    kcp_lock(n);
    if (n->kcp)
      ikcp_input(n->kcp, (char *)udp_buf, udp_len);
    kcp_unlock(n);

    // 从 KCP 取出完整的应用层消息（可能有多条）。
    // Drain all complete application-layer messages from KCP.
    while (1) {
      uint8_t kcp_buf[4096];
      kcp_lock(n);
      int klen =
          n->kcp ? ikcp_recv(n->kcp, (char *)kcp_buf, sizeof(kcp_buf)) : -1;
      kcp_unlock(n);
      if (klen <= 0)
        break;

      // 解密并回调上层。Decrypt and deliver to the application layer.
      uint8_t *plain = NULL;
      size_t plen = 0;
      if (crypto_decrypt_private(kcp_buf, (size_t)klen, n->sec, &plain,
                                 &plen) == 0) {
        if (n->on_message)
          n->on_message(n->last_peer_ip, n->last_peer_port, plain, plen);
        free(plain);
      }
    }
  }
  return 0;
}

// -------------------------------------------------------------------------
// Create a P2P node. 创建 P2P 节点。
// -------------------------------------------------------------------------
P2PNode *p2p_create(uint16_t port) {
#ifdef _WIN32
  WSADATA wsa;
  static int ws_init = 0;
  if (!ws_init) {
    WSAStartup(MAKEWORD(2, 2), &wsa);
    ws_init = 1;
  }
#endif

  if (sodium_init() < 0)
    return NULL;

  P2PNode *n = calloc(1, sizeof(P2PNode));
  if (!n)
    return NULL;

  // 初始化互斥锁。Initialize mutex.
#ifdef _WIN32
  InitializeCriticalSection(&n->kcp_mutex);
#else
  pthread_mutex_init(&n->kcp_mutex, NULL);
#endif

  // Create UDP socket. 创建 UDP 套接字。
  n->sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (n->sock == INVALID_SOCK) {
    free(n);
    return NULL;
  }

  // Allow port reuse. 允许端口重用。
  int reuse = 1;
  setsockopt(n->sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));

  // Bind to local port. 绑定到本地端口。
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (bind(n->sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    CLOSE(n->sock);
    free(n);
    return NULL;
  }

  // Generate crypto key pair. 生成加密密钥对。
  CryptoKeypair *kp = crypto_keypair_generate();
  if (!kp) {
    CLOSE(n->sock);
    free(n);
    return NULL;
  }
  memcpy(n->pub, crypto_keypair_get_public(kp), PK_SIZE);
  memcpy(n->sec, crypto_keypair_get_secret(kp), PK_SIZE);
  crypto_keypair_free(kp);

  // [Fix-2] 初始化 KCP 控制块。
  // Initialize the KCP control block.
  n->kcp_ctx.sock = n->sock;
  n->kcp_ctx.peer_addr_valid = 0;

  n->kcp = ikcp_create(KCP_CONV, &n->kcp_ctx);
  if (!n->kcp) {
    CLOSE(n->sock);
    free(n);
    return NULL;
  }

  ikcp_setoutput(n->kcp, kcp_output);
  // 普通模式：无拥塞控制，间隔 10ms，快速重传 2，流量控制关闭。
  // Normal mode: no congestion control, 10 ms interval, fast-resend 2,
  // flow-control off.
  ikcp_nodelay(n->kcp, 1, 10, 2, 1);
  ikcp_wndsize(n->kcp, 128, 128);

  return n;
}

// -------------------------------------------------------------------------
// Start the node. 启动节点。
// -------------------------------------------------------------------------
int p2p_start(P2PNode *n) {
  if (!n)
    return -1;
  n->running = 1;

#ifdef _WIN32
  n->th = CreateThread(NULL, 0, recv_loop, n, 0, NULL);
  if (!n->th)
    return -1;
  // [Fix-2] 启动 KCP update 定时线程。Start KCP update timer thread.
  n->kcp_th = CreateThread(NULL, 0, kcp_update_loop, n, 0, NULL);
  if (!n->kcp_th)
    return -1;
#else
  if (pthread_create(&n->th, NULL, recv_loop, n) != 0)
    return -1;
  // [Fix-2] 启动 KCP update 定时线程。Start KCP update timer thread.
  if (pthread_create(&n->kcp_th, NULL, kcp_update_loop, n) != 0)
    return -1;
#endif
  return 0;
}

// Set message callback. 设置消息回调。
void p2p_set_callback(P2PNode *n, void (*cb)(const char *, uint16_t,
                                             const uint8_t *, size_t)) {
  if (n)
    n->on_message = cb;
}

// Get own public key. 获取自己的公钥。
const uint8_t *p2p_get_public_key(P2PNode *n) { return n->pub; }

// Set peer's public key. 设置对方的公钥。
int p2p_set_peer_key(P2PNode *n, const uint8_t *k) {
  if (!n)
    return -1;
  memcpy(n->peer, k, PK_SIZE);
  n->peer_ready = 1;
  return 0;
}

// Check if peer key is set. 检查是否已设置对方公钥。
int p2p_is_peer_ready(P2PNode *n) { return n ? n->peer_ready : 0; }

// -------------------------------------------------------------------------
// Send encrypted message via KCP. 通过 KCP 发送加密消息。
// -------------------------------------------------------------------------
int p2p_send(P2PNode *n, const char *ip, uint16_t port, const uint8_t *data,
             size_t len) {
  if (!n || !n->peer_ready)
    return -1;

  // [Fix-2] 每次发送时更新 KCP 输出回调使用的对端地址。
  // Update the peer address used by the KCP output callback on every send.
  kcp_lock(n);
  n->kcp_ctx.peer_addr.sin_family = AF_INET;
  n->kcp_ctx.peer_addr.sin_port = htons(port);
  inet_pton(AF_INET, ip, &n->kcp_ctx.peer_addr.sin_addr);
  n->kcp_ctx.peer_addr_valid = 1;
  kcp_unlock(n);

  // Encrypt message first. 先加密消息。
  uint8_t *cipher = NULL;
  size_t clen = 0;
  if (crypto_encrypt_public(data, len, n->peer, &cipher, &clen) != 0)
    return -1;

  // [Fix-2] 把加密后的数据交给 KCP，由 KCP 负责分片、重传、有序交付。
  // Hand the encrypted payload to KCP; it handles segmentation, retransmission,
  // and in-order delivery.
  kcp_lock(n);
  int ret = ikcp_send(n->kcp, (char *)cipher, (int)clen);
  kcp_unlock(n);

  free(cipher);
  return (ret >= 0) ? 0 : -1;
}

// -------------------------------------------------------------------------
// Destroy node and free resources. 销毁节点并释放资源。
// -------------------------------------------------------------------------
void p2p_destroy(P2PNode *n) {
  if (!n)
    return;

  // [Fix-1] 设置 running=0，两个线程都会在下一次 select
  // 超时后检测到并自行退出。 Set running=0; both threads will detect it after
  // the next select() timeout and exit cleanly.
  n->running = 0;

#ifdef _WIN32
  if (n->th) {
    WaitForSingleObject(n->th, 2000);
    CloseHandle(n->th);
  }
  if (n->kcp_th) {
    WaitForSingleObject(n->kcp_th, 2000);
    CloseHandle(n->kcp_th);
  }
  DeleteCriticalSection(&n->kcp_mutex);
#else
  if (n->th)
    pthread_join(n->th, NULL);
  if (n->kcp_th)
    pthread_join(n->kcp_th, NULL);
  pthread_mutex_destroy(&n->kcp_mutex);
#endif

  kcp_lock(n); // 加锁后再释放 KCP，防止 update 线程还在用。
  if (n->kcp) {
    ikcp_release(n->kcp);
    n->kcp = NULL;
  }
  kcp_unlock(n);

  if (n->sock != INVALID_SOCK)
    CLOSE(n->sock);
  free(n);
}