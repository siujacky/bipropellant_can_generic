	.cpu cortex-m3
	.arch armv7-m
	.fpu softvfp
	.eabi_attribute 20, 1
	.eabi_attribute 21, 1
	.eabi_attribute 23, 3
	.eabi_attribute 24, 1
	.eabi_attribute 25, 1
	.eabi_attribute 26, 1
	.eabi_attribute 30, 2
	.eabi_attribute 34, 1
	.eabi_attribute 18, 4
	.file	"main.c"
	.text
	.align	1
	.p2align 2,,3
	.syntax unified
	.thumb
	.thumb_func
	.type	print_uint32, %function
print_uint32:
	@ args = 0, pretend = 0, frame = 16
	@ frame_needed = 0, uses_anonymous_args = 0
	cbz	r0, .L10
	push	{r4, r5, r6, lr}
	sub	sp, sp, #16
	add	r5, sp, #4
	mov	r1, r5
	movs	r2, #0
	ldr	r6, .L11
.L3:
	mov	ip, r0
	umull	r4, r3, r6, r0
	lsrs	r3, r3, #3
	add	lr, r3, r3, lsl #2
	sub	r0, r0, lr, lsl #1
	adds	r0, r0, #48
	cmp	ip, #9
	strb	r0, [r1], #1
	add	r2, r2, #1
	mov	r0, r3
	bhi	.L3
	mov	r4, r2
	add	r4, r4, r5
.L4:
	ldrb	r0, [r4, #-1]!	@ zero_extendqisi2
	bl	usart1_tx_byte
	cmp	r4, r5
	bne	.L4
	add	sp, sp, #16
	@ sp needed
	pop	{r4, r5, r6, pc}
.L10:
	movs	r0, #48
	b	usart1_tx_byte
.L12:
	.align	2
.L11:
	.word	-858993459
	.size	print_uint32, .-print_uint32
	.section	.rodata.str1.4,"aMS",%progbits,1
	.align	2
.LC0:
	.ascii	"CAN\000"
	.align	2
.LC1:
	.ascii	"UART\000"
	.align	2
.LC2:
	.ascii	"open\000"
	.align	2
.LC3:
	.ascii	"closed\000"
	.align	2
.LC4:
	.ascii	"?\000"
	.align	2
.LC5:
	.ascii	"\015\012Blue Pill CAN Tool v1\015\012\000"
	.align	2
.LC6:
	.ascii	"Pins: CAN=PA4-PA7(SPI1)+PB0(INT)  1wire=PB10(USART3"
	.ascii	")  Serial=PA9/PA10\015\012\000"
	.align	2
.LC7:
	.ascii	"250kbps (ODrive default). Auto-open in 2s if no ser"
	.ascii	"ial command.\015\012\000"
	.align	2
.LC8:
	.ascii	"SLCAN: O=open C=close S6=500k t/T=tx r=rtr F=flags "
	.ascii	"s=stat\015\012\000"
	.align	2
.LC9:
	.ascii	">\000"
	.align	2
.LC10:
	.ascii	"\015\012[AUTO] Bus open at 250kbps \342\200\224 LED"
	.ascii	" blinks on RX\015\012>\000"
	.align	2
.LC11:
	.ascii	"\015\012UART bridge mode \000"
	.align	2
.LC12:
	.ascii	" baud on PB10. Send 'mode can\\r' to exit.\015\012\000"
	.align	2
.LC13:
	.ascii	"mode can\015\000"
	.align	2
.LC14:
	.ascii	"\015\012[warn: no HDSEL echo \342\200\224 check PB1"
	.ascii	"0 pull-up]\015\012\000"
	.align	2
.LC15:
	.ascii	"\015\012[warn: HDSEL echo mismatch \342\200\224 bus"
	.ascii	" collision?]\015\012\000"
	.align	2
.LC16:
	.ascii	"\015\012Returned to CAN mode.\015\012>\000"
	.align	2
.LC17:
	.ascii	"F\000"
	.align	2
.LC18:
	.ascii	"\015\012\000"
	.align	2
.LC19:
	.ascii	"\015\012Mode: \000"
	.align	2
.LC20:
	.ascii	"  Bus: \000"
	.align	2
.LC21:
	.ascii	"  Brate: S\000"
	.align	2
.LC22:
	.ascii	"  TX:\000"
	.align	2
.LC23:
	.ascii	" RX:\000"
	.align	2
.LC24:
	.ascii	"\015\012SLCAN Commands:\015\012\000"
	.align	2
.LC25:
	.ascii	"  O        Open CAN bus (normal mode)\015\012\000"
	.align	2
.LC26:
	.ascii	"  C        Close CAN bus (listen-only)\015\012\000"
	.align	2
.LC27:
	.ascii	"  S<n>     Bitrate: S4=125k S5=250k S6=500k S7=800k"
	.ascii	" S8=1M\015\012\000"
	.align	2
.LC28:
	.ascii	"  t<id3><dlc><data>  TX standard frame\015\012\000"
	.align	2
.LC29:
	.ascii	"  T<id8><dlc><data>  TX extended frame\015\012\000"
	.align	2
.LC30:
	.ascii	"  r<id3><dlc>        TX RTR standard frame\015\012\000"
	.align	2
.LC31:
	.ascii	"  F        Read+clear error flags (hex byte)\015\012"
	.ascii	"\000"
	.align	2
.LC32:
	.ascii	"  s or i   Status (mode/bitrate/counts)\015\012\000"
	.align	2
.LC33:
	.ascii	"  mode uart <baud>   Switch to UART bridge on PB10\015"
	.ascii	"\012\000"
	.align	2
.LC34:
	.ascii	"  mode can           Return from UART bridge\015\012"
	.ascii	"\000"
	.align	2
.LC35:
	.ascii	"  h or ?   This help\015\012\000"
	.align	2
.LC36:
	.ascii	"Responses: z=tx ok, Z=ext tx ok, \\x07=error\015\012"
	.ascii	"\000"
	.section	.text.startup,"ax",%progbits
	.align	1
	.p2align 2,,3
	.global	main
	.syntax unified
	.thumb
	.thumb_func
	.type	main, %function
main:
	@ args = 0, pretend = 0, frame = 56
	@ frame_needed = 0, uses_anonymous_args = 0
	push	{r4, r5, r6, r7, r8, r9, r10, fp, lr}
	mov	r5, #8192
	movs	r7, #0
	ldr	r1, .L104
	ldr	r3, .L104+4
	ldr	r2, [r1, #24]
	ldr	r4, .L104+8
	orr	r2, r2, #16384
	orr	r2, r2, #28
	str	r2, [r1, #24]
	ldr	r2, [r1, #24]
	sub	sp, sp, #68
	orr	r2, r2, #16
	str	r2, [r1, #24]
	ldr	r2, [r3, #4]
	mov	r0, #115200
	bic	r2, r2, #15728640
	str	r2, [r3, #4]
	ldr	r2, [r3, #4]
	movw	r6, #2667
	orr	r2, r2, #3145728
	str	r2, [r3, #4]
	str	r5, [r3, #16]
	strb	r7, [r4]
	bl	usart1_init
	movs	r0, #5
	bl	mcp2515_init
	movs	r3, #5
	mov	r5, #2000
	ldr	r2, .L104+12
	ldr	r0, .L104+16
	strb	r3, [r2]
	strb	r7, [r4, #1]
	strb	r7, [r4, #2]
	bl	usart1_print
	ldr	r0, .L104+20
	bl	usart1_print
	ldr	r0, .L104+24
	bl	usart1_print
	ldr	r0, .L104+28
	bl	usart1_print
	ldr	r0, .L104+32
	bl	usart1_print
	str	r7, [r4, #4]
.L17:
	str	r6, [sp, #12]
.L14:
	ldr	r3, [sp, #12]
	subs	r2, r3, #1
	str	r2, [sp, #12]
	cmp	r3, #0
	bne	.L14
	bl	usart1_getchar_nb
	cmp	r0, #0
	bge	.L99
	subs	r5, r5, #1
	bne	.L17
	bl	mcp2515_enter_normal
	cbnz	r0, .L67
	movs	r3, #1
	ldr	r0, .L104+36
	strb	r3, [r4, #1]
	bl	usart1_print
	b	.L67
.L99:
	uxtb	r5, r0
	mov	r0, r5
	bl	usart1_tx_byte
	ldr	r3, [r4, #4]
	cmp	r3, #38
	bgt	.L67
	adds	r2, r3, #1
	add	r3, r3, r4
	str	r2, [r4, #4]
	strb	r5, [r3, #8]
.L67:
	ldr	r8, .L104+40
	add	r5, sp, #32
	add	r6, r8, #9
	sub	fp, r8, #43
.L18:
	ldrb	r7, [r4, #2]	@ zero_extendqisi2
	cbnz	r7, .L33
.L19:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cbnz	r3, .L21
.L32:
	bl	usart1_getchar_nb
	cmp	r0, #0
	blt	.L18
	uxtb	r0, r0
	cmp	r0, #13
	ldr	r3, [r4, #4]
	beq	.L34
	cmp	r0, #10
	beq	.L34
	cmp	r0, #8
	beq	.L65
	cmp	r0, #127
	beq	.L65
	cmp	r3, #38
	bgt	.L18
	ldrb	r7, [r4, #2]	@ zero_extendqisi2
	adds	r2, r3, #1
	add	r3, r3, r4
	str	r2, [r4, #4]
	strb	r0, [r3, #8]
	cmp	r7, #0
	beq	.L19
.L33:
.L20:
	b	.L20
.L21:
	bl	mcp2515_rx_available
	cbz	r0, .L97
	add	r3, sp, #20
	add	r2, sp, #11
	add	r1, sp, #24
	add	r0, sp, #16
	strd	r7, r7, [sp, #24]
	str	r7, [sp, #16]
	strb	r7, [sp, #11]
	str	r7, [sp, #20]
	bl	mcp2515_rx
	cbz	r0, .L26
	movw	r3, #2047
	ldr	r0, [sp, #16]
	ldrb	r2, [sp, #11]	@ zero_extendqisi2
	cmp	r0, r3
	bne	.L27
	cmp	r2, #2
	bls	.L27
	ldrb	r3, [sp, #24]	@ zero_extendqisi2
	cmp	r3, #176
	beq	.L100
.L27:
	add	r3, sp, #24
	ldr	r1, [sp, #20]
	str	r5, [sp]
	bl	slcan_format_rx
	mov	r0, r5
	bl	usart1_print
	ldrb	r3, [r4]	@ zero_extendqisi2
	cbz	r3, .L30
	mov	r3, #8192
	ldr	r2, .L104+4
	str	r3, [r2, #16]
.L31:
	ldr	r3, [r4, #48]
	strb	r7, [r4]
	adds	r3, r3, #1
	str	r3, [r4, #48]
.L26:
	bl	mcp2515_read_errors
.L97:
	ldrb	r3, [r4, #2]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L32
	b	.L20
.L30:
	mov	r3, #8192
	ldr	r2, .L104+4
	movs	r7, #1
	str	r3, [r2, #20]
	b	.L31
.L34:
	cmp	r3, #0
	ble	.L18
	movs	r7, #0
	add	r3, r3, r4
	mov	r1, r5
	mov	r0, fp
	strb	r7, [r3, #8]
	bl	slcan_parse_cmd
	cmp	r0, #8
	beq	.L101
	cmp	r0, #9
	bhi	.L47
	tbh	[pc, r0, lsl #1]
.L49:
	.2byte	(.L57-.L49)/2
	.2byte	(.L56-.L49)/2
	.2byte	(.L55-.L49)/2
	.2byte	(.L54-.L49)/2
	.2byte	(.L53-.L49)/2
	.2byte	(.L52-.L49)/2
	.2byte	(.L51-.L49)/2
	.2byte	(.L50-.L49)/2
	.2byte	(.L47-.L49)/2
	.2byte	(.L48-.L49)/2
	.p2align 1
.L50:
	ldr	r0, .L104+44
	bl	usart1_print
	ldr	r0, .L104+48
	bl	usart1_print
	ldr	r0, .L104+52
	bl	usart1_print
	ldr	r0, .L104+56
	bl	usart1_print
	ldr	r0, .L104+60
	bl	usart1_print
	ldr	r0, .L104+64
	bl	usart1_print
	ldr	r0, .L104+68
	bl	usart1_print
	ldr	r0, .L104+72
	bl	usart1_print
	ldr	r0, .L104+76
	bl	usart1_print
	ldr	r0, .L104+80
	bl	usart1_print
	ldr	r0, .L104+84
	bl	usart1_print
	ldr	r0, .L104+88
	bl	usart1_print
	ldr	r0, .L104+92
	bl	usart1_print
.L59:
	ldr	r0, .L104+32
	bl	usart1_print
.L48:
	movs	r3, #0
	str	r3, [r4, #4]
	b	.L18
.L51:
	bl	mcp2515_read_errors
	mov	r7, r0
	ldr	r0, .L104+96
	bl	usart1_print
	ubfx	r0, r7, #4, #8
	bl	nibble_to_hex
	bl	usart1_tx_byte
	mov	r0, r7
	bl	nibble_to_hex
	bl	usart1_tx_byte
	ldr	r0, .L104+100
	bl	usart1_print
	b	.L59
.L52:
	ldr	r0, .L104+104
	bl	usart1_print
	ldr	r2, .L104+108
	ldr	r3, .L104+112
	ldrb	r0, [r4, #2]	@ zero_extendqisi2
	ldr	r7, .L104+12
	cmp	r0, #0
	ite	ne
	movne	r0, r2
	moveq	r0, r3
	bl	usart1_print
	ldr	r0, .L104+116
	bl	usart1_print
	ldr	r2, .L104+120
	ldr	r3, .L104+124
	ldrb	r0, [r4, #1]	@ zero_extendqisi2
	cmp	r0, #0
	ite	eq
	moveq	r0, r2
	movne	r0, r3
	bl	usart1_print
	ldr	r0, .L104+128
	bl	usart1_print
	ldrb	r0, [r7]	@ zero_extendqisi2
	adds	r0, r0, #48
	uxtb	r0, r0
	bl	usart1_tx_byte
	movs	r0, #61
	bl	usart1_tx_byte
	ldrb	r3, [r7]	@ zero_extendqisi2
	subs	r3, r3, #4
	uxtb	r3, r3
	cmp	r3, #4
	itet	ls
	ldrls	r2, .L104+132
	ldrhi	r0, .L104+136
	ldrls	r0, [r2, r3, lsl #2]
	bl	usart1_print
	ldr	r0, .L104+140
	bl	usart1_print
	ldr	r0, [r4, #64]
	bl	print_uint32
	ldr	r0, .L104+144
	bl	usart1_print
	ldr	r0, [r4, #48]
	bl	print_uint32
	ldr	r0, .L104+100
	bl	usart1_print
	b	.L59
.L105:
	.align	2
.L104:
	.word	1073876992
	.word	1073811456
	.word	.LANCHOR0
	.word	.LANCHOR1
	.word	.LC5
	.word	.LC6
	.word	.LC7
	.word	.LC8
	.word	.LC9
	.word	.LC10
	.word	.LANCHOR0+51
	.word	.LC24
	.word	.LC25
	.word	.LC26
	.word	.LC27
	.word	.LC28
	.word	.LC29
	.word	.LC30
	.word	.LC31
	.word	.LC32
	.word	.LC33
	.word	.LC34
	.word	.LC35
	.word	.LC36
	.word	.LC17
	.word	.LC18
	.word	.LC19
	.word	.LC1
	.word	.LC0
	.word	.LC20
	.word	.LC3
	.word	.LC2
	.word	.LC21
	.word	.LANCHOR2
	.word	.LC4
	.word	.LC22
	.word	.LC23
.L53:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L47
	ldr	r2, [sp, #48]
	ldrb	r1, [sp, #36]	@ zero_extendqisi2
	ldr	r0, [sp, #32]
	bl	mcp2515_tx_rtr
	cmp	r0, #0
	bne	.L60
	ldr	r3, [r4, #64]
	movs	r0, #122
	adds	r3, r3, #1
	str	r3, [r4, #64]
	bl	usart1_tx_byte
	movs	r0, #13
	bl	usart1_tx_byte
	b	.L48
.L54:
	ldrb	r3, [r4, #1]	@ zero_extendqisi2
	cmp	r3, #0
	beq	.L47
	ldr	r3, [sp, #48]
	ldrb	r2, [sp, #36]	@ zero_extendqisi2
	ldr	r0, [sp, #32]
	add	r1, sp, #37
	bl	mcp2515_tx
	cbnz	r0, .L60
	ldr	r3, [sp, #48]
	cmp	r3, #0
	ldr	r3, [r4, #64]
	ite	eq
	moveq	r0, #122
	movne	r0, #90
	adds	r3, r3, #1
	str	r3, [r4, #64]
	bl	usart1_tx_byte
	movs	r0, #13
	bl	usart1_tx_byte
	b	.L48
.L56:
	bl	mcp2515_enter_listen_only
	movs	r3, #0
	movs	r0, #13
	strb	r3, [r4, #1]
	bl	usart1_tx_byte
	b	.L59
.L57:
	bl	mcp2515_enter_normal
	cbnz	r0, .L47
	movs	r3, #1
	movs	r0, #13
	strb	r3, [r4, #1]
	bl	usart1_tx_byte
	b	.L59
.L55:
	bl	mcp2515_enter_listen_only
	movs	r3, #0
	ldrb	r0, [sp, #56]	@ zero_extendqisi2
	strb	r3, [r4, #1]
	bl	mcp2515_set_bitrate
	ldrb	r3, [sp, #56]	@ zero_extendqisi2
	ldr	r2, .L106
	movs	r0, #13
	strb	r3, [r2]
	bl	usart1_tx_byte
	b	.L59
.L47:
	movs	r0, #7
	bl	usart1_tx_byte
	b	.L59
.L60:
	movs	r0, #7
	bl	usart1_tx_byte
	b	.L48
.L65:
	cmp	r3, #0
	ble	.L18
	subs	r3, r3, #1
	str	r3, [r4, #4]
	b	.L18
.L100:
	ldrb	r3, [sp, #25]	@ zero_extendqisi2
	cmp	r3, #1
	bne	.L27
	ldrb	r3, [sp, #26]	@ zero_extendqisi2
	cmp	r3, #178
	bne	.L27
	movw	r4, #45057
	movw	r0, #20000
	ldr	r1, .L106+4
	ldr	r2, .L106+8
	ldr	r3, [r1, #28]
	orr	r3, r3, #402653184
	str	r3, [r1, #28]
	ldr	r3, [r2]
	sub	r1, r1, #107520
	orr	r3, r3, #256
	str	r3, [r2]
	strh	r4, [r1, #4]	@ movhi
	str	r0, [sp, #32]
.L28:
	ldr	r3, [sp, #32]
	subs	r2, r3, #1
	str	r2, [sp, #32]
	cmp	r3, #0
	bne	.L28
	mov	r3, #-536813568
	ldr	r2, .L106+12
	str	r2, [r3, #3340]
.L29:
	b	.L29
.L101:
	movs	r3, #1
	mov	r2, #8192
	ldr	r1, .L106+16
	ldr	r9, [sp, #60]
	strb	r3, [r4, #2]
	ldr	r0, .L106+20
	str	r7, [r4, #4]
	str	r2, [r1, #20]
	strb	r3, [r4]
	bl	usart1_print
	mov	r0, r9
	bl	print_uint32
	ldr	r0, .L106+24
	bl	usart1_print
	bl	mcp2515_enter_listen_only
	strb	r7, [r4, #1]
	bl	mcp2515_release_pins
	mov	r0, r9
	bl	usart3_hdsel_init
	mov	r3, r8
.L37:
	strb	r7, [r3, #1]!
	cmp	r3, r6
	bne	.L37
	movs	r7, #0
	mov	r9, r7
.L38:
	bl	usart1_getchar_nb
	cmp	r0, #0
	blt	.L39
	ldr	r1, [r4, #53]	@ unaligned
	uxtb	r10, r0
	str	r1, [r4, #52]
	ldr	r1, [r4, #57]	@ unaligned
	mov	r3, r8
	movs	r2, #109
	ldr	r0, .L106+28
	str	r1, [r4, #56]
	strb	r10, [r4, #60]
	b	.L42
.L102:
	ldrb	r2, [r0, #1]!	@ zero_extendqisi2
.L42:
	ldrb	r1, [r3, #1]!	@ zero_extendqisi2
	cmp	r1, r2
	bne	.L40
	cmp	r3, r6
	bne	.L102
	bl	usart3_deinit
	bl	mcp2515_restore_pins
	ldr	r3, .L106
	ldrb	r0, [r3]	@ zero_extendqisi2
	bl	mcp2515_init
	mov	r3, #8192
	ldr	r2, .L106+16
	ldr	r0, .L106+32
	str	r3, [r2, #16]
	movs	r3, #0
	strb	r3, [r4]
	strb	r3, [r4, #2]
	bl	usart1_print
	b	.L18
.L40:
	mov	r0, r10
	bl	usart3_tx_byte
	movs	r1, #5
	add	r0, sp, #24
	strb	r9, [sp, #24]
	bl	usart3_rx_byte_timeout
	cbnz	r0, .L43
	cbz	r7, .L103
.L44:
	movs	r7, #1
.L39:
	bl	usart3_getchar_nb
	cmp	r0, #0
	blt	.L38
	uxtb	r0, r0
	bl	usart1_tx_byte
	b	.L38
.L43:
	ldrb	r3, [sp, #24]	@ zero_extendqisi2
	cmp	r3, r10
	beq	.L39
	cmp	r7, #0
	bne	.L44
	ldr	r0, .L106+36
	bl	usart1_print
	b	.L44
.L103:
	ldr	r0, .L106+40
	bl	usart1_print
	b	.L44
.L107:
	.align	2
.L106:
	.word	.LANCHOR1
	.word	1073876992
	.word	1073770496
	.word	100270084
	.word	1073811456
	.word	.LC11
	.word	.LC12
	.word	.LC13
	.word	.LC16
	.word	.LC15
	.word	.LC14
	.size	main, .-main
	.section	.rodata.str1.4
	.align	2
.LC37:
	.ascii	"125k\000"
	.align	2
.LC38:
	.ascii	"250k\000"
	.align	2
.LC39:
	.ascii	"500k\000"
	.align	2
.LC40:
	.ascii	"800k\000"
	.align	2
.LC41:
	.ascii	"1M\000"
	.section	.rodata
	.align	2
	.set	.LANCHOR2,. + 0
	.type	CSWTCH.48, %object
	.size	CSWTCH.48, 20
CSWTCH.48:
	.word	.LC37
	.word	.LC38
	.word	.LC39
	.word	.LC40
	.word	.LC41
	.data
	.set	.LANCHOR1,. + 0
	.type	g_brate, %object
	.size	g_brate, 1
g_brate:
	.byte	6
	.bss
	.align	2
	.set	.LANCHOR0,. + 0
	.type	g_led_state, %object
	.size	g_led_state, 1
g_led_state:
	.space	1
	.type	g_can_open, %object
	.size	g_can_open, 1
g_can_open:
	.space	1
	.type	g_mode, %object
	.size	g_mode, 1
g_mode:
	.space	1
	.space	1
	.type	cmd_len, %object
	.size	cmd_len, 4
cmd_len:
	.space	4
	.type	cmd_buf, %object
	.size	cmd_buf, 40
cmd_buf:
	.space	40
	.type	g_rx_count, %object
	.size	g_rx_count, 4
g_rx_count:
	.space	4
	.type	exit_buf, %object
	.size	exit_buf, 9
exit_buf:
	.space	9
	.space	3
	.type	g_tx_count, %object
	.size	g_tx_count, 4
g_tx_count:
	.space	4
	.ident	"GCC: (15:13.2.rel1-2) 13.2.1 20231009"
