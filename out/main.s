.intel_syntax noprefix
.section .rodata

.data

.text
.global main
main:
  push rbp
  mov rbp, rsp
  sub rsp, 16
.L_main_0:
  mov rax, 0
  push rax
.L_main_1:
  pop rax
  jmp .L_main_return
.L_main_2:
  xor rax, rax
.L_main_return:
  mov rsp, rbp
  pop rbp
  ret

.section .note.GNU-stack,"",@progbits
