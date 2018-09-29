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

static int  read_request_header(event_t *ev, char **buf, int *size);
static int  parse_request_header(char *data, request_header_t *header);
static void release_request_header(request_header_t *header);
static void release_event(event_t *ev);
static event_data_t *create_event_data(const char *header, const char *html);
static event_data_t *create_event_data_fp(const char *header, FILE *fp, int read_len, int total_len);
static void release_event_data(event_t *ev);
static void uri_decode(char* uri);
static uint8_t ishex(uint8_t x);
static char* local_file_list(char *path);

static const char *reponse_content_type(char *file_name);
static const char *response_header_format();
static void response_home_page(event_t *ev, char *path);
static void response_upload_page(event_t *ev, int result);
static void response_send_file_page(event_t *ev, char *file_name);
static void response_http_400_page(event_t *ev);
static void response_http_404_page(event_t *ev);
static void response_http_501_page(event_t *ev);

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
    int size;
    request_header_t header;
    int i;
    int content_length = 0;
    int boundary_length = 0;
    char *temp = NULL, *p;
    char file_name[MAX_PATH] = {0};
    int len, ret, write_len;
    char buffer[BUFFER_UNIT+1] = {0};
    event_data_t* ev_data = NULL;
    FILE* fp = NULL;

    if (ev->status == evIdle)
    {
        if (SUCC != read_request_header(ev, &buf, &size))
        {
            response_http_400_page(ev);
            free(buf);
            return;
        }
        if (!buf)
            return;
        //log_debug("%s", buf);
        parse_request_header(buf, &header);
        uri_decode(header.uri);
        header.uri = utf8_to_ansi(header.uri);
        log_debug("{%s:%d} >>> Entry recv ... uri=%s", __FUNCTION__, __LINE__, header.uri);
        if (strcmp(header.method, "GET") && strcmp(header.method, "POST"))
        {
            // 501 Not Implemented
            response_http_501_page(ev);
            free(header.uri);
            free(buf);
            release_request_header(&header);
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
            memset(file_name, 0, sizeof(file_name));
            memcpy(file_name, root_path(), strlen(root_path()));
            if (strlen(header.uri) > strlen("/upload?path="))
            {
                memcpy(file_name+strlen(file_name), header.uri+strlen("/upload?path="), strlen(header.uri)-strlen("/upload?path="));
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
                    free(header.uri);
                    free(buf);
                    release_request_header(&header);
                    return;
                }

                // get boundary_length
                for (i=0; i<header.fields_count; i++)
                {
                    if (0 == strcmp(header.fields[i].key, "Content-Type"))
                    {
                        temp = strstr(header.fields[i].value, "boundary=");
                        if (temp)
                        {
                            temp += strlen("boundary=");
                            boundary_length = strlen(temp);
                        }
                        break;
                    }
                }
                if (boundary_length == 0)
                {
                    // not support
                    // 501 Not Implemented
                    response_http_501_page(ev);
                    free(header.uri);
                    free(buf);
                    release_request_header(&header);
                    return;
                }
				
                // read boundary
                free(header.uri);
                free(buf);buf=NULL;
                release_request_header(&header);
                if (SUCC != read_request_header(ev, &buf, &size))
                {
                    response_http_400_page(ev);
                    return;
                }
                //log_debug("%s", buf);
                len = strlen(buf);

                // get upload filename
                temp = strstr(buf, "filename=\"");
                if (!temp)
                {
                    response_http_400_page(ev);
                    free(buf);
                    return;
                }
                temp += strlen("filename=\"");
                *strstr(temp, "\"") = 0;
                // IE browser: remove client file path
                p = temp;
                while (*p)
                {
                    if (*p == '\\' || *p == '/')
                        if (*(p+1))
                            temp = p+1;
                    p++;
                }
                memcpy(file_name+strlen(file_name), temp, strlen(temp));
                log_error("{%s:%d} Upload file name = %s", __FUNCTION__, __LINE__, file_name);

                // if file exist delete file
                if (file_exist(file_name))
                {
                    remove_file(file_name);
                }

                // begin read file
                ev_data = (event_data_t*)malloc(sizeof(event_data_t));
                memset(ev_data, 0, sizeof(event_data_t));
                memcpy(ev_data->file, file_name, strlen(file_name));
                ev_data->total = content_length;
                ev_data->offset = len;
                ev_data->tail = boundary_length + strlen(CRLF)*3+strlen("--");
                ASSERT(content_length > len);
                ev->status = evBusy;
                ev->data = ev_data;
                free(buf);
                buf = NULL;
                return;
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
            free(header.uri);
            free(buf);
            release_request_header(&header);
            return;
        }
        else
        {
            // send file
            memset(file_name, 0, sizeof(file_name));
            memcpy(file_name, root_path(), strlen(root_path()));
            memcpy(file_name+strlen(file_name), header.uri+1, strlen(header.uri+1));
            response_send_file_page(ev, file_name);
            free(buf);
            release_request_header(&header);
            return;
        }
    }
    else
    {
        len = ev->data->total - ev->data->offset > BUFFER_UNIT ? BUFFER_UNIT : ev->data->total - ev->data->offset;
        ret = network_read(ev->fd, buffer, len);
        if (ret == DISC)
        {
            release_event(ev);
            return;
        }
        else if (ret == SUCC)
        {
            write_len = ev->data->total - ev->data->offset - len > ev->data->tail ? len : ev->data->total - ev->data->offset - ev->data->tail;
            if (write_len > 0)
            {
                fp = ev->data->fp;
                if (!fp)
                {
                    fp = fopen(ev->data->file, "ab");
                    if (!fp)
                    {
                        log_error("{%s:%d} open file fail. filename=%s, socket=%d, errno=%d", __FUNCTION__, __LINE__, ev->data->file, ev->fd, errno);
                        release_event_data(ev);
                        ev->status = evIdle;
                        response_upload_page(ev, 0);
                        return;
                    }
                    ev->data->fp = fp;
                }                
                if (write_len != fwrite(buffer, 1, write_len, fp))
                {
                    log_error("{%s:%d} write file fail. filename=%s, socket=%d, errno=%d", __FUNCTION__, __LINE__, ev->data->file, ev->fd, errno);
                    release_event_data(ev);
                    ev->status = evIdle;
                    response_upload_page(ev, 0);
                    return;
                }
                //fclose(fp); fixed fopen EACCES error 
            }

            ev->data->offset += len;
            log_debug("{%s:%d} upload file progress=%d%%. socket=%d", __FUNCTION__, __LINE__,ev->data->offset*100/ev->data->total, ev->fd);
            if (ev->data->offset == ev->data->total)
            {
                release_event_data(ev);
                ev->status = evIdle;
                response_upload_page(ev, 1);
                log_info("{%s:%d} upload file complete. socket=%d", __FUNCTION__, __LINE__, ev->fd);
            }
            return;
        }
        else
        {
            log_error("{%s:%d} recv unknown fail.", __FUNCTION__, __LINE__);
            release_event_data(ev);
            ev->status = evIdle;
            response_upload_page(ev, 0);
            return;
        }
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
            sprintf(line, format_dir, FindFileData.cFileName, FindFileData.cFileName);
            line_length = strlen(line);
            line[line_length] = 0;
            if (offset+line_length > size-1)
            {
                size += BUFFER_UNIT;
                result = (char*)realloc(result, size);
            }
            memcpy(result+offset, line, line_length);
            offset += line_length;
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
            sprintf(line, format_file, FindFileData.cFileName, FindFileData.cFileName);
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

static void response_home_page(event_t *ev, char *path)
{
    const char *html_format = 
        "<html>" CRLF
        "<head><title>Index of /%s</title></head>" CRLF
        "<body bgcolor=\"white\">" CRLF
        "<h1>Index of /%s</h1><hr>" CRLF
        "<form action=\"/upload?path=%s\" method=\"post\" enctype=\"multipart/form-data\">" CRLF
        "<input type=\"file\" name=\"file\" />" CRLF
        "<input type=\"submit\" value=\"Upload\" /></form><hr><pre>" CRLF
        "%s" CRLF
        "</body></html>";

    char header[BUFFER_UNIT] = { 0 };
    event_data_t* ev_data = NULL;
    int length;
    char *file_list = NULL;
    char *html = NULL;
    event_t ev_ = {0};

    file_list = local_file_list(path);
    if (!file_list)
        return;

    length = strlen(html_format) + strlen(file_list) + (strlen(path) - strlen("%s"))*3 + 1;
    html = (char*)malloc(length);
    if (!html)
    {
        log_error("{%s:%d} malloc fail.", __FUNCTION__, __LINE__);
        return;
    }
    sprintf(html, html_format, path, path, path, file_list);
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

static void response_upload_page(event_t *ev, int result)
{
    const char *html_succ = 
        "<html>" CRLF
        "<head><title>Upload completed</title></head>" CRLF
        "<body bgcolor=\"white\">" CRLF
        "<center><h1>Upload completed!</h1></center>" CRLF
        "</body></html>";

    const char *html_fail = 
        "<html>" CRLF
        "<head><title>Upload failed</title></head>" CRLF
        "<body bgcolor=\"white\">" CRLF
        "<center><h1>Upload failed!</h1></center>" CRLF
        "</body></html>";

    char header[BUFFER_UNIT] = { 0 };
    int len = result ? strlen(html_succ) : strlen(html_fail);
    event_data_t* ev_data = NULL;
    event_t ev_ = {0};

    sprintf(header, response_header_format(), "200 OK", reponse_content_type(NULL), len);
    ev_data = create_event_data(header, result ? html_succ : html_fail);

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

static void response_http_400_page(event_t *ev)
{
    const char *http_error_400_page =
        "<html>" CRLF
        "<head><title>400 Bad Request</title></head>" CRLF
        "<body bgcolor=\"white\">" CRLF
        "<center><h1>400 Bad Request</h1></center>" CRLF
        "</body></html>";

    char header[BUFFER_UNIT] = { 0 };
    event_data_t* ev_data = NULL;
    int len;
    event_t ev_ = {0};

    len = strlen(http_error_400_page);
    sprintf(header, response_header_format(), "400 Bad Request", reponse_content_type(NULL), len);
    ev_data = create_event_data(header, http_error_400_page);

    ev_.fd = ev->fd;
    ev_.ip = ev->ip;
    ev_.type = EV_WRITE;
    ev_.param = ev->param;
    ev_.data = ev_data;
    ev_.callback = write_callback;
    event_add(&ev_);
}

static void response_http_404_page(event_t *ev)
{
    static char *http_error_404_page =
        "<html>" CRLF
        "<head><title>404 Not Found</title></head>" CRLF
        "<body bgcolor=\"white\">" CRLF
        "<center><h1>404 Not Found</h1></center>" CRLF
        "</body></html>";

    char header[BUFFER_UNIT] = { 0 };
    event_data_t* ev_data = NULL;
    int len;
    event_t ev_ = {0};

    len = strlen(http_error_404_page);
    sprintf(header, response_header_format(), "404 Not Found", reponse_content_type(NULL), len);
    ev_data = create_event_data(header, http_error_404_page);

    ev_.fd = ev->fd;
    ev_.ip = ev->ip;
    ev_.type = EV_WRITE;
    ev_.param = ev->param;
    ev_.data = ev_data;
    ev_.callback = write_callback;
    event_add(&ev_);
}

static void response_http_501_page(event_t *ev)
{
    const char *http_error_501_page =
        "<html>" CRLF
        "<head><title>501 Not Implemented</title></head>" CRLF
        "<body bgcolor=\"white\">" CRLF
        "<center><h1>501 Not Implemented</h1></center>" CRLF
        "</body></html>";

    char header[BUFFER_UNIT] = { 0 };
    event_data_t* ev_data = NULL;
    int len;
    event_t ev_ = {0};

    len = strlen(http_error_501_page);
    sprintf(header, response_header_format(), "501 Not Implemented", reponse_content_type(NULL), len);
    ev_data = create_event_data(header, http_error_501_page);

    ev_.fd = ev->fd;
    ev_.ip = ev->ip;
    ev_.type = EV_WRITE;
    ev_.param = ev->param;
    ev_.data = ev_data;
    ev_.callback = write_callback;
    event_add(&ev_);
}