ENTRY(_start)
SECTIONS {
  . = 1M;
  .text   : ALIGN(4K) { *(.multiboot) *(.text*) }
  kernel_text_end = .;
  .rodata : ALIGN(4K) { *(.rodata*) }
  /* C++ global constructors (src/cxxrt.cpp runs them at boot) */
  .init_array : ALIGN(8) { __init_array_start = .; KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .; }
  .data   : ALIGN(4K) { *(.data*) }
  .bss    : ALIGN(4K) { *(COMMON) *(.bss*) }
  kernel_end = .;
}
