OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  . = 0x80000000;

  .text : {
    *(.text .text.*)
    . = ALIGN(0x1000);
    /*
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    */
    PROVIDE(etext = .);
  }

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  PROVIDE(end = .);

  /* allocation area markers: 为实验预留一段物理页作为 ALLOC 区域
   * ALLOC_BEGIN 在 end 向上对齐到页面后，ALLO C_END = ALLOC_BEGIN + 1024*PGSIZE
   * 这样 pmem.c 能使用 ALLOC_BEGIN/ALLOC_END 来初始化内核/用户页池
   */
  . = ALIGN(0x1000);
  PROVIDE(ALLOC_BEGIN = .);
  . = . + (1024 * 4096);
  PROVIDE(ALLOC_END = .);
}
