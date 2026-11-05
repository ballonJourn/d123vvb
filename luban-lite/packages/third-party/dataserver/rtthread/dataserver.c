/*
 * dataServer for RT-Thread (D133 / Luban-Lite).
 *
 * C port of NikosMav/System-Programming-3 "Project2/source/server.cpp" and the
 * server half of "aux.cpp", "fpath.cpp", "q.cpp", "q_node.cpp". Same design and
 * the same wire protocol, so the upstream remoteClient talks to the board:
 *
 *   accept loop --> one comm thread per client
 *                     send block_size, read requested directory,
 *                     scan it (fpath tree), push one entry per file
 *                   --> bounded queue (q) --> worker pool
 *                     a worker never serves a socket another worker is busy
 *                     with (busy[]); it sends path, size, then the blocks.
 *                     The socket is closed after the last file of the request.
 *
 * Wire protocol (unchanged, little-endian ints as on x86 / RISC-V):
 *   S->C  int block_size                      (4 bytes)     <- ack
 *   C->S  requested directory, NUL padded     (512 bytes)   -> ack
 *   per file:
 *   S->C  file path relative to the root, NUL padded (512)  <- ack
 *   S->C  int file size                       (4 bytes)     <- ack
 *   S->C  ceil(size/block) blocks of block_size bytes, last one zero padded,
 *         each followed by an ack from the client
 *   ack = the 4 bytes "}#%\0" sent by the receiver after every complete chunk
 *
 * Differences from upstream (all fixes, protocol unchanged): see git log.
 * Build: Luban-Lite SConscript (ADFS) or "make -C rtthread/host" for tests.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dataserver.h"

#ifdef DATASERVER_RTTHREAD
#include <rtthread.h>
#define ds_log(fmt, ...)  rt_kprintf("[dataServer] " fmt "\n", ##__VA_ARGS__)
#else
#define ds_log(fmt, ...)  fprintf(stderr, "[dataServer] " fmt "\n", ##__VA_ARGS__)
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define PATH_FIELD      512     /* fixed path field of the protocol */
#define MAX_CONNS       16      /* clients with files still queued   */
#define SOCK_TIMEOUT_S  15      /* a stalled client frees its worker */

/* ------------------------------------------------------------ fpath ---- */
/* fpath.cpp: a node of the scanned directory tree (no file data). */
typedef struct fpath {
    char         *name;     /* name of file  */
    char         *path;     /* whole path    */
    int           depth;    /* depth of file in directory */
    struct fpath *parent;   /* parent of file */
    struct fpath *next;     /* next file */
    int           isDir;    /* is directory? */
    struct fpath *first;    /* first file (for directories) */
} fpath;

static char *dup_str(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

static fpath *fpath_new(const char *path, const char *name, int depth,
                        fpath *parent, int isDir)
{
    fpath *f = (fpath *)calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    f->path = dup_str(path);
    f->name = dup_str(name);
    if (!f->path || !f->name) {
        free(f->path);
        free(f->name);
        free(f);
        return NULL;
    }
    f->depth = depth;
    f->parent = parent;
    f->isDir = isDir;
    return f;
}

static void fpath_delete(fpath *f)
{
    if (!f)
        return;
    free(f->path);
    free(f->name);
    free(f);
}

/* free a tree built by get_files_info */
static void free_tree(fpath *node)
{
    fpath *current = node->first;
    while (current != NULL) {
        fpath *next = current->next;
        free_tree(current);
        current = next;
    }
    fpath_delete(node);
}

/* -------------------------------------------------------- q / q_node ---- */
/* q_node.cpp / q.cpp: bounded FIFO of (socket, file) tasks. */
typedef struct q_node {
    struct q_node *next_node;
    int            sock;    /* socket to serve */
    fpath         *file;    /* file (own copy) */
} q_node;

typedef struct q {
    q_node *front_node;
    q_node *back_node;
    int     capacityq;
    int     sizeq;
} q;

static void q_push(q *qu, q_node *n)
{
    n->next_node = NULL;
    if (qu->back_node)
        qu->back_node->next_node = n;
    else
        qu->front_node = n;
    qu->back_node = n;
    qu->sizeq++;
}

/* remove and return the first node whose socket satisfies ok() */
static q_node *q_take_first(q *qu, int (*ok)(int sock))
{
    q_node *prev = NULL, *n = qu->front_node;

    while (n) {
        if (ok(n->sock)) {
            if (prev)
                prev->next_node = n->next_node;
            else
                qu->front_node = n->next_node;
            if (qu->back_node == n)
                qu->back_node = prev;
            qu->sizeq--;
            n->next_node = NULL;
            return n;
        }
        prev = n;
        n = n->next_node;
    }
    return NULL;
}

/* ------------------------------------------------------ shared state ---- */
/* server.cpp globals: shared between threads */
struct conn_info {          /* comm_thread_info: [socket, num_of_files, files_sent] */
    int sock;
    int num_of_files;
    int files_sent;
};

static struct {
    dataserver_config_t cfg;
    char             root[DATASERVER_ROOT_MAX];
    int              running;
    int              server_fd;
    q                file_q;
    int             *busy;          /* busy[worker] = socket being served or -1 */
    pthread_mutex_t  q_lock;        /* guards file_q and busy[] */
    pthread_cond_t   emp;           /* queue got an entry / a socket became free */
    pthread_cond_t   f;             /* queue has room again */
    pthread_mutex_t  vec_lock;      /* guards conns[] */
    struct conn_info conns[MAX_CONNS];
    dataserver_stats_t stats;
    pthread_mutex_t  st_lock;
} S;

static int g_inited;

#define STAT_ADD(field, v) do {                 \
        pthread_mutex_lock(&S.st_lock);         \
        S.stats.field += (v);                   \
        pthread_mutex_unlock(&S.st_lock);       \
    } while (0)

/* ------------------------------------------------------- aux.cpp I/O ---- */

/* respond to waiting to achieve sync */
static int respond(int sock)
{
    static const char buf[4] = "}#%";
    return send(sock, buf, 4, MSG_NOSIGNAL) == 4 ? 0 : -1;
}

/* wait respond to continue: exactly the 4-byte ack */
static int wait_respond(int sock)
{
    char buf[4];
    int got = 0;

    while (got < 4) {
        int n = (int)recv(sock, buf + got, 4 - got, 0);
        if (n <= 0)
            return -1;
        got += n;
    }
    return (buf[0] == '}' && buf[1] == '#' && buf[2] == '%') ? 0 : -1;
}

/* send a whole chunk, then wait for the receiver's single ack */
static int send_data(int sock, const char *buf, int size)
{
    int total = 0;

    while (total < size) {
        int n = (int)send(sock, buf + total, size - total, MSG_NOSIGNAL);
        if (n <= 0)
            return -1;
        total += n;
    }
    return wait_respond(sock);
}

/* read exactly size bytes, then ack (buffer is caller provided) */
static int read_data(int sock, char *buf, int size)
{
    int total = 0;

    while (total < size) {
        int n = (int)recv(sock, buf + total, size - total, 0);
        if (n <= 0)
            return -1;
        total += n;
    }
    return respond(sock);
}

/* send block size info to client */
static int send_info(int sock, int block)
{
    char tmp[sizeof(int)];
    memcpy(tmp, &block, sizeof(int));      /* host order, as upstream */
    return send_data(sock, tmp, sizeof(int));
}

/* get wanted path from client (512-byte NUL padded field) */
static int get_path(int sock, char *out, size_t outsz)
{
    char buffer[PATH_FIELD];
    size_t n;

    if (read_data(sock, buffer, PATH_FIELD) != 0)
        return -1;
    buffer[PATH_FIELD - 1] = '\0';
    n = strlen(buffer);
    if (n >= outsz)
        return -1;
    memcpy(out, buffer, n + 1);
    return 0;
}

/* ---------------------------------------------------- root and paths ---- */

/* map a client request to a server path; -1 if it is not allowed */
static int resolve_request(const char *req, char *out, size_t outsz)
{
    size_t pos = (size_t)snprintf(out, outsz, "%s", S.root);
    const char *p = req;

    if (pos >= outsz)
        return -1;
    while (*p) {
        const char *e;
        size_t len;
        while (*p == '/')
            p++;
        e = p;
        while (*e && *e != '/')
            e++;
        len = (size_t)(e - p);
        if (len == 0)
            break;
        if (memchr(p, '\\', len))
            return -1;
        if (!(len == 1 && p[0] == '.')) {
            if (len == 2 && p[0] == '.' && p[1] == '.')
                return -1;
            if (pos + 1 + len >= outsz)
                return -1;
            out[pos++] = '/';
            memcpy(out + pos, p, len);
            pos += len;
            out[pos] = '\0';
        }
        p = e;
    }
    return 0;
}

/* server path -> path sent to the client (relative to the root) */
static const char *client_path(const char *path)
{
    size_t rl = strlen(S.root);
    if (strncmp(path, S.root, rl) == 0 && path[rl] == '/')
        return path + rl + 1;
    return path;
}

/* ------------------------------------------------- directory scanning ---- */

/* get files info from given path RECURSIVELY */
static int get_files_info(const char *path, fpath *parent, int depth)
{
    DIR *dir;
    struct dirent *ent;
    fpath *prev = NULL, *current;
    char *sub;
    size_t plen = strlen(path);

    depth++;
    if (depth > DATASERVER_MAX_DEPTH)
        return -1;
    dir = opendir(path);
    if (dir == NULL)
        return -1;
    sub = (char *)malloc(plen + 2 + 256);
    if (!sub) {
        closedir(dir);
        return -1;
    }
    while ((ent = readdir(dir)) != NULL) {
        struct stat st;
        /* ignoring "." and "..", they are not really files */
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (strlen(ent->d_name) > 255)
            continue;
        snprintf(sub, plen + 2 + 256, "%s/%s", path, ent->d_name);
        if (stat(sub, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            /* a folder: get the files info in it recursively */
            current = fpath_new(sub, ent->d_name, depth, parent, 1);
            if (!current)
                break;
            get_files_info(sub, current, depth);
        } else {
            current = fpath_new(sub, ent->d_name, depth, parent, 0);
            if (!current)
                break;
        }
        /* first child, or chain it after the previous one */
        if (parent->first == NULL)
            parent->first = current;
        else
            prev->next = current;
        prev = current;
    }
    free(sub);
    closedir(dir);
    return 1;
}

/* getting number of files in wanted directory */
static void num_of_files(int *counter, fpath *file)
{
    fpath *current = file->first;
    while (current != NULL) {
        if (current->isDir)
            num_of_files(counter, current);
        else
            (*counter)++;
        current = current->next;
    }
}

/* ---------------------------------------------------------- queueing ---- */

static int sock_not_busy(int sock)
{
    int i;
    for (i = 0; i < S.cfg.thread_pool_size; i++)
        if (S.busy[i] == sock)
            return 0;
    return 1;
}

/* push entry/file to shared queue of threads (blocks while the queue is full) */
static int push_entry(int sock, fpath *file)
{
    /* the entry gets its own copy so the tree can be freed right after */
    q_node *n = (q_node *)calloc(1, sizeof(*n));
    if (!n)
        return -1;
    n->sock = sock;
    n->file = fpath_new(file->path, file->name, file->depth, NULL, 0);
    if (!n->file) {
        free(n);
        return -1;
    }
    pthread_mutex_lock(&S.q_lock);
    while (S.file_q.sizeq >= S.file_q.capacityq && S.running)
        pthread_cond_wait(&S.f, &S.q_lock);
    q_push(&S.file_q, n);
    pthread_cond_broadcast(&S.emp);
    pthread_mutex_unlock(&S.q_lock);
    return 0;
}

/* push all entries/files to shared queue of threads; returns files pushed */
static int push_entries(int sock, fpath *file)
{
    int pushed = 0;
    fpath *current = file->first;
    while (current != NULL) {
        if (current->isDir)
            pushed += push_entries(sock, current);
        else if (push_entry(sock, current) == 0)
            pushed++;
        current = current->next;
    }
    return pushed;
}

/* ------------------------------------------------------- sending files ---- */

/* send file to client: path, size, then ceil(size/block) padded blocks */
static void send_file(int sock, fpath *file, int block)
{
    char info[PATH_FIELD];
    char temp[sizeof(int)];
    struct stat st;
    int fd, size = 0, send_requests, i;
    char *buffer;

    fd = open(file->path, O_RDONLY);
    if (fd >= 0 && fstat(fd, &st) == 0)
        size = (int)st.st_size;
    else if (fd < 0)
        ds_log("couldn't open %s", file->path);

    /* sending file path to client */
    memset(info, 0, sizeof(info));
    strncpy(info, client_path(file->path), sizeof(info) - 1);
    if (send_data(sock, info, PATH_FIELD) != 0)
        goto out;

    /* send file size to client */
    memcpy(temp, &size, sizeof(int));
    if (send_data(sock, temp, sizeof(int)) != 0)
        goto out;

    /* sending send_requests blocks to client */
    send_requests = (size + block - 1) / block;
    buffer = (char *)malloc((size_t)block);
    if (!buffer)
        goto out;
    for (i = 0; i < send_requests; i++) {
        int got = 0;
        memset(buffer, 0, (size_t)block);
        while (got < block) {
            int n = (int)read(fd, buffer + got, (size_t)(block - got));
            if (n <= 0)
                break;
            got += n;
        }
        if (send_data(sock, buffer, block) != 0)
            break;
        STAT_ADD(bytes_sent, (uint64_t)(got));
    }
    free(buffer);
    if (i == send_requests)
        STAT_ADD(files_sent, 1);
out:
    if (fd >= 0)
        close(fd);
}

/* one file of a request is done: close the socket after the last one */
static void file_done(int sock)
{
    int i;

    pthread_mutex_lock(&S.vec_lock);
    for (i = 0; i < MAX_CONNS; i++) {
        struct conn_info *c = &S.conns[i];
        if (c->sock == sock && c->num_of_files > 0) {
            c->files_sent++;
            if (c->files_sent == c->num_of_files) {
                close(sock);        /* whole directory sent */
                c->sock = -1;
                c->num_of_files = c->files_sent = 0;
                STAT_ADD(requests_done, 1);
            }
            break;
        }
    }
    pthread_mutex_unlock(&S.vec_lock);
}

/* ------------------------------------------------------------ threads ---- */

/* worker: take a task whose socket is not busy, send that file */
static void *thread_func_worker(void *arg)
{
    int index = (int)(intptr_t)arg;

    for (;;) {
        q_node *entry;
        int temp_sock;

        pthread_mutex_lock(&S.q_lock);
        /* upstream spun while every queued socket was busy; wait instead */
        while ((entry = q_take_first(&S.file_q, sock_not_busy)) == NULL)
            pthread_cond_wait(&S.emp, &S.q_lock);
        temp_sock = entry->sock;
        S.busy[index] = temp_sock;      /* this thread is busy with temp_sock */
        pthread_cond_broadcast(&S.f);   /* popped from the queue: room for pushes */
        pthread_mutex_unlock(&S.q_lock);

        /* here is all the work done (sending file info, the file itself etc) */
        send_file(temp_sock, entry->file, S.cfg.block_size);
        fpath_delete(entry->file);
        free(entry);

        pthread_mutex_lock(&S.q_lock);
        S.busy[index] = -1;             /* done with that socket */
        pthread_cond_broadcast(&S.emp); /* its other entries can be served now */
        pthread_mutex_unlock(&S.q_lock);

        file_done(temp_sock);
    }
    return NULL;
}

/* comm thread: one per client */
static void *thread_func_comm(void *arg)
{
    int current_socket = (int)(intptr_t)arg;
    char req[PATH_FIELD], full[DATASERVER_ROOT_MAX + PATH_FIELD];
    fpath *root = NULL;
    int counter = 0, slot = -1, pushed, i;
    struct timeval tv;

    tv.tv_sec = SOCK_TIMEOUT_S;
    tv.tv_usec = 0;
    setsockopt(current_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(current_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* send block_size to client so he knows how many bytes to read */
    if (send_info(current_socket, S.cfg.block_size) != 0)
        goto close_out;
    /* get path from client */
    if (get_path(current_socket, req, sizeof(req)) != 0)
        goto close_out;
    if (resolve_request(req, full, sizeof(full)) != 0) {
        ds_log("rejected request outside the root: %s", req);
        STAT_ADD(rejected, 1);
        goto close_out;
    }
    ds_log("about to scan directory %s", full);

    /* create a structure holding the files info and hierarchy (NOT THE DATA) */
    root = fpath_new(full, full, 0, NULL, 1);
    if (!root)
        goto close_out;
    get_files_info(full, root, root->depth);
    num_of_files(&counter, root);
    if (counter == 0)       /* empty or missing directory: nothing to send */
        goto close_out;

    pthread_mutex_lock(&S.vec_lock);
    for (i = 0; i < MAX_CONNS; i++) {
        if (S.conns[i].num_of_files == 0) {
            S.conns[i].sock = current_socket;
            S.conns[i].num_of_files = counter;
            S.conns[i].files_sent = 0;
            slot = i;
            break;
        }
    }
    pthread_mutex_unlock(&S.vec_lock);
    if (slot < 0) {
        ds_log("too many clients");
        STAT_ADD(rejected, 1);
        goto close_out;
    }

    /* push all the entries to file_q */
    pushed = push_entries(current_socket, root);
    if (pushed != counter) {
        /* out of memory part way: let the request finish with what was queued */
        pthread_mutex_lock(&S.vec_lock);
        S.conns[slot].num_of_files = pushed;
        if (S.conns[slot].files_sent >= pushed) {
            close(current_socket);
            S.conns[slot].sock = -1;
            S.conns[slot].num_of_files = S.conns[slot].files_sent = 0;
        }
        pthread_mutex_unlock(&S.vec_lock);
    }
    free_tree(root);
    return NULL;

close_out:
    if (root)
        free_tree(root);
    close(current_socket);
    return NULL;
}

static int start_thread(pthread_t *t, void *(*fn)(void *), void *arg, int detach)
{
    pthread_attr_t attr;
    size_t stack = S.cfg.thread_stack;
    int rc;

#ifndef DATASERVER_RTTHREAD
    if (stack < 64 * 1024)
        stack = 64 * 1024;
#endif
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack);
    if (detach)
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    rc = pthread_create(t, &attr, fn, arg);
    pthread_attr_destroy(&attr);
    return rc;
}

/* main accept loop (server.cpp main) */
static void *thread_func_accept(void *arg)
{
    (void)arg;
    for (;;) {
        struct sockaddr_in address;
        socklen_t addrlen = sizeof(address);
        pthread_t new_thread;
        int new_socket = accept(S.server_fd, (struct sockaddr *)&address, &addrlen);

        if (new_socket < 0) {
            usleep(100 * 1000);
            continue;
        }
        STAT_ADD(connections, 1);
        /* create a new communication thread with the new socket as argument */
        if (start_thread(&new_thread, thread_func_comm,
                         (void *)(intptr_t)new_socket, 1) != 0) {
            ds_log("cannot create comm thread");
            STAT_ADD(rejected, 1);
            close(new_socket);
        }
    }
    return NULL;
}

/* -------------------------------------------------------------- API ---- */

void dataserver_default_config(dataserver_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->port = 9000;
    cfg->thread_pool_size = 2;
    cfg->queue_size = 16;
    cfg->block_size = 4096;
    cfg->root = "/data/adas";
    cfg->thread_stack = 8192;
}

/* create a listening socket */
static int create_listen(int port)
{
    struct sockaddr_in address;
    int opt = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0)
        return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(fd, 3) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int dataserver_start(const dataserver_config_t *cfg)
{
    pthread_t t;
    size_t rl;
    int i;

    if (S.running)
        return -1;
    if (!g_inited) {
        pthread_mutex_init(&S.q_lock, NULL);
        pthread_mutex_init(&S.vec_lock, NULL);
        pthread_mutex_init(&S.st_lock, NULL);
        pthread_cond_init(&S.emp, NULL);
        pthread_cond_init(&S.f, NULL);
        g_inited = 1;
    }
    S.cfg = *cfg;
    if (S.cfg.thread_pool_size < 1)
        S.cfg.thread_pool_size = 1;
    if (S.cfg.queue_size < 1)
        S.cfg.queue_size = 1;
    if (S.cfg.block_size < 16)
        S.cfg.block_size = 16;

    rl = strlen(cfg->root);
    while (rl > 1 && cfg->root[rl - 1] == '/')
        rl--;
    if (rl == 0 || rl >= sizeof(S.root))
        return -1;
    memcpy(S.root, cfg->root, rl);
    S.root[rl] = '\0';
    mkdir(S.root, 0755);            /* may already exist */

    for (i = 0; i < MAX_CONNS; i++)
        S.conns[i].sock = -1;
    memset(&S.file_q, 0, sizeof(S.file_q));
    S.file_q.capacityq = S.cfg.queue_size;
    S.busy = (int *)malloc(sizeof(int) * (size_t)S.cfg.thread_pool_size);
    if (!S.busy)
        return -1;
    for (i = 0; i < S.cfg.thread_pool_size; i++)
        S.busy[i] = -1;             /* no socket has such value */

    S.server_fd = create_listen(S.cfg.port);
    if (S.server_fd < 0) {
        ds_log("cannot listen on port %d (errno %d)", S.cfg.port, errno);
        free(S.busy);
        return -1;
    }
    S.running = 1;
    /* create the thread pool of workers */
    for (i = 0; i < S.cfg.thread_pool_size; i++) {
        if (start_thread(&t, thread_func_worker, (void *)(intptr_t)i, 1) != 0) {
            ds_log("cannot create worker %d", i);
            break;
        }
    }
    if (i == 0 || start_thread(&t, thread_func_accept, NULL, 1) != 0) {
        ds_log("cannot start threads");
        return -1;          /* threads already started keep waiting; MCU reboots anyway */
    }
    ds_log("port %d, %d workers, queue %d, block %d, root %s",
           S.cfg.port, i, S.cfg.queue_size, S.cfg.block_size, S.root);
    return 0;
}

int dataserver_running(void)
{
    return S.running;
}

void dataserver_get_stats(dataserver_stats_t *out)
{
    if (!g_inited) {
        memset(out, 0, sizeof(*out));
        return;
    }
    pthread_mutex_lock(&S.st_lock);
    *out = S.stats;
    pthread_mutex_unlock(&S.st_lock);
    pthread_mutex_lock(&S.q_lock);
    out->queued = (uint32_t)S.file_q.sizeq;
    pthread_mutex_unlock(&S.q_lock);
}
