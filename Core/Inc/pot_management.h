/*
 * pot_management.h
 *
 *  Created on: Mar 27, 2026
 *      Author: Gökçe
 */

#ifndef INC_POT_MANAGEMENT_H_
#define INC_POT_MANAGEMENT_H_


typedef enum{
	POT_PHASER = 0,
	POT_LPF,
	POT_HPF,
	POT_DELAY,
	POT_RING,
	POT_VOLUME,
	POT_COUNT
} pot_num_t;


uint16_t * get_pot_values();


#endif /* INC_POT_MANAGEMENT_H_ */
