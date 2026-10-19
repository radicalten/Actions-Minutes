@ =============================================================================
@ poly_data.s — embeds the hardware-recorded rasterization reference stream
@
@ Source: Jaklyy/polyrastertest data/rastertest.data (DataVersion 2, 43,664 B),
@ recorded on a New 3DS XL. Incbin keeps the binary out of the C++ source and
@ puts it in the flat ARM9 image, where poly_ref.cpp reads it.
@ =============================================================================
.syntax unified
.arm
.section .text
.align 2

.global poly_data_start
.global poly_data_end

poly_data_start:
.incbin "src9/poly/rastertest.data"
poly_data_end:

.align 2
