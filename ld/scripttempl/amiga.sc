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
    *(_*)
    *(.rodata*)
    *(.data.rel.ro*)
    *(.gnu.linkonce.t.*)
    *(.gnu.linkonce.r.*)
    *(.gcc_except_table*)
    *(SORT_BY_NAME(.list___EH_FRAME*))
    *(SORT_BY_NAME(.list_*))
    *(.end_of_lists)
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
    *(SORT_BY_NAME(.data.*))
    ${CONSTRUCTING+CONSTRUCTORS}    
    *(SORT_BY_NAME(.dlist___EH_FRAME_OBJECT*))
    *(SORT_BY_NAME(.dlist_*))
    *(.end_of_dlists)
    *(.data.__EH_FRAME_OBJECT__*)
    *(.gnu.linkonce.d.*)
    ${RELOCATING+__edata = .;}
  }
  ${RELOCATING+___data_size = SIZEOF(.data);}
  .bss :
  {
    ${RELOCATING+__bss_start = .;}
    *(.bss)
    *(.bss.*)
    *(COMMON)
    ${RELOCATING+__end = .;}
  }
  ${RELOCATING+___bss_size = SIZEOF(.bss);}
  .datachip :
  {
    *(.datachip)
  }
  .bsschip :
  {
    *(.bsschip)
  }
  .dwarf :
  {
    ${RELOCATING+__debug_frame_start = .;}
    *(.debug_frame)
    ${RELOCATING+__debug_info_start = .;}
    *(.debug_info)
    ${RELOCATING+__debug_abbrev_start = .;}
    *(.debug_abbrev)
      ${RELOCATING+__debug_loclists_start = .;}
    *(.debug_loclists)
    ${RELOCATING+__debug_aranges_start = .;}
    *(.debug_aranges)
    ${RELOCATING+__debug_rgnlists_start = .;}
    *(.debug_rnglists)
    ${RELOCATING+__debug_line_start = .;}
    *(.debug_line)
    ${RELOCATING+__debug_str_start = .;}
    *(.debug_str)
    ${RELOCATING+__debug_line_str_start = .;}
    *(.debug_line_str)
  }  
}
EOF
