/*
 * hamlib - (C) Frank Singleton 2000 (vk3fcs@ix.netcom.com)
 *
 * kenwood.c - Copyright (C) 2000 Stephane Fillod
 * This shared library provides an API for communicating
 * via serial interface to a Kenwood radio.
 *
 *
 * $Id: kenwood.c,v 1.1 2000-12-23 08:40:14 f4cfe Exp $  
 *
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 * 
 */

#include <stdlib.h>
#include <stdio.h>   /* Standard input/output definitions */
#include <string.h>  /* String function definitions */
#include <unistd.h>  /* UNIX standard function definitions */
#include <fcntl.h>   /* File control definitions */
#include <errno.h>   /* Error number definitions */
#include <termios.h> /* POSIX terminal control definitions */
#include <sys/ioctl.h>
#include <math.h>

#include <hamlib/rig.h>
#include <hamlib/riglist.h>
#include <serial.h>
#include <misc.h>
#include "kenwood.h"


/*
 * modes in use by the "MD" command
 */
#define MD_LSB	'1'
#define MD_USB	'2'
#define MD_CW	'3'
#define MD_FM	'4'

/*
 * kenwood_transaction
 * We assume that rig!=NULL, rig->state!= NULL, data!=NULL, data_len!=NULL
 * Otherwise, you'll get a nice seg fault. You've been warned!
 * TODO: error case handling
 */
int kenwood_transaction(RIG *rig, const char *cmd, int cmd_len, char *data, int *data_len)
{
	int i;
	struct rig_state *rig_s;

	rig_s = &rig->state;

	write_block(rig_s->fd, cmd, cmd_len, rig_s->write_delay, rig_s->post_write_delay);

	/*
	 * buffered read are quite helpful here!
	 * However, an automate with a state model would be more efficient..
	 */
	i = 0;
	do {
		fread_block(rig_s->stream, data+i, 1, rig_s->timeout);
	} while (data[i++] != ';');

	*data_len = i;

	return i;
}

/*
 * kenwood_set_freq
 * Assumes rig!=NULL
 */
int kenwood_set_freq(RIG *rig, vfo_t vfo, freq_t freq)
{
		unsigned char freqbuf[16], ackbuf[16];
		int freq_len,ack_len;
		char vfo_letter;

			/*
			 * FIXME: vfo==RIG_VFO_CURR
			 */
		vfo_letter = vfo==RIG_VFO_A? 'A':'B';
		freq_len = sprintf(freqbuf,"F%c%011Ld;", vfo_letter, freq);

		kenwood_transaction (rig, freqbuf, freq_len, ackbuf, &ack_len);

		if (ack_len != 1) {
				rig_debug(RIG_DEBUG_ERR,"kenwood_set_freq: ack NG, len=%d\n",
								ack_len);
				return -RIG_ERJCTED;
		}

		return RIG_OK;
}

/*
 * kenwood_get_freq
 * Assumes rig!=NULL, freq!=NULL
 */
int kenwood_get_freq(RIG *rig, vfo_t vfo, freq_t *freq)
{
		unsigned char freqbuf[16];
		unsigned char cmdbuf[4];
		int freq_len;

			/*
			 * FIXME: vfo==RIG_VFO_CURR
			 */
		cmdbuf[0] = 'F';
		cmdbuf[1] = vfo==RIG_VFO_A? 'A':'B';
		cmdbuf[2] = ';';

		kenwood_transaction (rig, cmdbuf, 3, freqbuf, &freq_len);

		sscanf(freqbuf,"%Ld", freq);

		return RIG_OK;
}

/*
 * kenwood_set_mode
 * Assumes rig!=NULL
 */
int kenwood_set_mode(RIG *rig, vfo_t vfo, rmode_t mode, pbwidth_t width)
{
		unsigned char mdbuf[16],ackbuf[16];
		int mdbuf_len,ack_len,kmode;

		switch (mode) {
			case RIG_MODE_CW:       kmode = MD_CW; break;
			case RIG_MODE_USB:      kmode = MD_USB; break;
			case RIG_MODE_LSB:      kmode = MD_LSB; break;
			case RIG_MODE_FM:       kmode = MD_FM; break;
			default:
				rig_debug(RIG_DEBUG_ERR,"kenwood_set_mode: unsupported mode %d\n",
								mode);
				return -RIG_EINVAL;
		}

		mdbuf_len = sprintf(mdbuf, "MD%c;", kmode);
		kenwood_transaction (rig, mdbuf, mdbuf_len, ackbuf, &ack_len);

		if (ack_len != 1) {
				rig_debug(RIG_DEBUG_ERR,"kenwood_set_mode: ack NG, len=%d\n",
								ack_len);
				return -RIG_ERJCTED;
		}

		return RIG_OK;
}

/*
 * kenwood_get_mode
 * Assumes rig!=NULL, mode!=NULL
 */
int kenwood_get_mode(RIG *rig, vfo_t vfo, rmode_t *mode, pbwidth_t *width)
{
		unsigned char ackbuf[16];
		int ack_len;


		kenwood_transaction (rig, "MD;", 2, ackbuf, &ack_len);

		if (ack_len != 2) {
				rig_debug(RIG_DEBUG_ERR,"kenwood_get_mode: ack NG, len=%d\n",
								ack_len);
				return -RIG_ERJCTED;
		}

		*width = RIG_PASSBAND_NORMAL;
		switch (ackbuf[0]) {
			case MD_CW:		*mode = RIG_MODE_CW; break;
			case MD_USB:	*mode = RIG_MODE_USB; break;
			case MD_LSB:	*mode = RIG_MODE_LSB; break;
			case MD_FM:		*mode = RIG_MODE_FM; break;
			default:
				rig_debug(RIG_DEBUG_ERR,"kenwood_get_mode: unsupported mode %d\n",
								ackbuf[0]);
				return -RIG_EINVAL;
		}

		return RIG_OK;
}

/*
 * init_kenwood is called by rig_backend_load
 */
int init_kenwood(void *be_handle)
{
		rig_debug(RIG_DEBUG_VERBOSE, "kenwood: _init called\n");

		rig_register(&ts870s_caps);

		return RIG_OK;
}


