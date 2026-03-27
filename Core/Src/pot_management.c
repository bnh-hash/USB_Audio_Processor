/*
 * pot_management.c
 *
 *  Created on: Mar 27, 2026
 *      Author: Gökçe
 */

#include <stdint.h>
#include "pot_management.h"









static uint16_t pot_values[POT_COUNT];








uint16_t * get_pot_values()
{
	return pot_values;
}




