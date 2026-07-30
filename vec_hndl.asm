; Vector handlers

	.sect ".real_intvecs"
	.arm

;-------------------------------------------------------------------------------
; import reference for interrupt routines

	.ref _c_entry
	.def real_int_vecs

;-------------------------------------------------------------------------------
; interrupt vectors

real_int_vecs
        b   _c_entry
        b   tramp_undefEntry
        b   tramp_vPortSWI
        b   tramp_prefetchEntry
        b   tramp_dabort
        b   tramp_phantomInterrupt
        ldr pc,[pc,#-0x1b0]
        ldr pc,[pc,#-0x1b0]

	.ref int_vec_ptr

	; Trampolines for the above vectors.  int_vec_ptr holds the
	; vector pointer we use, we load that pointer, add the offset,
	; swap the value with the stack to restore r0, then pop into
	; the PC.

adr_int_vec_ptr .word int_vec_ptr

tramp_undefEntry
	push	{r0}
	ldr	r0, adr_int_vec_ptr
	ldr	r0, [r0]
	add	r0, r0, #4
	swp	r0, r0, [sp]
	pop	{pc}

tramp_vPortSWI
	push	{r0}
	ldr	r0, adr_int_vec_ptr
	ldr	r0, [r0]
	add	r0, r0, #8
	swp	r0, r0, [sp]
	pop	{pc}

tramp_prefetchEntry
	push	{r0}
	ldr	r0, adr_int_vec_ptr
	ldr	r0, [r0]
	add	r0, r0, #12
	swp	r0, r0, [sp]
	pop	{pc}

tramp_dabort
	push	{r0}
	ldr	r0, adr_int_vec_ptr
	ldr	r0, [r0]
	add	r0, r0, #16
	swp	r0, r0, [sp]
	pop	{pc}
	
tramp_phantomInterrupt
	push	{r0}
	ldr	r0, adr_int_vec_ptr
	ldr	r0, [r0]
	add	r0, r0, #20
	swp	r0, r0, [sp]
	pop	{pc}

	; Should not happen.
	.text
	.def vPortSWI
vPortSWI:
	b vPortSWI

	; Don't yield, we are single-threaded.
	.def vPortYieldWithinAPI
vPortYieldWithinAPI
	pop	{pc}
