#ifndef BSG_MANYCORE_MEM_H
#define BSG_MANYCORE_MEM_H

/* #ifndef _BSD_SOURCE */
/* 	#define _BSD_SOURCE */
/* #endif */
/* #ifndef _XOPEN_SOURCE */
/* 	#define _XOPEN_SOURCE 500 */
/* #endif */

#ifndef COSIM
#include <bsg_manycore_features.h>
#include <bsg_manycore_driver.h>
#else
#include "bsg_manycore_features.h"
#include "bsg_manycore_driver.h"
#endif

#ifdef __cplusplus
#include <cstdint>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t eva_id_t;

typedef uint32_t eva_t;

typedef struct {
	uint32_t x;
	uint32_t y; 
	uint32_t epa;
} npa_t;

int hb_mc_copy_from_epa (uint8_t fd, hb_mc_response_packet_t *buf, uint32_t x, uint32_t y, uint32_t epa, uint32_t size); 
int hb_mc_copy_to_epa (uint8_t fd, uint32_t x, uint32_t y, uint32_t epa, uint32_t *buf, uint32_t size);
int hb_mc_eva_to_npa (eva_id_t eva_id, eva_t eva, npa_t *npa);
int hb_mc_npa_to_eva (eva_id_t eva_id, npa_t *npa, eva_t *eva);


#ifdef __cplusplus
}
#endif

#endif
