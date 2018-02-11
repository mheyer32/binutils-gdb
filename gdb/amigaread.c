/* Read AmigaHunk object files for GDB.

   Copyright (C) 2018 Free Software Foundation, Inc.

   Written by Stefan Bebbo Franke

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include "defs.h"
#include "bfd.h"
#include "symtab.h"
#include "symfile.h"
#include "objfiles.h"
#include "buildsym.h"
#include "stabsread.h"
#include "gdb-stabs.h"
#include "complaints.h"
#include "demangle.h"
#include "psympriv.h"
#include "filenames.h"
#include "probe.h"
#include "arch-utils.h"
#include "gdbtypes.h"
#include "value.h"
#include "infcall.h"
#include "gdbthread.h"
#include "regcache.h"
#include "bcache.h"
#include "gdb_bfd.h"
#include "build-id.h"
#include "location.h"
#include "auxv.h"
#include "common-types.h"

#ifndef NULL
#define NULL ((void*)0)
#endif

#define AT_HWCAP	16		/* Machine dependent hints about
					   processor capabilities.  */


/* Per-BFD data for probe info.  */

static const struct bfd_data *probe_key = NULL;

/* The data pointer is htab_t for gnu_ifunc_record_cache_unchecked.  */

static const struct objfile_data *amiga_objfile_gnu_ifunc_cache_data;


/* Map function names to CORE_ADDR in amiga_objfile_gnu_ifunc_cache_data.  */

struct amiga_gnu_ifunc_cache
{
  /* This is always a function entry address, not a function descriptor.  */
  CORE_ADDR addr;

  char name[1];
};

/* htab_hash for amiga_objfile_gnu_ifunc_cache_data.  */

static hashval_t
amiga_gnu_ifunc_cache_hash (const void *a_voidp)
{
  const struct amiga_gnu_ifunc_cache *a
    = (const struct amiga_gnu_ifunc_cache *) a_voidp;

  return htab_hash_string (a->name);
}

/* htab_eq for amiga_objfile_gnu_ifunc_cache_data.  */

static int
amiga_gnu_ifunc_cache_eq (const void *a_voidp, const void *b_voidp)
{
  const struct amiga_gnu_ifunc_cache *a
    = (const struct amiga_gnu_ifunc_cache *) a_voidp;
  const struct amiga_gnu_ifunc_cache *b
    = (const struct amiga_gnu_ifunc_cache *) b_voidp;

  return strcmp (a->name, b->name) == 0;
}

/* Record the target function address of a STT_GNU_IFUNC function NAME is the
   function entry address ADDR.  Return 1 if NAME and ADDR are considered as
   valid and therefore they were successfully recorded, return 0 otherwise.

   Function does not expect a duplicate entry.  Use
   amiga_gnu_ifunc_resolve_by_cache first to check if the entry for NAME already
   exists.  */

static int
amiga_gnu_ifunc_record_cache (const char *name, CORE_ADDR addr)
{
  struct bound_minimal_symbol msym;
  asection *sect;
  struct objfile *objfile;
  htab_t htab;
  struct amiga_gnu_ifunc_cache entry_local, *entry_p;
  void **slot;

  msym = lookup_minimal_symbol_by_pc (addr);
  if (msym.minsym == NULL)
    return 0;
  if (BMSYMBOL_VALUE_ADDRESS (msym) != addr)
    return 0;
  /* minimal symbols have always SYMBOL_OBJ_SECTION non-NULL.  */
  sect = MSYMBOL_OBJ_SECTION (msym.objfile, msym.minsym)->the_bfd_section;
  objfile = msym.objfile;

  /* If .plt jumps back to .plt the symbol is still deferred for later
     resolution and it has no use for GDB.  Besides ".text" this symbol can
     reside also in ".opd" for ppc64 function descriptor.  */
  if (strcmp (bfd_get_section_name (objfile->obfd, sect), ".plt") == 0)
    return 0;

  htab = (htab_t) objfile_data (objfile, amiga_objfile_gnu_ifunc_cache_data);
  if (htab == NULL)
    {
      htab = htab_create_alloc_ex (1, amiga_gnu_ifunc_cache_hash,
				   amiga_gnu_ifunc_cache_eq,
				   NULL, &objfile->objfile_obstack,
				   hashtab_obstack_allocate,
				   dummy_obstack_deallocate);
      set_objfile_data (objfile, amiga_objfile_gnu_ifunc_cache_data, htab);
    }

  entry_local.addr = addr;
  obstack_grow (&objfile->objfile_obstack, &entry_local,
		offsetof (struct amiga_gnu_ifunc_cache, name));
  obstack_grow_str0 (&objfile->objfile_obstack, name);
  entry_p
    = (struct amiga_gnu_ifunc_cache *) obstack_finish (&objfile->objfile_obstack);

  slot = htab_find_slot (htab, entry_p, INSERT);
  if (*slot != NULL)
    {
      struct amiga_gnu_ifunc_cache *entry_found_p
	= (struct amiga_gnu_ifunc_cache *) *slot;
      struct gdbarch *gdbarch = get_objfile_arch (objfile);

      if (entry_found_p->addr != addr)
	{
	  /* This case indicates buggy inferior program, the resolved address
	     should never change.  */

	    warning (_("gnu-indirect-function \"%s\" has changed its resolved "
		       "function_address from %s to %s"),
		     name, paddress (gdbarch, entry_found_p->addr),
		     paddress (gdbarch, addr));
	}

      /* New ENTRY_P is here leaked/duplicate in the OBJFILE obstack.  */
    }
  *slot = entry_p;

  return 1;
}

/* Try to find the target resolved function entry address of a STT_GNU_IFUNC
   function NAME.  If the address is found it is stored to *ADDR_P (if ADDR_P
   is not NULL) and the function returns 1.  It returns 0 otherwise.

   Only the amiga_objfile_gnu_ifunc_cache_data hash table is searched by this
   function.  */

static int
amiga_gnu_ifunc_resolve_by_cache (const char *name, CORE_ADDR *addr_p)
{
  struct objfile *objfile;

  ALL_PSPACE_OBJFILES (current_program_space, objfile)
    {
      htab_t htab;
      struct amiga_gnu_ifunc_cache *entry_p;
      void **slot;

      htab = (htab_t) objfile_data (objfile, amiga_objfile_gnu_ifunc_cache_data);
      if (htab == NULL)
	continue;

      entry_p = ((struct amiga_gnu_ifunc_cache *)
		 alloca (sizeof (*entry_p) + strlen (name)));
      strcpy (entry_p->name, name);

      slot = htab_find_slot (htab, entry_p, NO_INSERT);
      if (slot == NULL)
	continue;
      entry_p = (struct amiga_gnu_ifunc_cache *) *slot;
      gdb_assert (entry_p != NULL);

      if (addr_p)
	*addr_p = entry_p->addr;
      return 1;
    }

  return 0;
}


/* Try to find the target resolved function entry address of a STT_GNU_IFUNC
   function NAME.  If the address is found it is stored to *ADDR_P (if ADDR_P
   is not NULL) and the function returns 1.  It returns 0 otherwise.
   */

static int
amiga_gnu_ifunc_resolve_name (const char *name, CORE_ADDR *addr_p)
{
  if (amiga_gnu_ifunc_resolve_by_cache (name, addr_p))
    return 1;

  return 0;
}

/* Call STT_GNU_IFUNC - a function returning addresss of a real function to
   call.  PC is theSTT_GNU_IFUNC resolving function entry.  The value returned
   is the entry point of the resolved STT_GNU_IFUNC target function to call.
   */

static CORE_ADDR
amiga_gnu_ifunc_resolve_addr (struct gdbarch *gdbarch, CORE_ADDR pc)
{
  const char *name_at_pc;
  CORE_ADDR start_at_pc, address;
  struct type *func_func_type = builtin_type (gdbarch)->builtin_func_func;
  struct value *function, *address_val;
  CORE_ADDR hwcap = 0;
  struct value *hwcap_val;

  /* Try first any non-intrusive methods without an inferior call.  */

  if (find_pc_partial_function (pc, &name_at_pc, &start_at_pc, NULL)
      && start_at_pc == pc)
    {
      if (amiga_gnu_ifunc_resolve_name (name_at_pc, &address))
	return address;
    }
  else
    name_at_pc = NULL;

  function = allocate_value (func_func_type);
  VALUE_LVAL (function) = lval_memory;
  set_value_address (function, pc);

  /* STT_GNU_IFUNC resolver functions usually receive the HWCAP vector as
     parameter.  FUNCTION is the function entry address.  ADDRESS may be a
     function descriptor.  */

  target_auxv_search (&current_target, AT_HWCAP, &hwcap);
  hwcap_val = value_from_longest (builtin_type (gdbarch)
				  ->builtin_unsigned_long, hwcap);
  address_val = call_function_by_hand (function, NULL, 1, &hwcap_val);
  address = value_as_address (address_val);
  address = gdbarch_convert_from_func_ptr_addr (gdbarch, address,
						&current_target);
  address = gdbarch_addr_bits_remove (gdbarch, address);

  if (name_at_pc)
   amiga_gnu_ifunc_record_cache (name_at_pc, address);

  return address;
}

/* Handle inferior hit of bp_gnu_ifunc_resolver, see its definition.  */

static void
amiga_gnu_ifunc_resolver_stop (struct breakpoint *b)
{
  struct breakpoint *b_return;
  struct frame_info *prev_frame = get_prev_frame (get_current_frame ());
  struct frame_id prev_frame_id = get_stack_frame_id (prev_frame);
  CORE_ADDR prev_pc = get_frame_pc (prev_frame);
  int thread_id = ptid_to_global_thread_id (inferior_ptid);

  gdb_assert (b->type == bp_gnu_ifunc_resolver);

  for (b_return = b->related_breakpoint; b_return != b;
       b_return = b_return->related_breakpoint)
    {
      gdb_assert (b_return->type == bp_gnu_ifunc_resolver_return);
      gdb_assert (b_return->loc != NULL && b_return->loc->next == NULL);
      gdb_assert (frame_id_p (b_return->frame_id));

      if (b_return->thread == thread_id
	  && b_return->loc->requested_address == prev_pc
	  && frame_id_eq (b_return->frame_id, prev_frame_id))
	break;
    }

  if (b_return == b)
    {
      /* No need to call find_pc_line for symbols resolving as this is only
	 a helper breakpointer never shown to the user.  */

      symtab_and_line sal;
      sal.pspace = current_inferior ()->pspace;
      sal.pc = prev_pc;
      sal.section = find_pc_overlay (sal.pc);
      sal.explicit_pc = 1;
      b_return = set_momentary_breakpoint (get_frame_arch (prev_frame), sal,
					   prev_frame_id,
					   bp_gnu_ifunc_resolver_return);

      /* set_momentary_breakpoint invalidates PREV_FRAME.  */
      prev_frame = NULL;

      /* Add new b_return to the ring list b->related_breakpoint.  */
      gdb_assert (b_return->related_breakpoint == b_return);
      b_return->related_breakpoint = b->related_breakpoint;
      b->related_breakpoint = b_return;
    }
}

/* Handle inferior hit of bp_gnu_ifunc_resolver_return, see its definition.  */

static void
amiga_gnu_ifunc_resolver_return_stop (struct breakpoint *b)
{
  struct gdbarch *gdbarch = get_frame_arch (get_current_frame ());
  struct type *func_func_type = builtin_type (gdbarch)->builtin_func_func;
  struct type *value_type = TYPE_TARGET_TYPE (func_func_type);
  struct regcache *regcache = get_thread_regcache (inferior_ptid);
  struct value *func_func;
  struct value *value;
  CORE_ADDR resolved_address, resolved_pc;

  gdb_assert (b->type == bp_gnu_ifunc_resolver_return);

  while (b->related_breakpoint != b)
    {
      struct breakpoint *b_next = b->related_breakpoint;

      switch (b->type)
	{
	case bp_gnu_ifunc_resolver:
	  break;
	case bp_gnu_ifunc_resolver_return:
	  delete_breakpoint (b);
	  break;
	default:
	  internal_error (__FILE__, __LINE__,
			  _("handle_inferior_event: Invalid "
			    "gnu-indirect-function breakpoint type %d"),
			  (int) b->type);
	}
      b = b_next;
    }
  gdb_assert (b->type == bp_gnu_ifunc_resolver);
  gdb_assert (b->loc->next == NULL);

  func_func = allocate_value (func_func_type);
  VALUE_LVAL (func_func) = lval_memory;
  set_value_address (func_func, b->loc->related_address);

  value = allocate_value (value_type);
  gdbarch_return_value (gdbarch, func_func, value_type, regcache,
			value_contents_raw (value), NULL);
  resolved_address = value_as_address (value);
  resolved_pc = gdbarch_convert_from_func_ptr_addr (gdbarch,
						    resolved_address,
						    &current_target);
  resolved_pc = gdbarch_addr_bits_remove (gdbarch, resolved_pc);

  gdb_assert (current_program_space == b->pspace || b->pspace == NULL);
  amiga_gnu_ifunc_record_cache (event_location_to_string (b->location.get ()),
			      resolved_pc);

  b->type = bp_breakpoint;
  update_breakpoint_locations (b, current_program_space,
			       find_pc_line (resolved_pc, 0), {});
}


/* Locate the segments in ABFD.  */

static struct symfile_segment_data *
amiga_symfile_segments (bfd *abfd)
{
  return 0;
}


/* Scan and build partial symbols for a symbol file.
   We have been initialized by a call to amiga_symfile_init, which
   currently does nothing.

   This function only does the minimum work necessary for letting the
   user "name" things symbolically; it does not read the entire symtab.
   Instead, it reads the external and static symbols and puts them in partial
   symbol tables.  When more extensive information is requested of a
   file, the corresponding partial symbol table is mutated into a full
   fledged symbol table by going back and reading the symbols
   for real.

*/

static void
amiga_symfile_read (struct objfile *objfile, symfile_add_flags symfile_flags)
{
  bfd *abfd = objfile->obfd;
}

/* Initialize anything that needs initializing when a completely new symbol
   file is specified (not just adding some symbols from another file, e.g. a
   shared library).

   We reinitialize buildsym, since we may be reading stabs from an Amiga
   file.  */

static void
amiga_new_init (struct objfile *ignore)
{
  stabsread_new_init ();
  buildsym_new_init ();
}

/* Perform any local cleanups required when we are done with a particular
   objfile.  I.E, we are in the process of discarding all symbol information
   for an objfile, freeing up all memory held for it, and unlinking the
   objfile struct from the global list of known objfiles.  */

static void
amiga_symfile_finish (struct objfile *objfile)
{
  if (objfile -> sym_stab_info != NULL)
    {
      mfree (objfile -> md, objfile -> sym_stab_info);
    }
}

/* Amiga specific initialization routine for reading symbols.  */

static void
amiga_symfile_init (struct objfile *objfile)
{
  int val;
  bfd *sym_bfd = objfile->obfd;
  char *name = bfd_get_filename (sym_bfd);
  asection *text_sect;

  /* Allocate struct to keep track of the symfile */
  objfile->sym_stab_info = (PTR)
    xmmalloc (objfile -> md, sizeof (struct dbx_symfile_info));
  memset ((PTR) objfile->sym_stab_info, 0, sizeof (struct dbx_symfile_info));

  /* FIXME POKING INSIDE BFD DATA STRUCTURES */
#define	STRING_TABLE_OFFSET	adata(sym_bfd).str_filepos
#define	SYMBOL_TABLE_OFFSET	adata(sym_bfd).sym_filepos

  /* FIXME POKING INSIDE BFD DATA STRUCTURES */

  DBX_SYMFILE_INFO (objfile)->stab_section_info = NULL;
  text_sect = bfd_get_section_by_name (sym_bfd, ".text");
  if (!text_sect)
    error ("Can't find .text section in symbol file");
  DBX_TEXT_ADDR (objfile) = bfd_section_vma (sym_bfd, text_sect);
  DBX_TEXT_SIZE (objfile) = bfd_section_size (sym_bfd, text_sect);

  /* FIXME: I suspect this should be external_nlist.  The size of host
     types like long and bfd_vma should not affect how we read the
     file.  */
  DBX_SYMBOL_SIZE (objfile) = sizeof (struct internal_nlist);
  DBX_SYMCOUNT (objfile) = AMIGA_DATA(sym_bfd)->symtab_size
    / DBX_SYMBOL_SIZE (objfile);
  DBX_SYMTAB_OFFSET (objfile) = SYMBOL_TABLE_OFFSET;

  /* Read the string table and stash it away in the psymbol_obstack.  It is
     only needed as long as we need to expand psymbols into full symbols,
     so when we blow away the psymbol the string table goes away as well.
     Note that gdb used to use the results of attempting to malloc the
     string table, based on the size it read, as a form of sanity check
     for botched byte swapping, on the theory that a byte swapped string
     table size would be so totally bogus that the malloc would fail.  Now
     that we put in on the psymbol_obstack, we can't do this since gdb gets
     a fatal error (out of virtual memory) if the size is bogus.  We can
     however at least check to see if the size is zero or some negative
     value. */

  DBX_STRINGTAB_SIZE (objfile) = AMIGA_DATA (sym_bfd)->stringtab_size;

  if (DBX_SYMCOUNT (objfile) == 0
      || DBX_STRINGTAB_SIZE (objfile) == 0)
    return;

  if (DBX_STRINGTAB_SIZE (objfile) <= 0
      || DBX_STRINGTAB_SIZE (objfile) > bfd_get_size (sym_bfd))
    error ("ridiculous string table size (%d bytes).",
	   DBX_STRINGTAB_SIZE (objfile));

  DBX_STRINGTAB (objfile) =
    (char *) obstack_alloc (&objfile -> psymbol_obstack,
			    DBX_STRINGTAB_SIZE (objfile));
  OBJSTAT (objfile, sz_strtab += DBX_STRINGTAB_SIZE (objfile));

  /* Now read in the string table in one big gulp.  */

  val = bfd_seek (sym_bfd, STRING_TABLE_OFFSET, L_SET);
  if (val < 0)
    perror_with_name (name);
  val = bfd_read (DBX_STRINGTAB (objfile), DBX_STRINGTAB_SIZE (objfile), 1,
		  sym_bfd);
  if (val == 0)
    error ("End of file reading string table");
  else if (val < 0)
    /* It's possible bfd_read should be setting bfd_error, and we should be
       checking that.  But currently it doesn't set bfd_error.  */
    perror_with_name (name);
  else if (val != DBX_STRINGTAB_SIZE (objfile))
    error ("Short read reading string table");
}


/* Implementation of `sym_get_probes', as documented in symfile.h.  */

static const std::vector<probe *> &
amiga_get_probes (struct objfile *objfile)
{
  std::vector<probe *> *probes_per_bfd;

  /* Have we parsed this objfile's probes already?  */
  probes_per_bfd = (std::vector<probe *> *) bfd_data (objfile->obfd, probe_key);

  if (probes_per_bfd == NULL)
    {
      probes_per_bfd = new std::vector<probe *>;

      /* Here we try to gather information about all types of probes from the
	 objfile.  */
      for (const probe_ops *ops : all_probe_ops)
	ops->get_probes (probes_per_bfd, objfile);

      set_bfd_data (objfile->obfd, probe_key, probes_per_bfd);
    }

  return *probes_per_bfd;
}

/* Helper function used to free the space allocated for storing SystemTap
   probe information.  */

static void
probe_key_free (bfd *abfd, void *d)
{
  std::vector<probe *> *probes = (std::vector<probe *> *) d;

  for (probe *p : *probes)
    p->pops->destroy (p);

  delete probes;
}




/* Implementation `sym_probe_fns', as documented in symfile.h.  */

static const struct sym_probe_fns amiga_probe_fns =
{
  amiga_get_probes,		    /* sym_get_probes */
};

/* Register that we are able to handle ELF object file formats.  */

static const struct sym_fns amiga_sym_fns =
{
  amiga_new_init,			/* init anything gbl to entire symtab */
  amiga_symfile_init,		/* read initial info, setup for sym_read() */
  amiga_symfile_read,		/* read a symbol file into symtab */
  NULL,				/* sym_read_psymbols */
  amiga_symfile_finish,		/* finished with file, cleanup */
  default_symfile_offsets,	/* Translate ext. to int. relocation */
  amiga_symfile_segments,		/* Get segment information from a file.  */
  NULL,
  default_symfile_relocate,	/* Relocate a debug section.  */
  &amiga_probe_fns,		/* sym_probe_fns */
  &psym_functions
};

/* STT_GNU_IFUNC resolver vector to be installed to gnu_ifunc_fns_p.  */

static const struct gnu_ifunc_fns amiga_gnu_ifunc_fns =
{
  amiga_gnu_ifunc_resolve_addr,
  amiga_gnu_ifunc_resolve_name,
  amiga_gnu_ifunc_resolver_stop,
  amiga_gnu_ifunc_resolver_return_stop
};

void
_initialize_elfread (void)
{
  probe_key = register_bfd_data_with_cleanup (NULL, probe_key_free);
  add_symtab_fns (bfd_target_amiga_flavour, &amiga_sym_fns);

  amiga_objfile_gnu_ifunc_cache_data = register_objfile_data ();
  gnu_ifunc_fns_p = &amiga_gnu_ifunc_fns;
}
