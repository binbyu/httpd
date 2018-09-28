#include "httpd.h"

wchar_t* ansi_to_unicode(char* str)
{
    wchar_t* result;
    int len = MultiByteToWideChar(CP_ACP, 0, str, -1, NULL, 0);
    result = (wchar_t*)malloc((len+1)*sizeof(wchar_t));
    memset(result, 0, (len+1)*sizeof(wchar_t));
    MultiByteToWideChar(CP_ACP, 0, str, -1, (LPWSTR)result, len);
    return result;
}

char* unicode_to_ansi(wchar_t* str)
{
    char* result;
    int len = WideCharToMultiByte(CP_ACP, 0, str, -1, NULL, 0, NULL, NULL);
    result = (char*)malloc((len+1)*sizeof(char));
    memset(result, 0, (len+1)*sizeof(char));
    WideCharToMultiByte(CP_ACP, 0, str, -1, result, len, NULL, NULL);
    return result;
}

wchar_t* utf8_to_unicode(char* str)
{
    wchar_t* result;
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    result = (wchar_t*)malloc((len+1)*sizeof(wchar_t));
    memset(result, 0, (len+1)*sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, str, -1, (LPWSTR)result, len);
    return result;
}

char* unicode_to_utf8(wchar_t* str)
{
    char* result;
    int len = WideCharToMultiByte(CP_UTF8, 0, str, -1, NULL, 0, NULL, NULL);
    result =(char*)malloc((len+1)*sizeof(char));
    memset(result, 0, (len+1)*sizeof(char));
    WideCharToMultiByte(CP_UTF8, 0, str, -1, result, len, NULL, NULL);
    return result;
}

char* utf8_to_ansi(char* str)
{
    wchar_t* uni = utf8_to_unicode(str);
    char* ansi = unicode_to_ansi(uni);
    free(uni);
    return ansi;
}

char* ansi_to_utf8(char* str)
{
    wchar_t* uni = ansi_to_unicode(str);
    char* utf8 = unicode_to_utf8(uni);
    free(uni);
    return utf8;
}

int file_exist(char *file_name)
{
    FILE* fp = NULL;
    fp = fopen(file_name, "rb");
    if (fp)
    {
        fclose(fp);
    }
    return fp != NULL;
}

int remove_file(char *file_name)
{
    return DeleteFileA(file_name);
}

char* file_ext(char* file_name)
{
    char *ext = NULL;
    int i;

    for (i=strlen(file_name)-1; i>=0; i--)
    {
        if (file_name[i] == '.')
        {
            ext = file_name+i+1;
            break;
        }
    }
    return ext;
}

char* root_path()
{
    static char root[MAX_PATH] = {0};
    uint32_t i;

    if (!root[0])
    {
        GetCurrentDirectoryA(MAX_PATH, root);
        for (i=0; i<strlen(root); i++)
        {
            if (root[i] == '\\')
            {
                root[i] = '/';
            }
        }
        if (root[strlen(root)-1] != '/')
        {
            root[strlen(root)] = '/';
        }
    }
    return root;
}

char* uint32_to_str(uint32_t n)
{
    static char buf[16] = {0};
    memset(buf, 0, sizeof(buf));
    _itoa(n, buf, 10);
    return buf;
}