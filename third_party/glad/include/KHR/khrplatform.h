#ifndef KHRPLATFORM_H
#define KHRPLATFORM_H

#ifdef _WIN32
  #define KHRPLATFORM_CALL __stdcall
#else
  #define KHRPLATFORM_CALL
#endif

typedef signed char khronos_int8_t;
typedef unsigned char khronos_uint8_t;
typedef signed short int khronos_int16_t;
typedef unsigned short int khronos_uint16_t;
typedef signed int khronos_int32_t;
typedef unsigned int khronos_uint32_t;
typedef signed long long int khronos_int64_t;
typedef unsigned long long int khronos_uint64_t;
typedef signed long int khronos_intptr_t;
typedef unsigned long int khronos_uintptr_t;
typedef signed long int khronos_ssize_t;
typedef unsigned long int khronos_usize_t;
typedef float khronos_float_t;
typedef double khronos_double_t;
typedef char khronos_int8_t_unused;
typedef unsigned char khronos_uint8_t_unused;
typedef signed short int khronos_int16_t_unused;
typedef unsigned short int khronos_uint16_t_unused;
typedef signed int khronos_int32_t_unused;
typedef unsigned int khronos_uint32_t_unused;
typedef signed long long int khronos_int64_t_unused;
typedef unsigned long long int khronos_uint64_t_unused;
typedef signed long int khronos_intptr_t_unused;
typedef unsigned long int khronos_uintptr_t_unused;
typedef signed long int khronos_ssize_t_unused;
typedef unsigned long int khronos_usize_t_unused;
typedef float khronos_float_t_unused;
typedef double khronos_double_t_unused;

#endif
