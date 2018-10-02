#include "httpd.h"
#include <errno.h>


#define LF                  (u_char) '\n'
#define CR                  (u_char) '\r'
#define CRLF                "\r\n"

typedef struct  
{
    char                *key;
    char                *value;
} request_fields_t;

typedef struct
{
    char                *method;
    char                *uri;
    char                *version;
    uint8_t             fields_count;
    request_fields_t    *fields;
} request_header_t;

static void accept_callback(event_t *ev);
static void read_callback(event_t *ev);
static void write_callback(event_t *ev);

static int   read_request_header(event_t *ev, char **buf, int *size);
static void  read_request_boundary(event_t *ev);
static int   parse_request_header(char *data, request_header_t *header);
static void  release_request_header(request_header_t *header);
static void  release_event(event_t *ev);
static event_data_t *create_event_data(const char *header, const char *html);
static event_data_t *create_event_data_fp(const char *header, FILE *fp, int read_len, int total_len);
static void  release_event_data(event_t *ev);
static void  uri_decode(char* uri);
static uint8_t ishex(uint8_t x);
static char *local_file_list(char *path);
static int   reset_filename_from_formdata(event_t *ev, char **formdata, int size);
static int   parse_boundary(event_t *ev, char *data, int size, char **ptr);

static const char *reponse_content_type(char *file_name);
static const char *response_header_format();
static const char *response_body_format();
static void response_home_page(event_t *ev, char *path);
static void response_upload_page(event_t *ev, int result);
static void response_send_file_page(event_t *ev, char *file_name);
static void response_http_400_page(event_t *ev);
static void response_http_404_page(event_t *ev);
static void response_http_500_page(event_t *ev);
static void response_http_501_page(event_t *ev);
static void send_response(event_t *ev, char* title, char *status);

int http_startup(uint16_t *port)
{
    SOCKET fd;
    event_t ev = {0};

    log_info("{%s:%d} Http server start...", __FUNCTION__, __LINE__);
    network_init();
    event_init();
    network_listen(port, &fd);

    ev.fd = fd;
    ev.ip = htonl(INADDR_ANY);
    ev.type = EV_READ | EV_PERSIST;
    ev.callback = accept_callback;
    event_add(&ev);

    event_dispatch();

    closesocket(fd);
    event_uninit();
    network_unint();
    log_info("{%s:%d} Http server stop ...", __FUNCTION__, __LINE__);
    return SUCC;
}

static void accept_callback(event_t *ev)
{
    SOCKET fd;
    struct in_addr addr;
    event_t ev_ = {0};

    if (SUCC == network_accept(ev->fd, &addr, &fd))
    {   
        ev_.fd = fd;
        ev_.ip = addr.s_addr;
        ev_.type = EV_READ | EV_PERSIST;
        ev_.callback = read_callback;
        event_add(&ev_);

        log_info("{%s:%d} A new client connect. ip = %s, socket=%d", __FUNCTION__, __LINE__, inet_ntoa(addr), fd);
    }
    else
    {
        log_error("{%s:%d} accept fail. WSAGetLastError=%d", __FUNCTION__, __LINE__, WSAGetLastError());
    }
}

static void read_callback(event_t *ev)
{
    char *buf = NULL;
    int   size;
    request_header_t header;
    int   i;
    int   content_length = 0;
    char *temp = NULL;
    char  file_path[MAX_PATH] = {0};

    if (ev->status == EV_IDLE)
    {
        if (SUCC != read_request_header(ev, &buf, &size))
        {
            response_http_400_page(ev);
            free(buf);
            return;
        }
        if (!buf)
            return;
        parse_request_header(buf, &header);
        uri_decode(header.uri);
        header.uri = utf8_to_ansi(header.uri);
        log_info("{%s:%d} >>> Entry recv ... uri=%s", __FUNCTION__, __LINE__, header.uri);
        if (strcmp(header.method, "GET") && strcmp(header.method, "POST"))
        {
            // 501 Not Implemented
            response_http_501_page(ev);
            release_request_header(&header);
            free(buf);
            return;
        }
        if (0 == strcmp(header.method, "POST"))
        {
            /***** This program is not supported now.                             *****/
            /***** Just using Content-Type = multipart/form-data when upload file *****/

            // 1. get Content-Length
            // 2. if don't have Content-Length. using chunk
            // 3. read_request_body()
            // 4. get Content-Type
            // 5. Content-Type is json or others
        }
        if (0 == strncmp(header.uri, "/upload", strlen("/upload")))
        {
            // get upload file save path from uri
            memset(file_path, 0, sizeof(file_path));
            memcpy(file_path, root_path(), strlen(root_path()));
            if (strlen(header.uri) > strlen("/upload?path="))
            {
                memcpy(file_path+strlen(file_path), header.uri+strlen("/upload?path="), strlen(header.uri)-strlen("/upload?path="));
            }
            if (0 == strcmp(header.method, "POST"))
            {
                // get Content-Length boundary_length
                for (i=0; i<header.fields_count; i++)
                {
                    if (0 == strcmp(header.fields[i].key, "Content-Length"))
                    {
                        content_length = atoi(header.fields[i].value);
                        break;
                    }
                }
                if (content_length == 0)
                {
                    // not support
                    // 501 Not Implemented
                    response_http_501_page(ev);
                    release_request_header(&header);
                    free(buf);
                    return;
                }

                // get boundary
                ev->data = (event_data_t*)malloc(sizeof(event_data_t)-sizeof(char)+BUFFER_UNIT);
                memset(ev->data, 0, sizeof(event_data_t)-sizeof(char)+BUFFER_UNIT);
                if (!ev->data)
                {
                    // 500 Internal Server Error
                    response_http_500_page(ev);
                    release_request_header(&header);
                    free(buf);
                    return;
                }
                for (i=0; i<header.fields_count; i++)
                {
                    if (0 == strcmp(header.fields[i].key, "Content-Type"))
                    {
                        temp = strstr(header.fields[i].value, "boundary=");
                        if (temp)
                        {
                            temp += strlen("boundary=");
                            memcpy(ev->data->boundary, temp, strlen(temp));
                        }
                        break;
                    }
                }
                if (ev->data->boundary[0] == 0)
                {
                    // not support
                    // 501 Not Implemented
                    response_http_501_page(ev);
                    release_request_header(&header);
                    free(buf);
                    return;
                }

                // relase memory
                release_request_header(&header);
                free(buf);

                // set event
                memcpy(ev->data->file, file_path, strlen(file_path));
                ev->data->offset = 0;
                ev->data->total = content_length;

                // read & save files
                read_request_boundary(ev);
            }
            else
            {
                // not support
                // 501 Not Implemented
                response_http_501_page(ev);
                free(buf);
                release_request_header(&header);
                return;
            }
        }
        else if (header.uri[strlen(header.uri)-1] == '/')
        {
            response_home_page(ev, header.uri+1);
            free(buf);
            release_request_header(&header);
            return;
        }
        else
        {
            // send file
            memset(file_path, 0, sizeof(file_path));
            memcpy(file_path, root_path(), strlen(root_path()));
            memcpy(file_path+strlen(file_path), header.uri+1, strlen(header.uri+1));
            response_send_file_page(ev, file_path);
            free(buf);
            release_request_header(&header);
            return;
        }
    }
    else
    {
        // read & save files
        read_request_boundary(ev);
    }
}

static void write_callback(event_t *ev)
{
    if (!ev->data)
        return;

    if (ev->data->size != send(ev->fd, ev->data->data, ev->data->size, 0))
    {
        log_error("{%s:%d} send fail. socket=%d, WSAGetLastError=%d", __FUNCTION__, __LINE__, ev->fd, WSAGetLastError());
        shutdown(ev->fd, SD_SEND);
        release_event_data(ev);
        return;
    }
    log_debug("{%s:%d} send response completed. progress=%d%%, socket=%d", __FUNCTION__, __LINE__, ev->data->total ? ev->data->offset*100/ev->data->total : 100, ev->fd);
    if (ev->data->total == ev->data->offset)
    {
        log_info("{%s:%d} send response completed. socket=%d", __FUNCTION__, __LINE__, ev->fd);
        release_event_data(ev);
    }
    else
    {
        response_send_file_page(ev, ev->data->file);
    }
}

static int read_request_header(event_t *ev, char **buf, int *size)
{
    char c;
    int len = 1;
    int idx = 0;
    int ret;

    while (TRUE)
    {
        ret = network_read(ev->fd, &c, len);
        if (ret == DISC)
        {
            release_event(ev);
            return SUCC;
        }
        else if (ret == SUCC)
        {
            if (*buf == NULL)
            {
                *size = BUFFER_UNIT;
                *buf = (char*)malloc(*size);
                if (!(*buf))
                {
                    log_error("{%s:%d} malloc fail.", __FUNCTION__, __LINE__);
                    return FAIL;
                }
            }
            (*buf)[idx++] = c;
            if (idx >= *size - 1) // last char using for '\0'
            {
                // buffer is not enough
                *size += BUFFER_UNIT;
                *buf = (char*)realloc(*buf, *size);
                if (!(*buf))
                {
                    log_error("{%s:%d} realloc fail.", __FUNCTION__, __LINE__);
                    return FAIL;
                }
            }
            if (idx >= 4 && (*buf)[idx - 1] == LF && (*buf)[idx - 2] == CR
                && (*buf)[idx - 3] == LF && (*buf)[idx - 4] == CR)
            {
                (*buf)[idx] = 0;
                return SUCC;
            }
        }
        else
        {
            log_error("{%s:%d} recv unknown fail.", __FUNCTION__, __LINE__);
            return FAIL;
        }
    }

    log_error("{%s:%d} cannot found header.", __FUNCTION__, __LINE__);
    return FAIL;
}

static void read_request_boundary(event_t *ev)
{
#define WRITE_FILE(fp, buf, size, ev) do { \
    if (size) { \
        if (size != fwrite(compare_buff, 1, size, fp)) { \
        log_error("{%s:%d} write file fail. socket=%d", __FUNCTION__, __LINE__, ev->fd); \
            release_event_data(ev); \
            ev->status = EV_IDLE; \
            response_upload_page(ev, 0); \
            return; \
        } \
    } \
} while (0)

#define GET_FILENAME(ev, ptr, size_) do { \
    ret = reset_filename_from_formdata(ev, &ptr, (size_)); \
    if (ret == 0) { \
        log_error("{%s:%d} cannot found filename in formdata. socket=%d", __FUNCTION__, __LINE__, ev->fd); \
        release_event_data(ev); \
        ev->status = EV_IDLE; \
        response_upload_page(ev, 0); \
        return; \
    } else if (ret == 1) { \
        ev->data->fp = fopen(ev->data->file, "wb"); \
        if (!ev->data->fp) { \
            log_error("{%s:%d} open file fail. filename=%s, socket=%d, errno=%d", __FUNCTION__, __LINE__, ev->data->file, ev->fd, errno); \
            release_event_data(ev); \
            ev->status = EV_IDLE; \
            response_upload_page(ev, 0); \
            return; \
        } \
        compare_buff_size -= ptr - compare_buff; \
        memcpy(compare_buff, ptr, compare_buff_size); \
        compare_buff[compare_buff_size] = 0; \
        goto _re_find; \
    } else { \
        ev->data->size = compare_buff + compare_buff_size - ptr; \
        memcpy(ev->data->data, ptr, ev->data->size); \
    } \
} while (0)

    char    *buf    = NULL;
    char    *ptr    = NULL;
    int      ret    = 0;
    uint32_t offset = 0;
    uint32_t writen = 0;
    uint32_t compare_buff_size = 0;
    char     buffer[BUFFER_UNIT+1] = {0};
    char     compare_buff[BUFFER_UNIT*2 + 1] = {0};

    offset = ev->data->total - ev->data->offset > BUFFER_UNIT ? BUFFER_UNIT : ev->data->total - ev->data->offset;
    ret = network_read(ev->fd, buffer, offset);
    if (ret == DISC)
    {
        response_http_500_page(ev);
        release_event(ev);
        return;
    }
    else if (ret == SUCC)
    {
        memset(compare_buff, 0, sizeof(compare_buff));
        if (ev->data->size)
        {
            memcpy(compare_buff, ev->data->data, ev->data->size);
        }
        memcpy(compare_buff+ev->data->size, buffer, offset);
        compare_buff_size = offset + ev->data->size;
        ev->data->size = 0;
        memset(ev->data->data, 0, BUFFER_UNIT);

        // parse boundary
    _re_find:
        ret = parse_boundary(ev, compare_buff, compare_buff_size, &ptr);
        switch (ret)
        {
        case 0: // write all bytes to file
            WRITE_FILE(ev->data->fp, compare_buff, compare_buff_size, ev);
            log_debug("{%s:%d} upload [%s] progress=%d%%. socket=%d", __FUNCTION__, __LINE__, ev->data->file, ev->data->offset * 100 / ev->data->total, ev->fd);
            break;
        case 1: // first boundary
            // get file name from boundary header
            GET_FILENAME(ev, ptr, compare_buff + compare_buff_size - ptr);
            break;
        case 2: // last boundary
            ASSERT(ev->data->total == ev->data->offset + offset);
            writen = ptr - compare_buff;
            // writen bytes before boundary
            WRITE_FILE(ev->data->fp, compare_buff, writen, ev);
            fclose(ev->data->fp);
            log_info("{%s:%d} upload [%s] complete. socket=%d", __FUNCTION__, __LINE__, ev->data->file, ev->fd);
            release_event_data(ev);
            ev->status = EV_IDLE;
            response_upload_page(ev, 1);
            return;
        case 3: // middle boundary
            // writen bytes before boundary
            writen = ptr - compare_buff;
            WRITE_FILE(ev->data->fp, compare_buff, writen, ev);
            fclose(ev->data->fp);
            log_info("{%s:%d} upload [%s] complete. socket=%d", __FUNCTION__, __LINE__, ev->data->file, ev->fd);
            // get file name from boundary header
            GET_FILENAME(ev, ptr, compare_buff + compare_buff_size - ptr);
            break;
        case 4: // backup last boundary
        case 5: // backup middle boundary
            writen = ptr - compare_buff;
            if (writen)
            {
                if (writen != fwrite(compare_buff, 1, writen, ev->data->fp))
                {
                    log_error("{%s:%d} write file fail. socket=%d", __FUNCTION__, __LINE__, ev->fd);
                    release_event_data(ev);
                    ev->status = EV_IDLE;
                    response_upload_page(ev, 0);
                    return;
                }
            }
            // backup
            ev->data->size = compare_buff + compare_buff_size - ptr;
            memcpy(ev->data->data, ptr, ev->data->size);
            break;
        default:
            break;
        }

        ev->data->offset += offset;
        ev->status = EV_BUSY;
    }
    else
    {
        log_error("{%s:%d} recv unknown fail.", __FUNCTION__, __LINE__);
        release_event_data(ev);
        ev->status = EV_IDLE;
        response_upload_page(ev, 0);
        return;
    }
}

static int parse_request_header(char *data, request_header_t *header)
{
#define move_next_line(x)   while (*x && *(x + 1) && *x != CR && *(x + 1) != LF) x++;
#define next_header_line(x) while (*x && *(x + 1) && *x != CR && *(x + 1) != LF) x++; *x=0;
#define next_header_word(x) while (*x && *x != ' ' && *x != ':' && *(x + 1) && *x != CR && *(x + 1) != LF) x++; *x=0;

    char *p = data;
    char *q;
    int idx = 0;

    memset(header, 0, sizeof(request_header_t));
    // method
    next_header_word(p);
    header->method = data;
    // uri
    data = ++p;
    next_header_word(p);
    header->uri = data;
    // version
    data = ++p;
    next_header_word(p);
    header->version = data;
    // goto fields data
    next_header_line(p);
    data = ++p + 1;
    p++;
    // fields_count
    q = p;
    while (*p)
    {
        move_next_line(p);
        data = ++p + 1;
        p++;
        header->fields_count++;
        if (*data && *(data + 1) && *data == CR && *(data + 1) == LF)
            break;
    }
    // malloc fields
    header->fields = (request_fields_t*)malloc(sizeof(request_fields_t)*header->fields_count);
    if (!header->fields)
    {
        log_error("{%s:%d} malloc fail.", __FUNCTION__, __LINE__);
        return FAIL;
    }
    // set fields
    data = p = q;
    while (*p)
    {
        next_header_word(p);
        header->fields[idx].key = data;
        data = ++p;
        if (*data == ' ')
        {
            data++;
            p++;
        }
        next_header_line(p);
        header->fields[idx++].value = data;
        data = ++p + 1;
        p++;
        if (*data && *(data + 1) && *data == CR && *(data + 1) == LF)
            break;
    }
    ASSERT(idx == header->fields_count);
    return SUCC;
}

static void release_request_header(request_header_t *header)
{
    if (header->uri)
    {
        free(header->uri);
    }
    if (header->fields)
    {
        free(header->fields);
    }
}

static void release_event(event_t *ev)
{
    closesocket(ev->fd);
    release_event_data(ev);
    event_del(ev);
}

static event_data_t *create_event_data(const char *header, const char *html)
{
    event_data_t* ev_data = NULL;
    int header_length = 0;
    int html_length = 0;
    int data_length = 0;

    if (header)
        header_length = strlen(header);
    if (html)
        html_length = strlen(html);

    data_length = sizeof(event_data_t) - sizeof(char) + header_length + html_length;
    ev_data = (event_data_t*)malloc(data_length);
    if (!ev_data)
    {
        log_error("{%s:%d} malloc failed", __FUNCTION__, __LINE__);
        return ev_data;
    }
    memset(ev_data, 0, data_length);
    ev_data->total = header_length + html_length;
    ev_data->offset = header_length + html_length;
    ev_data->size = header_length + html_length;
    if (header)
        memcpy(ev_data->data, header, header_length);
    if (html)
        memcpy(ev_data->data + header_length, html, html_length);
    return ev_data;
}

static event_data_t *create_event_data_fp(const char *header, FILE *fp, int read_len, int total_len)
{
    event_data_t* ev_data = NULL;
    int header_length = 0;
    int data_length = 0;

    if (header)
        header_length = strlen(header);

    data_length = sizeof(event_data_t) - sizeof(char) + header_length + read_len;
    ev_data = (event_data_t*)malloc(data_length);
    if (!ev_data)
    {
        log_error("{%s:%d} malloc failed", __FUNCTION__, __LINE__);
        return ev_data;
    }
    memset(ev_data, 0, data_length);
    ev_data->total = total_len;
    ev_data->size = read_len + header_length;
    if (header)
        memcpy(ev_data->data, header, header_length);
    if (read_len != fread(ev_data->data + header_length, 1, read_len, fp))
    {
        log_error("{%s:%d} fread failed", __FUNCTION__, __LINE__);
        free(ev_data);
        ev_data = NULL;
    }
    return ev_data;
}

static void release_event_data(event_t *ev)
{
    if (ev->data)
    {
        if (ev->data->fp)
        {
            fclose(ev->data->fp);
            ev->data->fp = NULL;
        }
        free(ev->data);
        ev->data = NULL;
    }
}

static void uri_decode(char* uri)
{
    int len = strlen(uri);
    char *out = NULL;
    char *o = out;
    char *s = uri;
    char *end = uri + strlen(uri);
    int c;

    out = (char*)malloc(len+1);
    if (!out)
    {
        log_error("{%s:%d} malloc fail.", __FUNCTION__, __LINE__);
        return;
    }

    for (o = out; s <= end; o++)
    {
        c = *s++;
        if (c == '+')
        {
            c = ' ';
        }
        else if (c == '%' 
            && (!ishex(*s++) || !ishex(*s++) || !sscanf(s - 2, "%2x", &c)))
        {
            // bad uri
            free(out);
            return;
        }

        if (out)
        {
            *o = c;
        }
    }

    memcpy(uri, out, strlen(out));
    uri[strlen(out)] = 0;
    free(out);
}

static uint8_t ishex(uint8_t x)
{
    return	(x >= '0' && x <= '9')	||
        (x >= 'a' && x <= 'f')	||
        (x >= 'A' && x <= 'F');
}

static int reset_filename_from_formdata(event_t *ev, char **formdata, int size)
{
    char *file_name = NULL;
    char *p         = NULL;
    int   i         = 0;
    int   found     = 0;
    char *anis      = NULL;

    // find "\r\n\r\n"
    p = *formdata;
    for (int i = 0; i <= size-4; i++)
    {
        if (0 == memcmp(*formdata + i, CRLF CRLF, strlen(CRLF CRLF)))
        {
            found = 1;
            (*formdata)[i] = 0;
            (*formdata) += i + strlen(CRLF CRLF);
            break;
        }
    }
    if (!found)
        return 2;

    // file upload file name from formdata
    file_name = strstr(p, "filename=\"");
    if (!file_name)
        return 0;
    file_name += strlen("filename=\"");
    *strstr(file_name, "\"") = 0;

    anis = utf8_to_ansi(file_name);

    // IE browser: remove client file path
    p = anis;
    while (*p)
    {
        if (*p == '\\' || *p == '/')
        if (*(p + 1))
            anis = p + 1;
        p++;
    }

    // reset filepath
    for (i = strlen(ev->data->file) - 1; i >= 0; i--)
    {
        if (ev->data->file[i] == '/')
        {
            memcpy(ev->data->file + i + 1, anis, strlen(anis) + 1);
            break;
        }
    }

    // if file exist delete file
    if (file_exist(ev->data->file))
    {
        remove_file(ev->data->file);
    }
    free(anis);
    return 1;
}

static int parse_boundary(event_t *ev, char *data, int size, char **ptr)
{
    char first_boundary[BOUNDARY_MAX_LEN]  = { 0 };
    char middle_boundary[BOUNDARY_MAX_LEN] = { 0 };
    char last_boundary[BOUNDARY_MAX_LEN]   = { 0 };
    int  first_len, middle_len, last_len;
    int i;

    sprintf(first_boundary, "--%s\r\n", ev->data->boundary);      //------WebKitFormBoundaryOG3Viw9MEZcexbvT\r\n
    sprintf(middle_boundary, "\r\n--%s\r\n", ev->data->boundary);   //\r\n----WebKitFormBoundaryOG3Viw9MEZcexbvT\r\n
    sprintf(last_boundary, "\r\n--%s--\r\n", ev->data->boundary); //\r\n------WebKitFormBoundaryOG3Viw9MEZcexbvT--\r\n
    first_len  = strlen(first_boundary);
    middle_len = strlen(middle_boundary);
    last_len   = strlen(last_boundary);

    ASSERT(size > first_len);
    ASSERT(size > middle_len);
    ASSERT(size > last_len);
    if (0 == memcmp(data, first_boundary, first_len))
    {
        *ptr = data + first_len;
        return 1; // first boundary
    }

    if (0 == memcmp(data + (size - last_len), last_boundary, last_len))
    {
        *ptr = data + (size - last_len);
        return 2; // last boundary
    }

    for (i = 0; i < size; i++)
    {
        if (size - i >= last_len)
        {
            if (0 == memcmp(data + i, middle_boundary, middle_len))
            {
                *ptr = data + i/* + middle_len*/;
                return 3; // middle boundary
            }
        }
        else if (size - i >= middle_len && size - i < last_len)
        {
            if (0 == memcmp(data + i, middle_boundary, middle_len))
            {
                *ptr = data + i/* + middle_len*/;
                return 3; // middle boundary
            }
            if (0 == memcmp(data + i, last_boundary, size - i))
            {
                *ptr = data + i;
                return 4; // backup last boundary
            }
        }
        else if (size - i >= 7 && size - i < middle_len)
        {
            if (0 == memcmp(data + i, last_boundary, size - i))
            {
                *ptr = data + i;
                return 4; // backup last boundary
            }
            if (0 == memcmp(data + i, middle_boundary, size - i))
            {
                *ptr = data + i;
                return 5; // backup middle boundary
            }
        }
        else
        {
            if (0 == memcmp(data + i, middle_boundary, size - i))
            {
                *ptr = data + i;
                return 5; // backup middle boundary
            }
            if (0 == memcmp(data + i, last_boundary, size - i))
            {
                *ptr = data + i;
                return 4; // backup last boundary
            }
        }
    }

    return 0;
}

static char* local_file_list(char *path)
{
    const char* format_dir = "<a href=\"%s/\">%s/</a>" CRLF;
    const char* format_file = "<a href=\"%s\">%s</a>";
    WIN32_FIND_DATAA FindFileData;
    HANDLE hFind;
    char filter[MAX_PATH] = {0};
    char *result = NULL;
    char line[BUFFER_UNIT] = {0};
    int line_length;
    int size = BUFFER_UNIT;
    int offset = 0;
    char *size_str = NULL;
    char *utf8 = NULL;
    int i;

    sprintf(filter, "%s*", path);
    // list directory
    hFind = FindFirstFileA(filter, &FindFileData);
    if (hFind == INVALID_HANDLE_VALUE) 
    {
        log_error("{%s:%d} Invalid File Handle. GetLastError=%d", __FUNCTION__, __LINE__, GetLastError());
        return NULL;
    }
    do 
    {
        if (FILE_ATTRIBUTE_DIRECTORY & FindFileData.dwFileAttributes)
        {
            if (!result)
            {
                result = (char*)malloc(size);
            }
            utf8 = ansi_to_utf8(FindFileData.cFileName);
            sprintf(line, format_dir, utf8, utf8);
            line_length = strlen(line);
            line[line_length] = 0;
            if (offset+line_length > size-1)
            {
                size += BUFFER_UNIT;
                result = (char*)realloc(result, size);
            }
            memcpy(result+offset, line, line_length);
            offset += line_length;
            free(utf8);
        }
    } while (FindNextFileA(hFind, &FindFileData));
    FindClose(hFind);

    // list files
    hFind = FindFirstFileA(filter, &FindFileData);
    if (hFind == INVALID_HANDLE_VALUE) 
    {
        log_error("{%s:%d} Invalid File Handle. GetLastError=%d", __FUNCTION__, __LINE__, GetLastError());
        return NULL;
    }
    do 
    {
        if (!(FILE_ATTRIBUTE_DIRECTORY & FindFileData.dwFileAttributes))
        {
            if (!result)
            {
                result = (char*)malloc(size);
            }
            utf8 = ansi_to_utf8(FindFileData.cFileName);
            sprintf(line, format_file, utf8, utf8);
            line_length = strlen(line);
            for (i=strlen(FindFileData.cFileName); i<60; i++)
            {
                line[line_length++] = ' ';
            }
            size_str = uint32_to_str(FindFileData.nFileSizeLow);
            memcpy(line+line_length, size_str, strlen(size_str));
            line_length += strlen(size_str);
            line[line_length++] = CR;
            line[line_length++] = LF;
            line[line_length] = 0;

            if (offset+line_length > size-1)
            {
                size += BUFFER_UNIT;
                result = (char*)realloc(result, size);
            }
            memcpy(result+offset, line, line_length);
            offset += line_length;
            free(utf8);
        }
    } while (FindNextFileA(hFind, &FindFileData));
    FindClose(hFind);
    result[offset] = 0;

    return result;
}

static const char *reponse_content_type(char *file_name)
{
    const request_fields_t content_type[] =     {
        { "html",   "text/html"                 },
        { "css",    "text/css"                  },
        { "txt",    "text/plain"                },
        { "log",    "text/plain"                },
        { "cpp",    "text/plain"                },
        { "c",      "text/plain"                },
        { "h",      "text/plain"                },
        { "js",     "application/x-javascript"  },
        { "png",    "application/x-png"         },
        { "jpg",    "image/jpeg"                },
        { "jpeg",   "image/jpeg"                },
        { "jpe",    "image/jpeg"                },
        { "gif",    "image/gif"                 },
        { "ico",    "image/x-icon"              },
        { "doc",    "application/msword"        },
        { "docx",   "application/msword"        },
        { "ppt",    "application/x-ppt"         },
        { "pptx",   "application/x-ppt"         },
        { "xls",    "application/x-xls"         },
        { "xlsx",   "application/x-xls"         },
        { "mp4",    "video/mpeg4"               },
        { "mp3",    "audio/mp3"                 }
    };

    char *ext = NULL;
    int i;

    if (!file_name)
    {
        return "text/html";
    }
    ext = file_ext(file_name);
    if (ext)
    {
        for (i=0; i<sizeof(content_type)/sizeof(content_type[0]); i++)
        {
            if (0 == strcmp(content_type[i].key, ext))
            {
                return content_type[i].value;
            }
        }
    }
    return "application/octet-stream";
}

static const char *response_header_format()
{
    const char *http_header_format =
        "HTTP/1.1 %s" CRLF
        "Content-Type: %s" CRLF
        "Content-Length: %d" CRLF
        CRLF;

    return http_header_format;
}

static const char *response_body_format()
{
    const char *http_body_format =
        "<html>" CRLF
        "<head><title>%s</title></head>" CRLF
        "<body bgcolor=\"white\">" CRLF
        "<center><h1>%s</h1></center>" CRLF
        "</body></html>";

    return http_body_format;
}

static void response_home_page(event_t *ev, char *path)
{
    const char *html_format = 
        "<html>" CRLF
        "<head>" CRLF
        "<meta charset=\"utf-8\">" CRLF
        "<title>Index of /%s</title>" CRLF
        "</head>" CRLF
        "<body bgcolor=\"white\">" CRLF
        "<h1>Index of /%s</h1><hr>" CRLF
        "<form action=\"/upload?path=%s\" method=\"post\" enctype=\"multipart/form-data\">" CRLF
        "<input type=\"file\" name=\"file\" multiple=\"true\" />" CRLF
        "<input type=\"submit\" value=\"Upload\" /></form><hr><pre>" CRLF
        "%s" CRLF
        "</body></html>";

    char header[BUFFER_UNIT] = { 0 };
    event_data_t* ev_data = NULL;
    int length;
    char *file_list = NULL;
    char *html = NULL;
    event_t ev_ = {0};
    char *utf8 = NULL;

    utf8 = ansi_to_utf8(path);
    file_list = local_file_list(path);
    if (!file_list)
        return;

    length = strlen(html_format) + strlen(file_list) + (strlen(utf8) - strlen("%s"))*3 + 1;
    html = (char*)malloc(length);
    if (!html)
    {
        log_error("{%s:%d} malloc fail.", __FUNCTION__, __LINE__);
        return;
    }
    sprintf(html, html_format, utf8, utf8, utf8, file_list);
    free(utf8);
    free(file_list);
    sprintf(header, response_header_format(), "200 OK", reponse_content_type(NULL), strlen(html));
    ev_data = create_event_data(header, html);
    free(html);

    ev_.fd = ev->fd;
    ev_.ip = ev->ip;
    ev_.type = EV_WRITE;
    ev_.param = ev->param;
    ev_.data = ev_data;
    ev_.callback = write_callback;
    event_add(&ev_);
}

static void response_send_file_page(event_t *ev, char *file_name)
{
    char header[BUFFER_UNIT] = { 0 };
    FILE* fp = NULL;
    int total;
    int len;
    event_data_t* ev_data = NULL;
    event_t ev_ = {0};

    fp = fopen(file_name, "rb");
    if (!fp)
    {
        log_error("{%s:%d} open [%s] failed, errno=%d", __FUNCTION__, __LINE__, file_name, errno);
        release_event_data(ev);
        response_http_404_page(ev);
        goto end;
    }
    if (ev->data == NULL)
    {
        fseek(fp, 0, SEEK_END);
        total = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        len = total > BUFFER_UNIT ? BUFFER_UNIT : total;
        sprintf(header, response_header_format(), "200 OK", reponse_content_type(file_name), total);
        ev_data = create_event_data_fp(header, fp, len, total);
        memcpy(ev_data->file, file_name, strlen(file_name));
        ev_data->offset += len;
    }
    else
    {
        fseek(fp, ev->data->offset, SEEK_SET);
        len = ev->data->total - ev->data->offset > BUFFER_UNIT ? BUFFER_UNIT : ev->data->total - ev->data->offset;
        ev_data = create_event_data_fp(NULL, fp, len, ev->data->total);
        memcpy(ev_data->file, file_name, strlen(file_name));
        ev_data->offset = ev->data->offset + len;
        release_event_data(ev);
    }

    ev_.fd = ev->fd;
    ev_.ip = ev->ip;
    ev_.type = EV_WRITE;
    ev_.param = ev->param;
    ev_.data = ev_data;
    ev_.callback = write_callback;
    event_add(&ev_);

end:
    if (fp)
        fclose(fp);
}

static void response_upload_page(event_t *ev, int result)
{
    if (result)
    {
        send_response(ev, "Upload completed", "200 OK");
    }
    else
    {
        send_response(ev, "Upload failed", "200 OK");
    }
}

static void response_http_400_page(event_t *ev)
{
    send_response(ev, "400 Bad Request", NULL);
}

static void response_http_404_page(event_t *ev)
{
    send_response(ev, "404 Not Found", NULL);
}

static void response_http_500_page(event_t *ev)
{
    send_response(ev, "500 Internal Server Error", NULL);
}

static void response_http_501_page(event_t *ev)
{
    send_response(ev, "501 Not Implemented", NULL);
}

static void send_response(event_t *ev, char *title, char *status)
{
    char header[BUFFER_UNIT] = { 0 };
    char body[BUFFER_UNIT]   = { 0 };
    event_data_t* ev_data = NULL;
    event_t ev_ = {0};

    sprintf(body, response_body_format(), title, title);
    sprintf(header, response_header_format(), status ? status : title, reponse_content_type(NULL), strlen(body));
    ev_data = create_event_data(header, body);

    ev_.fd = ev->fd;
    ev_.ip = ev->ip;
    ev_.type = EV_WRITE;
    ev_.param = ev->param;
    ev_.data = ev_data;
    ev_.callback = write_callback;
    event_add(&ev_);
}