/* Copyright (c) 2002, 2003, 2004  Marek Michalkiewicz
   Copyright (c) 2005, 2007 Joerg Wunsch
   All rights reserved.
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:
   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following disclaimer in
     the documentation and/or other materials provided with the
     distribution.
   * Neither the name of the copyright holders nor the names of
     contributors may be used to endorse or promote products derived
     from this software without specific prior written permission.
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE. */

//  Port to Energia / MPS430 by Yannick DEVOS XV4Y - (c) 2013
//  http://xv4y.radioclub.asia/
//  

/* $Id: crc16.h 2136 2010-06-08 12:03:38Z joerg_wunsch $ */

#ifndef _UTIL_CRC16_H_
#define _UTIL_CRC16_H_

#include <stdint.h>

/* Only crc16_update() (Modbus CRC-16, poly 0xA001) is used by this project - the
 * other three functions this header was copied with (crc_xmodem_update,
 * _crc_ccitt_update, _crc_ibutton_update) were unused, along with the lo8/hi8
 * helpers that only _crc_ccitt_update referenced. */
uint16_t crc16_update(uint16_t crc, uint8_t a)
    {
  int i;

  crc ^= a;
  for (i = 0; i < 8; ++i)
  {
      if (crc & 1)
    crc = (crc >> 1) ^ 0xA001;
      else
    crc = (crc >> 1);
  }

  return crc;
    }


#endif /* _UTIL_CRC16_H_ */
