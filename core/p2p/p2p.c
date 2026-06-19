// core/p2p/p2p.c
// P2P network layer implementation. P2P 网络层实现。

#include "p2p.h"
#include "../crypto/crypto_c.h"
#include "kcp/ikcp.h"
#include "../protocol/zrtp.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSE closesocket
#define INVALID_SOCK INVALID_SOCKET
static IUINT32 iclock(void) { return (IUINT32)GetTickCount64(); }
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
#define KCP_CONV 0x11223344u // 固定会话 ID
#define KCP_UPDATE_MS 10     // KCP update 间隔（毫秒）
#define SELECT_TIMEOUT_MS 100 // [Fix-1] recvfrom 超时

// -------------------------------------------------------------------------
// KCP 输出回调
// -------------------------------------------------------------------------
typedef struct {
  socket_t sock;
  struct sockaddr_in peer_addr;
  int peer_addr_valid;
} KcpCtx;

static int kcp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
  (void)kcp;
  KcpCtx *ctx = (KcpCtx *)user;
  if (!ctx->peer_addr_valid) return 0;
  sendto(ctx->sock, buf, len, 0, (struct sockaddr *)&ctx->peer_addr,
         sizeof(ctx->peer_addr));
  return 0;
}

// -------------------------------------------------------------------------
// P2P node structure. P2P 节点结构。
// -------------------------------------------------------------------------
struct P2PNode {
  socket_t sock;        // UDP socket. UDP 套接字。
  volatile int running; // Running flag. 运行标志。

  uint8_t pub[PK_SIZE];  // Own public key. 自己的公钥。
  uint8_t sec[PK_SIZE];  // Own secret key. 自己的私钥。
  uint8_t peer[PK_SIZE]; // Peer's public key. 对方的公钥。
  int peer_ready;        // Whether peer key is set. 是否已设置对方公钥。

  void (*on_message)(const char *ip, uint16_t port, const uint8_t *data,
                     size_t len); // Message callback. 消息回调。

  ikcpcb *kcp;    // KCP 控制块
  KcpCtx kcp_ctx; // KCP 输出回调上下文

  char last_peer_ip[INET_ADDRSTRLEN];
  uint16_t last_peer_port;

  zrtp_session_t *zrtp;               // ZRTP session handle. ZRTP 会话句柄。
  zrtp_sas_callback on_sas_cb;        // SAS ready callback. SAS 就绪回调。
  zrtp_result_callback on_result_cb;  // Verification result callback. 验证结果回调。
  void *zrtp_user_data;               // User data for callbacks. 回调用户数据。
  int zrtp_exchanging;                // 1 if exchange in progress. 正在交换中。

  void *dht; // DHT routing table instance. DHT 路由表实例。

#ifdef _WIN32
  HANDLE th;                  // Receive thread handle. 接收线程句柄。
  HANDLE kcp_th;              // KCP update thread handle. KCP 定时线程句柄。
  CRITICAL_SECTION kcp_mutex; // KCP 互斥锁
#else
  pthread_t th;
  pthread_t kcp_th;
  pthread_mutex_t kcp_mutex;
#endif
};

// -------------------------------------------------------------------------
// 互斥锁封装
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
// [Fix-2] KCP update 定时线程
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
    if (n->kcp) ikcp_update(n->kcp, iclock());
    kcp_unlock(n);
  }
  return 0;
}

// -------------------------------------------------------------------------
// [Fix-1 + Fix-2] 接收线程
// -------------------------------------------------------------------------
#ifdef _WIN32
static DWORD WINAPI recv_loop(LPVOID arg) {
#else
static void *recv_loop(void *arg) {
#endif
  P2PNode *n = (P2PNode *)arg;
  uint8_t udp_buf[4096];

  while (n->running) {
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(n->sock, &rset);
    struct timeval tv = {0, SELECT_TIMEOUT_MS * 1000};
    int sel = select((int)n->sock + 1, &rset, NULL, NULL, &tv);
    if (sel <= 0) continue;

    struct sockaddr_in from;
#ifdef _WIN32
    int fromlen = sizeof(from);
#else
    socklen_t fromlen = sizeof(from);
#endif
    int udp_len = recvfrom(n->sock, (char *)udp_buf, sizeof(udp_buf), 0,
                           (struct sockaddr *)&from, &fromlen);
    if (udp_len <= 0) continue;

    inet_ntop(AF_INET, &from.sin_addr, n->last_peer_ip,
              sizeof(n->last_peer_ip));
    n->last_peer_port = ntohs(from.sin_port);

    printf("[DHT] Received UDP packet from %s:%d, size: %d\n",
           n->last_peer_ip, n->last_peer_port, udp_len);

    kcp_lock(n);
    if (n->kcp) ikcp_input(n->kcp, (char *)udp_buf, udp_len);
    kcp_unlock(n);

    while (1) {
      uint8_t kcp_buf[4096];
      kcp_lock(n);
      int klen = n->kcp ? ikcp_recv(n->kcp, (char *)kcp_buf, sizeof(kcp_buf)) : -1;
      kcp_unlock(n);
      if (klen <= 0) break;

      // 解密并回调上层。Decrypt and deliver to the application layer.
      uint8_t *plain = NULL;
      size_t plen = 0;
      if (crypto_decrypt_private(kcp_buf, (size_t)klen, n->sec, &plain, &plen) == 0) {
        printf("[DHT] Decrypted message, adding node: %s:%d\n",
               n->last_peer_ip, n->last_peer_port);
        // Add node to DHT routing table. 添加节点到 DHT 路由表。
        if (n->dht) {
          dht_add_node(n->dht, n->peer, n->last_peer_ip, n->last_peer_port, 0);
        } else {
          printf("[DHT] Warning: dht is NULL!\n");
        }
        if (n->on_message) {
          n->on_message(n->last_peer_ip, n->last_peer_port, plain, plen);
        }
        free(plain);
      } else {
        printf("[DHT] Decryption failed\n");
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

  if (sodium_init() < 0) return NULL;

  P2PNode *n = calloc(1, sizeof(P2PNode));
  if (!n) return NULL;

#ifdef _WIN32
  InitializeCriticalSection(&n->kcp_mutex);
#else
  pthread_mutex_init(&n->kcp_mutex, NULL);
#endif

  n->sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (n->sock == INVALID_SOCK) {
    free(n);
    return NULL;
  }

  int reuse = 1;
  setsockopt(n->sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);
  if (bind(n->sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
    CLOSE(n->sock);
    free(n);
    return NULL;
  }

  CryptoKeypair *kp = crypto_keypair_generate();
  if (!kp) {
    CLOSE(n->sock);
    free(n);
    return NULL;
  }
  memcpy(n->pub, crypto_keypair_get_public(kp), PK_SIZE);
  memcpy(n->sec, crypto_keypair_get_secret(kp), PK_SIZE);
  crypto_keypair_free(kp);

  n->kcp_ctx.sock = n->sock;
  n->kcp_ctx.peer_addr_valid = 0;

  n->kcp = ikcp_create(KCP_CONV, &n->kcp_ctx);
  if (!n->kcp) {
    CLOSE(n->sock);
    free(n);
    return NULL;
  }

  ikcp_setoutput(n->kcp, kcp_output);
  ikcp_nodelay(n->kcp, 1, 10, 2, 1);
  ikcp_wndsize(n->kcp, 128, 128);

  n->zrtp = NULL;
  n->on_sas_cb = NULL;
  n->on_result_cb = NULL;
  n->zrtp_user_data = NULL;
  n->zrtp_exchanging = 0;

  // Create DHT instance. 创建 DHT 实例。
  n->dht = dht_create(n->pub);
  if (n->dht) {
    printf("[DHT] Created successfully, dht ptr: %p\n", n->dht);
  } else {
    printf("[DHT] Creation failed!\n");
  }

  return n;
}

// -------------------------------------------------------------------------
// Start the node. 启动节点。
// -------------------------------------------------------------------------
int p2p_start(P2PNode *n) {
  if (!n) return -1;
  n->running = 1;

#ifdef _WIN32
  n->th = CreateThread(NULL, 0, recv_loop, n, 0, NULL);
  if (!n->th) return -1;
  n->kcp_th = CreateThread(NULL, 0, kcp_update_loop, n, 0, NULL);
  if (!n->kcp_th) return -1;
#else
  if (pthread_create(&n->th, NULL, recv_loop, n) != 0) return -1;
  if (pthread_create(&n->kcp_th, NULL, kcp_update_loop, n) != 0) return -1;
#endif
  return 0;
}

// Set message callback. 设置消息回调。
void p2p_set_callback(P2PNode *n, void (*cb)(const char *, uint16_t,
                                             const uint8_t *, size_t)) {
  if (n) n->on_message = cb;
}

// Get own public key. 获取自己的公钥。
const uint8_t *p2p_get_public_key(P2PNode *n) { return n->pub; }

// Set peer's public key. 设置对方的公钥。
int p2p_set_peer_key(P2PNode *n, const uint8_t *k) {
  if (!n) return -1;
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
  if (!n || !n->peer_ready) return -1;

  kcp_lock(n);
  n->kcp_ctx.peer_addr.sin_family = AF_INET;
  n->kcp_ctx.peer_addr.sin_port = htons(port);
  inet_pton(AF_INET, ip, &n->kcp_ctx.peer_addr.sin_addr);
  n->kcp_ctx.peer_addr_valid = 1;
  kcp_unlock(n);

  uint8_t *cipher = NULL;
  size_t clen = 0;
  if (crypto_encrypt_public(data, len, n->peer, &cipher, &clen) != 0)
    return -1;

  kcp_lock(n);
  int ret = ikcp_send(n->kcp, (char *)cipher, (int)clen);
  kcp_unlock(n);

  free(cipher);
  return (ret >= 0) ? 0 : -1;
}

// -------------------------------------------------------------------------
// Asynchronous ZRTP key exchange. 异步 ZRTP 密钥交换。
// -------------------------------------------------------------------------
int p2p_zrtp_start_exchange(P2PNode *n,
                            zrtp_sas_callback on_sas,
                            zrtp_result_callback on_result,
                            void *user_data) {
  if (!n || !n->peer_ready) {
    printf("Peer key not set. Cannot perform ZRTP exchange.\n");
    return -1;
  }
  if (n->zrtp_exchanging) {
    printf("ZRTP exchange already in progress.\n");
    return -1;
  }

  if (!n->zrtp) {
    n->zrtp = zrtp_session_new();
    if (!n->zrtp) {
      printf("Failed to create ZRTP session.\n");
      return -1;
    }
  }

  zrtp_session_set_keypair(n->zrtp, n->pub, n->sec);
  zrtp_session_set_peer_public(n->zrtp, n->peer);

  if (zrtp_session_key_exchange(n->zrtp) != ZRTP_SUCCESS) {
    printf("ZRTP key exchange failed.\n");
    return -1;
  }

  n->on_sas_cb = on_sas;
  n->on_result_cb = on_result;
  n->zrtp_user_data = user_data;
  n->zrtp_exchanging = 1;

  const char *sas = zrtp_session_get_sas(n->zrtp);
  if (n->on_sas_cb) {
    n->on_sas_cb(sas, n->zrtp_user_data);
  }

  return 0;
}

void p2p_zrtp_confirm(P2PNode *n, int confirmed) {
  if (!n || !n->zrtp_exchanging) return;

  if (confirmed) {
    zrtp_session_mark_verified(n->zrtp);
    printf("✅ Peer verified. Encryption channel established.\n");
  } else {
    printf("❌ Verification failed. Connection rejected.\n");
  }

  if (n->on_result_cb) {
    n->on_result_cb(confirmed, n->zrtp_user_data);
  }

  n->zrtp_exchanging = 0;
}

// Get DHT instance. 获取 DHT 实例。
void* p2p_get_dht(P2PNode* n) {
    return n ? n->dht : NULL;
}

// -------------------------------------------------------------------------
// Destroy node and free resources. 销毁节点并释放资源。
// -------------------------------------------------------------------------
void p2p_destroy(P2PNode *n) {
  if (!n) return;

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
  if (n->th) pthread_join(n->th, NULL);
  if (n->kcp_th) pthread_join(n->kcp_th, NULL);
  pthread_mutex_destroy(&n->kcp_mutex);
#endif

  kcp_lock(n);
  if (n->kcp) {
    ikcp_release(n->kcp);
    n->kcp = NULL;
  }
  kcp_unlock(n);

  // Free ZRTP session. 释放 ZRTP 会话。
  if (n->zrtp) {
    zrtp_session_free(n->zrtp);
    n->zrtp = NULL;
  }

  // Free DHT instance. 释放 DHT 实例。
  if (n->dht) {
    dht_destroy(n->dht);
    n->dht = NULL;
  }

  if (n->sock != INVALID_SOCK) CLOSE(n->sock);
  free(n);
}