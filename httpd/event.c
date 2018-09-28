#include "httpd.h"
#include "tree.h"


struct rbnode_t {
    RB_ENTRY(rbnode_t) node;
    event_t           *ev;
};
RB_HEAD(rbtree_t, rbnode_t) _read_evs, _write_evs, _except_evs;

fd_set           _readfds                   = { 0 };
fd_set           _writefds                  = { 0 };
fd_set           _exceptfds                 = { 0 };
struct rbnode_t *_active_ns[FD_SETSIZE * 3] = { 0 };
uint32_t         _active_size               = 0;

static int  compare(struct rbnode_t *n1, struct rbnode_t *n2);
RB_PROTOTYPE(rbtree_t, rbnode_t, node, compare);
RB_GENERATE(rbtree_t, rbnode_t, node, compare);
static struct rbnode_t *create_rbnode(event_t *ev);
static struct rbnode_t *find_rbnode(uint32_t fd, struct rbtree_t *t);
static void release_rbnode(struct rbnode_t *n);
static void release_rbtree(struct rbtree_t *t);
static int  event_del_by_fd(uint32_t fd, struct rbtree_t *t, fd_set *s);
static int  rbnode_del_no_free(struct rbnode_t *n);

ret_code_t event_init()
{
    RB_INIT(&_read_evs);
    RB_INIT(&_write_evs);
    RB_INIT(&_except_evs);
    FD_ZERO(&_readfds);
    FD_ZERO(&_writefds);
    FD_ZERO(&_exceptfds);
    memset(_active_ns, 0, sizeof(struct rbnode_t*) * FD_SETSIZE * 3);
    _active_size = 0;
    return SUCC;
}

ret_code_t event_uninit()
{
    release_rbtree(&_read_evs);
    release_rbtree(&_write_evs);
    release_rbtree(&_except_evs);
    FD_ZERO(&_readfds);
    FD_ZERO(&_writefds);
    FD_ZERO(&_exceptfds);
    memset(_active_ns, 0, sizeof(struct rbnode_t*) * FD_SETSIZE * 3);
    _active_size = 0;
    return SUCC;
}

ret_code_t event_add(event_t *ev)
{
    struct rbnode_t  k;
    struct rbnode_t *n = NULL;
    struct rbtree_t *t = NULL;
    fd_set          *s = NULL;
    if (ev->type & EV_READ)
    {
        t = &_read_evs;
        s = &_readfds;
    }
    else if (ev->type & EV_WRITE)
    {
        t = &_write_evs;
        s = &_writefds;
    }
    else if (ev->type & EV_EXCEPT)
    {
        t = &_except_evs;
        s = &_exceptfds;
    }
    if (!t || !s)
    {
        log_error("{%s:%d} invalid params", __FUNCTION__, __LINE__);
        return PARA;
    }

    if (s->fd_count >= FD_SETSIZE)
    {
        log_warn("{%s:%d} event map is full", __FUNCTION__, __LINE__);
        return FULL;
    }
    k.ev = ev;
    n = RB_FIND(rbtree_t, t, &k);
    if (n)
    {
        log_warn("{%s:%d} event is already exist, fd=%d", __FUNCTION__, __LINE__, ev->fd);
        return EXIS;
    }
    n = create_rbnode(ev);
    if (!n)
    {
        return FAIL;
    }
    RB_INSERT(rbtree_t, t, n);
    FD_SET(ev->fd, s);
    return SUCC;
}

ret_code_t event_del(event_t *ev)
{
    uint32_t fd = ev->fd;
    event_del_by_fd(fd, &_read_evs, &_readfds);
    event_del_by_fd(fd, &_write_evs, &_writefds);
    event_del_by_fd(fd, &_except_evs, &_exceptfds);
    return SUCC;
}

ret_code_t event_dispatch()
{
    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;
    struct timeval timeout = { 0, 500000 };
    int ret;
    uint32_t i;

    while (TRUE)
    {
        _active_size = 0;
        memcpy(&readfds, &_readfds, sizeof(_readfds.fd_count) + _readfds.fd_count * sizeof(SOCKET));
        memcpy(&writefds, &_writefds, sizeof(_writefds.fd_count) + _writefds.fd_count * sizeof(SOCKET));
        memcpy(&exceptfds, &_exceptfds, sizeof(_exceptfds.fd_count) + _exceptfds.fd_count * sizeof(SOCKET));

        ret = select(0, &readfds, &writefds, &exceptfds, &timeout);
        switch (ret)
        {
        case 0: // the time limit expired
            break;
        case SOCKET_ERROR: // an error occurred
            log_error("{%s:%d} an error occurred at select. WSAGetLastError=%d", __FUNCTION__, __LINE__, WSAGetLastError());
            return FAIL;
        default: // the total number of socket handles that are ready
            for (i=0; i<readfds.fd_count; i++)
            {
                _active_ns[_active_size++] = find_rbnode(readfds.fd_array[i], &_read_evs);
            }
            for (i=0; i<writefds.fd_count; i++)
            {
                _active_ns[_active_size++] = find_rbnode(writefds.fd_array[i], &_write_evs);
            }
            for (i=0; i<exceptfds.fd_count; i++)
            {
                _active_ns[_active_size++] = find_rbnode(exceptfds.fd_array[i], &_except_evs);
            }
            break;
        }

        for (i = 0; i < _active_size; i++)
        {
            if (!_active_ns[i]) // is disconnect. reset by event_del_by_fd()
            {
                continue;
            }
            if (!(_active_ns[i]->ev->type & EV_PERSIST))
                rbnode_del_no_free(_active_ns[i]);
            _active_ns[i]->ev->callback(_active_ns[i]->ev);
            if (_active_ns[i] && !(_active_ns[i]->ev->type & EV_PERSIST))
                release_rbnode(_active_ns[i]);
        }
    }
    return SUCC;
}

static int compare(struct rbnode_t *n1, struct rbnode_t *n2)
{
    if (n1->ev->fd < n2->ev->fd)
        return -1;
    else if (n1->ev->fd > n2->ev->fd)
        return 1;
    else
        return 0;
}

static struct rbnode_t *create_rbnode(event_t *ev)
{
    struct rbnode_t *n = NULL;
    event_t *e = NULL;

    e = (event_t *)malloc(sizeof(event_t));
    if (!e)
    {
        log_error("{%s:%d} malloc failed", __FUNCTION__, __LINE__);
        return n;
    }
    n = (struct rbnode_t *)malloc(sizeof(struct rbnode_t));
    if (!n)
    {
        log_error("{%s:%d} malloc failed", __FUNCTION__, __LINE__);
        free(e);
        return n;
    }
    memcpy(e, ev, sizeof(event_t));
    memset(n, 0, sizeof(struct rbnode_t));
    n->ev = e;
    return n;
}

static struct rbnode_t *find_rbnode(uint32_t fd, struct rbtree_t *t)
{
    struct rbnode_t k = { 0 };
    struct event_t  e = { 0 };

    e.fd = fd;
    k.ev = &e;
    return RB_FIND(rbtree_t, t, &k);
}

static void release_rbnode(struct rbnode_t *n)
{
    free(n->ev);
    free(n);
}

static void release_rbtree(struct rbtree_t *t)
{
    static struct rbnode_t *ns[FD_SETSIZE] = { 0 };
    int size = 0;
    struct rbnode_t *n = NULL;

    n = RB_MIN(rbtree_t, t);
    while (n)
    {
        ns[size++] = n;
        n = RB_NEXT(rbtree_t, t, n);
    }
    RB_INIT(t);
    while (size)
    {
        release_rbnode(ns[--size]);
    }
}

static int event_del_by_fd(uint32_t fd, struct rbtree_t *t, fd_set *s)
{
    struct rbnode_t  k;
    struct rbnode_t *n = NULL;
    event_t e = { 0 };
    uint32_t i;

    for (i = 0; i<_active_size; i++)
    {
        if (_active_ns[i] && _active_ns[i]->ev->fd == fd)
        {
            _active_ns[i] = NULL;
        }
    }

    e.fd = fd;
    k.ev = &e;
    n = RB_FIND(rbtree_t, t, &k);
    if (n)
    {
        RB_REMOVE(rbtree_t, t, n);
        FD_CLR(fd, s);
        release_rbnode(n);
    }
    return SUCC;
}

static int rbnode_del_no_free(struct rbnode_t *n)
{
    struct rbtree_t *t = NULL;
    fd_set          *s = NULL;
    if (n->ev->type & EV_READ)
    {
        t = &_read_evs;
        s = &_readfds;
    }
    else if (n->ev->type & EV_WRITE)
    {
        t = &_write_evs;
        s = &_writefds;
    }
    else if (n->ev->type & EV_EXCEPT)
    {
        t = &_except_evs;
        s = &_exceptfds;
    }
    if (!t || !s)
    {
        log_error("{%s:%d} invalid params", __FUNCTION__, __LINE__);
        return PARA;
    }
    n = RB_FIND(rbtree_t, t, n);
    if (!n)
    {
        log_warn("{%s:%d} event is not exist, fd=%d", __FUNCTION__, __LINE__, n->ev->fd);
        return NEXI;
    }
    RB_REMOVE(rbtree_t, t, n);
    FD_CLR(n->ev->fd, s);
    return SUCC;
}
