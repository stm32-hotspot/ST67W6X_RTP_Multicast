/**
 *******************************************************************************
 * @file    memory.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Fast memory handling implementation.
 *******************************************************************************
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
 *******************************************************************************
 */
#include <stdint.h>
#include <string.h>

/* Private tunables ----------------------------------------------------------*/

#define ENABLE_MEMCPY
#define ENABLE_MEMSET

/* Private definitions -------------------------------------------------------*/

/** Number of bytes changed per 4x unrolled iteration */
#define MEMORY_BLOCK_BIG    (sizeof(uint32_t) << 2U)

/** Number of bytes changed per iteration */
#define MEMORY_BLOCK_SMALL  (sizeof(uint32_t))     /*!< Bytes copied per iteration */

/* Private macros ------------------------------------------------------------*/

/** Check if pointer is unaligned */
#define IS_UNALIGNED_PTR(x) \
  ((uint32_t)(x) & (sizeof(uint32_t) - 1U))

/* Private types -------------------------------------------------------------*/

/* Private data --------------------------------------------------------------*/

/* Private function ----------------------------------------------------------*/

/* Public API definitions ----------------------------------------------------*/

#ifdef ENABLE_MEMCPY
void __attribute__((optimize("-fno-tree-loop-distribute-patterns"))) *memcpy (
  void        *__restrict__ dst,
  const void  *__restrict__ src,
  size_t                    size
)
{
  uint8_t       *dst_u = dst;
  const uint8_t *src_u = src;

#ifndef MEMCPY_OPTIMIZE_OS
  /* Perform efficient copy */
  if (
    (size >= MEMORY_BLOCK_BIG) &&                       /* Sufficient data... */
    !(IS_UNALIGNED_PTR(dst) || IS_UNALIGNED_PTR(src))   /* with aligned pointers */
  )
  {
    uint32_t        *dst_a = (uint32_t*)dst;
    const uint32_t  *src_a = (const uint32_t*)src;

    /* Unrolled iteration */
    while (size >= MEMORY_BLOCK_BIG)
    {
      *dst_a++ = *src_a++;
      *dst_a++ = *src_a++;
      *dst_a++ = *src_a++;
      *dst_a++ = *src_a++;
      size    -= MEMORY_BLOCK_BIG;
    }

    /* Simple iteration */
    while (size >= MEMORY_BLOCK_SMALL)
    {
      *dst_a++ = *src_a++;
      size    -= MEMORY_BLOCK_SMALL;
    }

    /* For missing bytes, use byte copy */
    dst_u = (uint8_t*)dst_a;
    src_u = (const uint8_t*)src_a;
  }
#endif /* MEMCPY_OPTIMIZE_OS */

  /* Perform byte copy */
  while (size--)
  {
    *dst_u++ = *src_u++;
  }
  return dst;
}
#endif /* ENABLE_MEMCPY */

#ifdef ENABLE_MEMSET
void __attribute__((optimize("-fno-tree-loop-distribute-patterns"))) *memset (
  void *__restrict__ dst,
  int                value,
  size_t             size
)
{
  uint8_t *dst_u = dst;
  uint8_t val8   = (uint8_t)value;

#ifndef MEMSET_OPTIMIZE_OS
  /* Handle unaligned start */
  while (size && IS_UNALIGNED_PTR(dst_u))
  {
    *dst_u++ = val8;
    size--;
  }

  /* Perform efficient copy */
  if (size >= MEMORY_BLOCK_BIG)
  {
    uint32_t *dst_a = (uint32_t*)dst_u;
    uint32_t val32  = (uint32_t)((val8 << 24U) | (val8 << 16U) | (val8 << 8U) | val8);

    /* Unrolled iteration */
    while (size >= MEMORY_BLOCK_BIG)
    {
      *dst_a++ = val32;
      *dst_a++ = val32;
      *dst_a++ = val32;
      *dst_a++ = val32;
      size    -= MEMORY_BLOCK_BIG;
    }

    /* Simple iteration */
    while (size >= MEMORY_BLOCK_SMALL)
    {
      *dst_a++ = val32;
      size    -= MEMORY_BLOCK_SMALL;
    }

    /* For missing bytes, use byte copy */
    dst_u = (uint8_t*)dst_a;
  }
#endif /* MEMSET_OPTIMIZE_OS */

  /* Perform byte copy */
  while (size--)
  {
    *dst_u++ = val8;
  }
  return dst;
}
#endif /* ENABLE_MEMSET */

/* Private function definitions ----------------------------------------------*/
