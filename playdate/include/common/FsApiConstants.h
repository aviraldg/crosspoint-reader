#pragma once

#include <cstdint>

using oflag_t = uint8_t;

#ifndef O_RDONLY
#define O_RDONLY 0x00
#endif
#ifndef O_READ
#define O_READ O_RDONLY
#endif
#ifndef O_WRONLY
#define O_WRONLY 0x01
#endif
#ifndef O_WRITE
#define O_WRITE O_WRONLY
#endif
#ifndef O_RDWR
#define O_RDWR 0x02
#endif
#ifndef O_AT_END
#define O_AT_END 0x04
#endif
#ifndef O_APPEND
#define O_APPEND 0x08
#endif
#ifndef O_CREAT
#define O_CREAT 0x10
#endif
#ifndef O_TRUNC
#define O_TRUNC 0x20
#endif
#ifndef O_EXCL
#define O_EXCL 0x40
#endif
#ifndef O_SYNC
#define O_SYNC 0x80
#endif
#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif
