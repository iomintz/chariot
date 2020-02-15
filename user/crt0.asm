section .data
;; storage for kernel parameters
extern __argc
extern __argv
extern environ


section .text


extern libc_start

global _start
_start:

	mov QWORD [__argc], rdi
	mov QWORD [__argv], rsi
	mov QWORD [environ], rdx
	; mov QWORD [__envp], rdi

	mov rbp, rsp
	push rbp

	jmp libc_start

	pop rbp

	.invalid_loop:
		mov rax, [0x00]
		jmp .invalid_loop



# set the dynamic linker
section .interp
	db "/lib/ld.so", 0
