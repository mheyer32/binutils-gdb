cat <<EOF
OUTPUT_FORMAT("${OUTPUT_FORMAT}")
OUTPUT_ARCH(${ARCH})

${RELOCATING+${LIB_SEARCH_DIRS}}
${STACKZERO+${RELOCATING+${STACKZERO}}}
${SHLIB_PATH+${RELOCATING+${SHLIB_PATH}}}

SECTIONS
{
  ${RELOCATING+PROVIDE(___machtype = 0x0);}
  ${RELOCATING+. = ${TEXT_START_ADDR};}
  .text :
  {
    ${RELOCATING+__stext = .;}
    *(.text)
    *(.text.main)
    *(.text*)
    *(.rodata*)
    *(.data.rel.ro*)
    ___INIT_LIST__ = .;
    LONG((___INIT_LIST__END__ - ___INIT_LIST__) / 4 - 2)
    *(.list___INIT_LIST__)
    LONG(0)
    ___INIT_LIST__END__ = .;    
    ___EXIT_LIST__ = .;
    LONG((___EXIT_LIST__END__ - ___EXIT_LIST__) / 4 - 2)
    *(.list___EXIT_LIST__)
    LONG(0)
    ___EXIT_LIST__END__ = .;    
    ___CTOR_LIST__ = .;
    LONG((___CTOR_LIST__END__ - ___CTOR_LIST__) / 4 - 2)
    *(.list___CTOR_LIST__)
    LONG(0)
    ___CTOR_LIST__END__ = .;    
    ___DTOR_LIST__ = .;
    LONG((___DTOR_LIST__END__ - ___DTOR_LIST__) / 4 - 2)
    *(.list___DTOR_LIST__)
    LONG(0)
    ___DTOR_LIST__END__ = .;    
    ___EH_FRAME_BEGINS__ = .;
    LONG((___EH_FRAME_BEGINS__END__ - ___EH_FRAME_BEGINS__) / 4 - 2)
    *(.list___EH_FRAME_BEGINS__)
    LONG(0)
    ___EH_FRAME_BEGINS__END__ = .;    
    *(.gnu.linkonce.t.*)
    *(.gnu.linkonce.r.*)
    ${RELOCATING+___datadata_relocs = .;}
    ${RELOCATING+__etext = .;}
    ${PAD_TEXT+${RELOCATING+. = ${DATA_ALIGNMENT};}}
  }
  ${RELOCATING+___text_size = SIZEOF(.text);}
  ${RELOCATING+. = ${DATA_ALIGNMENT};}
  .data :
  {
    ${RELOCATING+__sdata = .;}
    *(.data)
    *(.data.*)    
    *(.gnu.linkonce.d.*)
    ${RELOCATING+___LIB_LIST__ = .;}
    LONG((___LIB_LIST__END__ - ___LIB_LIST__) / 4 - 2)
    *(.list___LIB_LIST__)
    LONG(0)
    ${RELOCATING+___LIB_LIST__END__ = .;}    
    ${RELOCATING+___EH_FRAME_OBJECTS__ = .;}
    LONG((___EH_FRAME_OBJECTS__END__ - ___EH_FRAME_OBJECTS__) / 4 - 2)
    *(.list___EH_FRAME_OBJECTS__)
    LONG(0)
    ${RELOCATING+___EH_FRAME_OBJECTS__END__ = .;}   
    ${RELOCATING+___a4_init = 0x7ffe;}
    ${RELOCATING+__edata = .;}
  }
  .bss :
  {
    ${RELOCATING+__bss_start = .;}
    *(.bss)
    *(.bss.*)
    *(COMMON)
    ${RELOCATING+__end = .;}
  }
  ${RELOCATING+___data_size = SIZEOF(.data) + SIZEOF(.bss);}
  ${RELOCATING+___bss_size = 0x0;}
  ${RELOCATING+___bss_init_size = SIZEOF(.bss);}
  .datachip :
  {
    *(.datachip)
  }
  .bsschip :
  {
    *(.bsschip)
  }  
}
EOF
