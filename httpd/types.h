#ifndef __TYPE_S_H_
#define __TYPE_S_H_

typedef char                int8_t;
typedef short               int16_t;
typedef long                int32_t;
typedef long long           int64_t;
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned long       uint32_t;
typedef unsigned long long  uint64_t;


typedef enum
{
    SUCC,
    FAIL,
    PARA,
    FULL,
    EXIS,
    NEXI,
    DISC
} ret_code_t;

#endif