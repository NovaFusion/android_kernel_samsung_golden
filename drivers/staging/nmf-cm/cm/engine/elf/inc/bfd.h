/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Jean-Philippe FASSINO <jean-philippe.fassino@stericsson.com> for ST-Ericsson.
 * License terms: GNU General Public License (GPL) version 2.
 */
/*!
 * \internal
 * \brief Elf bfd relocation.
 *
 * \defgroup ELFLOADER MMDSP ELF loader.
 */
#ifndef __INC_CM_ELF_BFD_H
#define __INC_CM_ELF_BFD_H

#include <cm/inc/cm_type.h>

/*
 * Relocation spcification
 */
enum complain_overflow
{
  /* Do not complain on overflow.  */
  complain_overflow_dont,

  /* Complain if the bitfield overflows, whether it is considered
     as signed or unsigned.  */
  complain_overflow_bitfield,

  /* Complain if the value overflows when considered as signed
     number.  */
  complain_overflow_signed,

  /* Complain if the value overflows when considered as an
     unsigned number.  */
  complain_overflow_unsigned
};

struct reloc_howto_struct
{
  /*  The type field has mainly a documentary use - the back end can
      do what it wants with it, though normally the back end's
      external idea of what a reloc number is stored
      in this field.  For example, a PC relative word relocation
      in a coff environment has the type 023 - because that's
      what the outside world calls a R_PCRWORD reloc.  */
  unsigned int type;

  /*  The value the final relocation is shifted right by.  This drops
      unwanted data from the relocation.  */
  unsigned int rightshift;

  /*  The size of the item to be relocated.  This is *not* a
      power-of-two measure.  To get the number of bytes operated
      on by a type of relocation, use bfd_get_reloc_size.  */
  int size;

  /*  The number of bits in the item to be relocated.  This is used
      when doing overflow checking.  */
  unsigned int bitsize;

  /*  Notes that the relocation is relative to the location in the
      data section of the addend.  The relocation function will
      subtract from the relocation value the address of the location
      being relocated.  */
  t_uint64 pc_relative;

  /*  The bit position of the reloc value in the destination.
      The relocated value is left shifted by this amount.  */
  unsigned int bitpos;

  /* What type of overflow error should be checked for when
     relocating.  */
  enum complain_overflow complain_on_overflow;

  void (*special_function)(void);

  /* The textual name of the relocation type.  */
  char *name;

  /* Some formats record a relocation addend in the section contents
     rather than with the relocation.  For ELF formats this is the
     distinction between USE_REL and USE_RELA (though the code checks
     for USE_REL == 1/0).  The value of this field is TRUE if the
     addend is recorded with the section contents; when performing a
     partial link (ld -r) the section contents (the data) will be
     modified.  The value of this field is FALSE if addends are
     recorded with the relocation (in arelent.addend); when performing
     a partial link the relocation will be modified.
     All relocations for all ELF USE_RELA targets should set this field
     to FALSE (values of TRUE should be looked on with suspicion).
     However, the converse is not true: not all relocations of all ELF
     USE_REL targets set this field to TRUE.  Why this is so is peculiar
     to each particular target.  For relocs that aren't used in partial
     links (e.g. GOT stuff) it doesn't matter what this is set to.  */
  char partial_inplace;

  /* src_mask selects the part of the instruction (or data) to be used
     in the relocation sum.  If the target relocations don't have an
     addend in the reloc, eg. ELF USE_REL, src_mask will normally equal
     dst_mask to extract the addend from the section contents.  If
     relocations do have an addend in the reloc, eg. ELF USE_RELA, this
     field should be zero.  Non-zero values for ELF USE_RELA targets are
     bogus as in those cases the value in the dst_mask part of the
     section contents should be treated as garbage.  */
  t_uint64 src_mask;

  /* dst_mask selects which parts of the instruction (or data) are
     replaced with a relocated value.  */
  t_uint64 dst_mask;

  /* When some formats create PC relative instructions, they leave
     the value of the pc of the place being relocated in the offset
     slot of the instruction, so that a PC relative relocation can
     be made just by adding in an ordinary offset (e.g., sun3 a.out).
     Some formats leave the displacement part of an instruction
     empty (e.g., m88k bcs); this flag signals the fact.  */
  char pcrel_offset;
};

#define HOWTO(C, R, S, B, P, BI, O, SF, NAME, INPLACE, MASKSRC, MASKDST, PC) \
  { (unsigned) C, R, S, B, P, BI, O, SF, NAME, INPLACE, MASKSRC, MASKDST, PC }

#endif
