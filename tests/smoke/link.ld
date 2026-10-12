/* 冒烟测试链接脚本：把镜像钉在 RAM 基址/复位向量处 */
ENTRY(_start)
SECTIONS
{
  . = 0x80000000;

  .text : {
    *(.text.start)
    *(.text*)
  }

  .rodata : { *(.rodata*) }
  .data   : { *(.data*) }
  .bss    : { *(.bss*) *(COMMON) }

  /DISCARD/ : { *(.note*) *(.riscv.attributes) *(.comment) }
}
