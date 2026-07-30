/*----------------------------------------------------------------------------*/
/* sys_link.cmd                                                               */
/*                                                                            */
/* 
* Copyright (C) 2009-2018 Texas Instruments Incorporated - www.ti.com  
* 
* 
*  Redistribution and use in source and binary forms, with or without 
*  modification, are permitted provided that the following conditions 
*  are met:
*
*    Redistributions of source code must retain the above copyright 
*    notice, this list of conditions and the following disclaimer.
*
*    Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the 
*    documentation and/or other materials provided with the   
*    distribution.
*
*    Neither the name of Texas Instruments Incorporated nor the names of
*    its contributors may be used to endorse or promote products derived
*    from this software without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
*  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
*  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
*  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
*  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*/

/*----------------------------------------------------------------------------*/
/* Linker Settings                                                            */

--retain="*(.real_intvecs)"

/*----------------------------------------------------------------------------*/
/* Memory Map                                                                 */

MEMORY
{
    VECTORS  (X)  : origin=0x00000000 length=0x00000100
    FLASH0   (RX) : origin=0x00000100 length=0x0000ff00

    /* Stack is set in sys_core.asm and must be here. */
    STACKS   (RW) : origin=0x08000000 length=0x00001500

    /* Pointer to vector table, used by bootstrap to forward exceptions. */
    RAMVEC   (RW) : origin=0x08001500 length=0x00000004

    /* Area of memory saved across reset.  Used in sys_startup.c */
    SAVEAREA (RW) : origin=0x08001504 length=0x000000fc

    RAM      (RW) : origin=0x08001600 length=0x0001ea00
}

/*----------------------------------------------------------------------------*/
/* Section Configuration                                                      */

SECTIONS
{
    .real_intvecs : {} > VECTORS
    .intvecs : {} > FLASH0
    .text    : {} > FLASH0 
    .const   : {} > FLASH0 
    .cinit   : {} > FLASH0 
    .pinit   : {} > FLASH0
    .vecptr  : {} > RAMVEC
    .savearea: {} > SAVEAREA
    .bss     : {} > RAM
    .data    : {} > RAM
    .sysmem  : {} > RAM
}
