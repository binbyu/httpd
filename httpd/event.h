#ifndef __EVENT_H__
#define __EVENT_H__

typedef enum
{
    EV_UNKNOWN              = 0x00,
    EV_READ                 = 0x01,
    EV_WRITE                = 0x02,
    EV_EXCEPT               = 0x04,
    EV_PERSIST              = 0x08
} event_type_t;

typedef enum  
{
    evIdle                  = 0,
    evBusy,
    evFinish
} event_status_t;

typedef struct
{
    char                    file[MAX_PATH];
    uint32_t                total;
    uint32_t                offset;
    uint32_t                tail;
    uint32_t                size;
    FILE                   *fp;         // fixed fopen EACCES error. just for write file
    char                    data[1];
} event_data_t;

typedef struct event_t event_t;
struct event_t
{
    uint32_t                fd;         // socket
    uint32_t                ip;         // ip 
    uint8_t                 type;       // event_type_t
    uint8_t                 status;     // event_status_t
    void                   *param;      // param
    event_data_t           *data;       // event_data_t*
    void (*callback) (event_t*);
};

ret_code_t event_init();
ret_code_t event_uninit();
ret_code_t event_add(event_t *ev);
ret_code_t event_del(event_t *ev);
ret_code_t event_dispatch();

#endif